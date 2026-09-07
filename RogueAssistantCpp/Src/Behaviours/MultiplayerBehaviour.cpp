#include "Behaviours/MultiplayerBehaviour.h"
#include "Bridge/BridgeProtocol.h"
#include "GameConnection.h"
#include "GameData.h"
#include "Log.h"
#include "StringUtils.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <vector>

enum RogueNetChannel : enet_uint8
{
	Control,
	Handshake,
	GameState,
	PlayerState,
	PlayerProfiles,
	Count,
};

u16 const MultiplayerBehaviour::c_DefaultPort = 30025;

namespace
{
// Keep in sync with the ROM API 3 state machine.
constexpr u8 NetStateHost = 2U << 0U;
constexpr u8 HandshakeStateSendToHost = 1;
constexpr u8 HandshakeStateSendToClient = 2;
constexpr auto CompatibilityTimeout = std::chrono::seconds(5);
constexpr std::size_t MaxRomPayloadSize = rogue::bridge::MaximumFrameBodyLength - rogue::bridge::FrameBodyHeaderSize;

struct PacketDeleter
{
	void operator()(ENetPacket* packet) const
	{
		if (packet != nullptr)
			enet_packet_destroy(packet);
	}
};

using PacketPtr = std::unique_ptr<ENetPacket, PacketDeleter>;

bool SendPacket(ENetPeer* peer, enet_uint8 channel, void const* data, std::size_t size, enet_uint32 flags)
{
	if (peer == nullptr)
		return false;

	ENetPacket* packet = enet_packet_create(data, size, flags);
	if (packet == nullptr)
	{
		LOG_ERROR("ENet: Failed to allocate packet");
		return false;
	}
	if (enet_peer_send(peer, channel, packet) != 0)
	{
		LOG_ERROR("ENet: Failed to queue packet on channel %u", static_cast<unsigned>(channel));
		enet_packet_destroy(packet);
		return false;
	}
	return true;
}

bool CheckedProduct(std::uint32_t left, std::uint32_t right, std::size_t& product)
{
	if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
		return false;
	product = static_cast<std::size_t>(left) * right;
	return true;
}

bool ParsePort(std::string_view text, std::uint16_t& port)
{
	std::uint32_t parsed = 0;
	auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
	if (error != std::errc{} || end != text.data() + text.size() || parsed == 0 || parsed > 65535)
		return false;
	port = static_cast<std::uint16_t>(parsed);
	return true;
}
} // namespace

// The ROM supplies every offset and size below, and the network supplies every
// packet size. Check the whole layout before reading fields from the copied
// multiplayer state.
static bool ValidateMultiplayerLayout(GameConnection& game)
{
	ObservedGameMemory const& memory = game.GetObservedGameMemory();
	GameStructures::RogueAssistantHeader const& rogueHeader = memory.GetRogueHeader();
	size_t const blobSize = memory.GetMultiplayerStateBlobSize();

	struct Span
	{
		char const* m_Name;
		size_t m_Offset;
		size_t m_Size;
	};

	if (rogueHeader.rogueAssistantCompatVersion != rogue::multiplayer::RequiredRomAssistantApi)
	{
		LOG_ERROR("Multiplayer layout invalid: ROM Assistant API is not 3");
		return false;
	}
	if (!rogue::rom::IsSupportedEdition(rogueHeader.rogueVersion))
	{
		LOG_ERROR("Multiplayer layout invalid: ROM edition is neither Vanilla nor EX");
		return false;
	}
	if (rogueHeader.netPlayerCount == 0 || rogueHeader.netPlayerCount > 255)
	{
		LOG_ERROR("Multiplayer layout invalid: netPlayerCount must be from 1 through 255");
		return false;
	}
	if (rogueHeader.netHandshakeSize == 0 || rogueHeader.netGameStateSize == 0 ||
		rogueHeader.netPlayerProfileSize == 0 || rogueHeader.netPlayerStateSize == 0)
	{
		LOG_ERROR("Multiplayer layout invalid: one or more structure sizes are zero");
		return false;
	}
	if (blobSize == 0 || blobSize > MaxRomPayloadSize)
	{
		LOG_ERROR("Multiplayer layout invalid: state size must be from 1 through %zu bytes", MaxRomPayloadSize);
		return false;
	}

	std::size_t playerProfilesSize = 0;
	std::size_t playerStatesSize = 0;
	if (!CheckedProduct(rogueHeader.netPlayerProfileSize, rogueHeader.netPlayerCount, playerProfilesSize) ||
		!CheckedProduct(rogueHeader.netPlayerStateSize, rogueHeader.netPlayerCount, playerStatesSize))
	{
		LOG_ERROR("Multiplayer layout invalid: player table size overflow");
		return false;
	}

	Span const spans[] = {
		{"requestState", rogueHeader.netRequestStateOffset, 1},
		{"currentState", rogueHeader.netCurrentStateOffset, 1},
		{"handshake", rogueHeader.netHandshakeOffset, rogueHeader.netHandshakeSize},
		{"gameState", rogueHeader.netGameStateOffset, rogueHeader.netGameStateSize},
		{"playerProfile", rogueHeader.netPlayerProfileOffset, playerProfilesSize},
		{"playerState", rogueHeader.netPlayerStateOffset, playerStatesSize},
	};

	for (Span const& span : spans)
	{
		if (span.m_Offset > blobSize || span.m_Size > blobSize - span.m_Offset)
		{
			LOG_ERROR("Multiplayer layout invalid: %s spans %zu+%zu but blob is %zu bytes", span.m_Name, span.m_Offset,
					  span.m_Size, blobSize);
			return false;
		}
	}

	if (rogueHeader.netHandshakeStateOffset >= rogueHeader.netHandshakeSize ||
		rogueHeader.netHandshakePlayerIdOffset >= rogueHeader.netHandshakeSize)
	{
		LOG_ERROR("Multiplayer layout invalid: handshake fields lie outside the handshake block");
		return false;
	}

	return true;
}

// Player IDs arrive over the network. Zero identifies the host, so a client's
// ID must be nonzero and inside the player table.
static bool IsValidClientPlayerId(GameStructures::RogueAssistantHeader const& rogueHeader, u8 playerId)
{
	return playerId != 0 && playerId < rogueHeader.netPlayerCount;
}

MultiplayerBehaviour::MultiplayerBehaviour()
	: m_Port(c_DefaultPort), m_ConnState(ConnectionState::Default), m_HasAttemptedConnection(false), m_RequestFlags(0),
	  m_EnetInitialised(false), m_NetServer(nullptr), m_PlayerId(0), m_NetClient(nullptr), m_NetPeer(nullptr)
{
}

void MultiplayerBehaviour::OnAttach(GameConnection& game)
{
	m_ConnState = ConnectionState::Default;
	m_ConnectionAddressRaw.clear();
	m_HasAttemptedConnection = false;
	m_RequestFlags = 0;
	m_PlayerId = 0;
	m_PeerStates.clear();
	m_ServerState.m_PendingHandshake = nullptr;
	m_ServerState.m_PendingHandshakeData.clear();
	m_ServerState.m_PlayerProfiles.clear();
	m_ClientState.m_PendingHandshakeData.clear();
	m_ClientState.m_PendingPlayerProfiles.clear();

	GameStructures::RogueAssistantHeader const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();

	if (game.GetObservedGameMemory().IsMultiplayerStateValid() && ValidateMultiplayerLayout(game))
	{
		u8 const* multiplayerBlob = game.GetObservedGameMemory().GetMultiplayerStateBlob();

		u8 requestFlags = multiplayerBlob[rogueHeader.netRequestStateOffset];
		m_RequestFlags = requestFlags;
	}
}

void MultiplayerBehaviour::OnDetach(GameConnection& game)
{
	CloseConnection(game);
}

bool MultiplayerBehaviour::IsRequestingHostConnection() const
{
	return (m_RequestFlags & NetStateHost) != 0;
}

void MultiplayerBehaviour::ProvideConnectionAddress(std::string const& address)
{
	if (!m_HasAttemptedConnection)
		m_ConnectionAddressRaw = address;
}

std::string MultiplayerBehaviour::SanitiseConnectionAddress(std::string const& address)
{
	std::string outAddress;

	if (IsRequestingHostConnection())
	{
		// A host address contains only the port number.
		for (char c : address)
		{
			if (c >= '0' && c <= '9')
				outAddress += c;
		}
	}
	else
	{
		// Configuration validation checks the client address before using it.
		outAddress = address;
	}

	return outAddress;
}

rogue::multiplayer::Hello MultiplayerBehaviour::BuildCompatibilityHello(GameConnection const& game) const
{
	auto const& header = game.GetObservedGameMemory().GetRogueHeader();
	return {
		rogue::multiplayer::ProtocolMajor,
		rogue::multiplayer::ProtocolMinor,
		header.rogueAssistantCompatVersion,
		header.rogueVersion,
		header.netPlayerCount,
		header.netMultiplayerSize,
		header.netHandshakeSize,
		header.netGameStateSize,
		header.netPlayerProfileSize,
		header.netPlayerStateSize,
	};
}

void MultiplayerBehaviour::SendCompatibilityHello(GameConnection& game, ENetPeer* peer)
{
	std::vector<std::byte> encoded;
	std::string error;
	if (!rogue::multiplayer::EncodeHello(BuildCompatibilityHello(game), encoded, error) ||
		!SendPacket(peer, RogueNetChannel::Control, encoded.data(), encoded.size(), ENET_PACKET_FLAG_RELIABLE))
	{
		if (error.empty())
			error = "could not send the compatibility hello";
		RejectPeer(game, peer, error, "Could not start multiplayer.");
	}
}

void MultiplayerBehaviour::RejectPeer(GameConnection& game, ENetPeer* peer, std::string const& error,
									  std::string_view userMessage)
{
	LOG_ERROR("ENet: Multiplayer compatibility rejected: %s", error.c_str());
	game.ReportError(userMessage.empty()
						 ? "Rejected the multiplayer connection.\nThe other game sent invalid data."
						 : std::string(userMessage));

	if (m_ServerState.m_PendingHandshake == peer)
	{
		m_ServerState.m_PendingHandshake = nullptr;
		m_ServerState.m_PendingHandshakeData.clear();
	}
	m_PeerStates.erase(peer);
	if (peer != nullptr)
		enet_peer_disconnect_now(peer, 0);

	if (!IsHost())
		game.RemoveBehaviour(this);
}

void MultiplayerBehaviour::HandleCompatibilityHello(GameConnection& game, ENetEvent& netEvent)
{
	auto peerIt = m_PeerStates.find(netEvent.peer);
	if (peerIt == m_PeerStates.end())
	{
		RejectPeer(game, netEvent.peer, "hello arrived from an unknown peer");
		return;
	}
	if (peerIt->second.m_HelloReceived)
	{
		RejectPeer(game, netEvent.peer, "peer sent more than one compatibility hello");
		return;
	}

	rogue::multiplayer::Hello remoteHello;
	std::string error;
	auto const* bytes = reinterpret_cast<std::byte const*>(netEvent.packet->data);
	if (!rogue::multiplayer::DecodeHello(std::span<std::byte const>(bytes, netEvent.packet->dataLength), remoteHello,
										 error))
	{
		RejectPeer(game, netEvent.peer, error);
		return;
	}

	auto const compatibility = rogue::multiplayer::CheckCompatibility(BuildCompatibilityHello(game), remoteHello);
	if (!compatibility.compatible)
	{
		RejectPeer(game, netEvent.peer, compatibility.error, compatibility.error);
		return;
	}

	peerIt->second.m_HelloReceived = true;
	peerIt->second.m_Compatible = true;
	std::vector<u8> earlyHandshake = std::move(peerIt->second.m_EarlyHandshake);
	LOG_INFO("ENet: Accepted multiplayer protocol 1.%u peer", static_cast<unsigned>(compatibility.negotiatedMinor));
	if (!IsHost() && netEvent.peer == m_NetPeer && m_ConnState == ConnectionState::AwaitingCompatibility)
	{
		m_ConnState = ConnectionState::AwaitingHandshake;
	}
	if (!earlyHandshake.empty())
		HandleRomHandshake(game, netEvent.peer, earlyHandshake);
}

void MultiplayerBehaviour::HandlePeerDisconnect(GameConnection& game, ENetPeer* peer)
{
	bool const knownPeer = m_PeerStates.erase(peer) != 0;
	if (m_ServerState.m_PendingHandshake == peer)
	{
		m_ServerState.m_PendingHandshake = nullptr;
		m_ServerState.m_PendingHandshakeData.clear();
	}
	if (!knownPeer)
		return;
	if (IsHost())
	{
		// A host can serve another peer after one client leaves. This also keeps
		// the remaining peers alive when the ROM supports more than two players.
		return;
	}

	game.ReportError("The other player disconnected.");
	game.RemoveBehaviour(this);
}

void MultiplayerBehaviour::BroadcastToConnectedPeers(enet_uint8 channel, std::span<u8 const> data, enet_uint32 flags)
{
	for (auto const& [peer, state] : m_PeerStates)
	{
		if (state.m_Compatible && state.m_RomConnected)
			(void)SendPacket(peer, channel, data.data(), data.size(), flags);
	}
}

void MultiplayerBehaviour::OnUpdate(GameConnection& game)
{
	GameStructures::RogueAssistantHeader const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();

	if (!game.GetObservedGameMemory().IsMultiplayerStateValid())
		return;

	// Validate before accepting a host or client address. Opening ENet uses ROM-
	// supplied sizes and counts, so check them before the connection starts.
	if (!ValidateMultiplayerLayout(game))
	{
		game.ReportError("Cannot start multiplayer.\nThe ROM multiplayer data is invalid.");
		game.RemoveBehaviour(this);
		return;
	}

	if (!m_HasAttemptedConnection)
	{
		bool const hasAddress = !m_ConnectionAddressRaw.empty();
		if (hasAddress)
			m_HasAttemptedConnection = true;

		if (hasAddress)
		{
			if (IsRequestingHostConnection())
			{
				if (!ParsePort(m_ConnectionAddressRaw, m_Port))
				{
					game.ReportError("Enter a port from 1 to 65535.");
					game.RemoveBehaviour(this);
				}
				else
				{
					OpenHostConnection(game);
				}
			}
			else
			{
				OpenClientConnection(game);
			}
		}
		return;
	}

	// Revalidate the layout after the game changes netMultiplayerSize.
	u8 const* multiplayerBlob = game.GetObservedGameMemory().GetMultiplayerStateBlob();

	u8 requestFlags = multiplayerBlob[rogueHeader.netRequestStateOffset];

	if (m_RequestFlags != requestFlags)
	{
		// Restart multiplayer because the ROM request changed.
		game.RemoveBehaviour(this);
		return;
	}

	// Poll ENet while the ROM prepares a handshake response so disconnects and
	// waiting compatibility messages are still handled.
	if (!m_ClientState.m_PendingHandshakeData.empty())
		(void)TrySubmitClientHandshake(game);
	if (!m_ClientState.m_PendingPlayerProfiles.empty())
		(void)TrySubmitClientPlayerProfiles(game);
	bool const hadPendingHandshakeData = !m_ServerState.m_PendingHandshakeData.empty();
	if (hadPendingHandshakeData)
		(void)TrySubmitHostHandshake(game);
	ENetPeer* const pendingBeforePoll = m_ServerState.m_PendingHandshake;
	PollConnection(game);

	// A handshake received by PollConnection cannot use ROM state from the current
	// update. Wait for the next game-memory read.
	if (!hadPendingHandshakeData && pendingBeforePoll != nullptr &&
		m_ServerState.m_PendingHandshake == pendingBeforePoll && m_ServerState.m_PendingHandshakeData.empty())
	{
		ASSERT_MSG(IsHost(), "Only a host can process client handshakes");

		u8 const handshakeState = multiplayerBlob[rogueHeader.netHandshakeOffset + rogueHeader.netHandshakeStateOffset];
		if (handshakeState == HandshakeStateSendToClient)
		{
			ENetPeer* const peer = m_ServerState.m_PendingHandshake;
			u8 const playerId =
				multiplayerBlob[rogueHeader.netHandshakeOffset + rogueHeader.netHandshakePlayerIdOffset];

			if (!IsValidClientPlayerId(rogueHeader, playerId))
			{
				m_ServerState.m_PendingHandshake = nullptr;
				RejectPeer(game, peer, "ROM assigned an out-of-range multiplayer player ID");
				return;
			}

			m_ServerState.m_PendingHandshake = nullptr;
			auto peerIt = m_PeerStates.find(peer);
			if (peerIt == m_PeerStates.end() || !peerIt->second.m_Compatible)
				return;
			peerIt->second.m_PlayerId = playerId;
			peerIt->second.m_RomConnected = true;
			if (!SendPacket(peer, RogueNetChannel::Handshake, &multiplayerBlob[rogueHeader.netHandshakeOffset],
							rogueHeader.netHandshakeSize, ENET_PACKET_FLAG_RELIABLE))
			{
				RejectPeer(game, peer, "could not send the ROM multiplayer handshake response",
						   "Could not connect to the other player.");
				return;
			}

			// Force the next player-profile broadcast to every client.
			m_ServerState.m_PlayerProfiles.clear();
			return;
		}
	}

	// Advance the ROM handshake.
	switch (m_ConnState)
	{
	case MultiplayerBehaviour::ConnectionState::Connecting:
	case MultiplayerBehaviour::ConnectionState::AwaitingCompatibility:
		if (std::chrono::steady_clock::now() >= m_ConnectDeadline)
		{
			LOG_ERROR("ENet: Timed out connecting or negotiating compatibility.");
			game.ReportError("The multiplayer connection timed out.");
			game.RemoveBehaviour(this);
		}
		break;

	case MultiplayerBehaviour::ConnectionState::AwaitingHandshake: {
		u8 handshakeState = multiplayerBlob[rogueHeader.netHandshakeOffset + rogueHeader.netHandshakeStateOffset];
		if (handshakeState == HandshakeStateSendToHost)
		{
			if (SendPacket(m_NetPeer, RogueNetChannel::Handshake, &multiplayerBlob[rogueHeader.netHandshakeOffset],
						   rogueHeader.netHandshakeSize, ENET_PACKET_FLAG_RELIABLE))
			{
				m_ConnState = ConnectionState::AwaitingResponse;
			}
			else
			{
				game.ReportError("Could not start multiplayer.");
				game.RemoveBehaviour(this);
			}
		}
	}
	break;

	case MultiplayerBehaviour::ConnectionState::AwaitingResponse:
		break;

	case MultiplayerBehaviour::ConnectionState::ConnectionConfirmed:
		if (SendMultiplayerConfirmationToGame(game))
			m_ConnState = ConnectionState::Connected;
		break;

	case MultiplayerBehaviour::ConnectionState::Connected:
		ConnectedUpdate(game);
		break;
	}
}

void MultiplayerBehaviour::OpenHostConnection(GameConnection& game)
{
	LOG_INFO("ENet: Opening host");

	if (enet_initialize() != 0)
	{
		LOG_ERROR("ENet: Failed to initialize");
		game.ReportError("Could not start multiplayer.");
		game.RemoveBehaviour(this);
		return;
	}
	m_EnetInitialised = true;

	GameStructures::RogueAssistantHeader const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();

	ENetAddress address;
	address.host = ENET_HOST_ANY;
	address.port = m_Port;

	ENetHost* netServer = enet_host_create(&address,
										   rogueHeader.netPlayerCount - 1, // client count
										   RogueNetChannel::Count,		   // channel count
										   0,							   // assumed incoming bandwidth
										   0							   // assumed outgoing bandwidth
	);
	m_NetServer = netServer;

	if (netServer == nullptr)
	{
		LOG_ERROR("ENet: Failed to create host");
		game.ReportError("Cannot use the selected multiplayer port.");
		game.RemoveBehaviour(this);
		return;
	}

	m_ConnState = ConnectionState::ConnectionConfirmed;
}

static void SetPeerTimeouts(ENetPeer* netPeer)
{
	u32 timeoutLimit = ENET_PEER_TIMEOUT_LIMIT;
	u32 timeoutMinimum = ENET_PEER_TIMEOUT_MINIMUM;
	u32 timeoutMaximum = ENET_PEER_TIMEOUT_MAXIMUM;

#if _DEBUG
	timeoutLimit = static_cast<u32>(-1);
	timeoutMinimum = static_cast<u32>(-1);
	timeoutMaximum = static_cast<u32>(-1);
#endif

	LOG_INFO("ENet: Setting peer timeouts: %u, %u, %u", timeoutLimit, timeoutMinimum, timeoutMaximum);
	enet_peer_timeout(netPeer, timeoutLimit, timeoutMinimum, timeoutMaximum);
}

void MultiplayerBehaviour::OpenClientConnection(GameConnection& game)
{
	LOG_INFO("ENet: Opening client");

	if (enet_initialize() != 0)
	{
		LOG_ERROR("ENet: Failed to initialize");
		game.ReportError("Could not start multiplayer.");
		game.RemoveBehaviour(this);
		return;
	}
	m_EnetInitialised = true;

	m_NetClient = enet_host_create(nullptr, // null address to indicate this host is for client connection
								   1,		// client count
								   RogueNetChannel::Count, // channel count
								   0,					   // assumed incoming bandwidth
								   0					   // assumed outgoing bandwidth
	);

	if (m_NetClient == nullptr)
	{
		LOG_ERROR("ENet: Failed to create client");
		game.ReportError("Could not start multiplayer.");
		game.RemoveBehaviour(this);
		return;
	}

	// Resolve the host address.
	strutil::trim(m_ConnectionAddressRaw);

	std::size_t const separator = m_ConnectionAddressRaw.rfind(':');
	if (separator != std::string::npos && separator != 0 && separator + 1 < m_ConnectionAddressRaw.size())
	{
		u16 desiredPort = 0;
		std::string_view const rawPort(m_ConnectionAddressRaw.data() + separator + 1,
									   m_ConnectionAddressRaw.size() - separator - 1);
		if (ParsePort(rawPort, desiredPort))
		{
			m_ConnectionAddressRaw.resize(separator);
			m_Port = desiredPort;
		}
		else
		{
			game.ReportError("Enter a port from 1 to 65535.");
			game.RemoveBehaviour(this);
			return;
		}
	}
	else
	{
		m_Port = c_DefaultPort;
	}

	ENetAddress address;
	if (m_ConnectionAddressRaw.empty() || enet_address_set_host(&address, m_ConnectionAddressRaw.c_str()) != 0)
	{
		game.ReportError("Cannot find the multiplayer host.");
		game.RemoveBehaviour(this);
		return;
	}
	address.port = m_Port;

	m_NetPeer = enet_host_connect(m_NetClient, &address, RogueNetChannel::Count, 0);

	if (m_NetPeer == nullptr)
	{
		LOG_ERROR("ENet: Failed to create client peer");
		game.ReportError("Could not connect to the multiplayer host.");
		game.RemoveBehaviour(this);
		return;
	}

	m_ConnectDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	m_ConnState = ConnectionState::Connecting;
}

void MultiplayerBehaviour::CloseConnection(GameConnection&)
{
	if (ENetHost* netServer = m_NetServer)
	{
		LOG_INFO("ENet: Closing Host");

		m_NetServer = nullptr;
		enet_host_destroy(netServer);
	}

	if (m_NetClient != nullptr)
	{
		LOG_INFO("ENet: Closing Client");

		if (m_NetPeer != nullptr)
			enet_peer_reset(m_NetPeer);

		enet_host_destroy(m_NetClient);

		m_NetClient = nullptr;
		m_NetPeer = nullptr;
	}

	m_ServerState.m_PendingHandshake = nullptr;
	m_ServerState.m_PendingHandshakeData.clear();
	m_ClientState.m_PendingHandshakeData.clear();
	m_ClientState.m_PendingPlayerProfiles.clear();
	m_PeerStates.clear();
	if (m_EnetInitialised)
	{
		enet_deinitialize();
		m_EnetInitialised = false;
	}
}

void MultiplayerBehaviour::PollConnection(GameConnection& game)
{
	ENetHost* netServer = m_NetServer;
	ENetHost* conn = netServer != nullptr ? netServer : m_NetClient;

	if (conn != nullptr)
	{
		ENetEvent netEvent;
		int serviceResult = 0;
		while ((serviceResult = enet_host_service(conn, &netEvent, 0)) > 0)
		{
			switch (netEvent.type)
			{
			case ENET_EVENT_TYPE_CONNECT:
				LOG_INFO("ENet: Connected %x:%u", netEvent.peer->address.host, netEvent.peer->address.port);
				SetPeerTimeouts(netEvent.peer);
				m_PeerStates[netEvent.peer] = PeerState{
					false, false, false, 0, std::chrono::steady_clock::now() + CompatibilityTimeout, {},
				};
				if (m_NetClient != nullptr && m_ConnState == ConnectionState::Connecting)
					m_ConnState = ConnectionState::AwaitingCompatibility;
				SendCompatibilityHello(game, netEvent.peer);
				break;

			case ENET_EVENT_TYPE_RECEIVE:
				HandleIncomingMessage(game, netEvent);
				break;

			case ENET_EVENT_TYPE_DISCONNECT:
				LOG_INFO("ENet: Disconnected %x:%u", netEvent.peer->address.host, netEvent.peer->address.port);
				HandlePeerDisconnect(game, netEvent.peer);
				break;

			default:
				LOG_ERROR("ENet: Unrecognized event type %d", static_cast<int>(netEvent.type));
				break;
			}
		}
		if (serviceResult < 0)
			LOG_ERROR("ENet: Failed while polling multiplayer events");

		std::vector<ENetPeer*> expiredPeers;
		auto const now = std::chrono::steady_clock::now();
		for (auto const& [peer, state] : m_PeerStates)
		{
			if (!state.m_Compatible && now >= state.m_CompatibilityDeadline)
				expiredPeers.push_back(peer);
		}
		for (ENetPeer* peer : expiredPeers)
			RejectPeer(game, peer, "timed out waiting for the compatibility hello",
					   "The multiplayer connection timed out.");
	}
}

static bool UpdateBinaryBlob(std::vector<u8>& copy, u8 const* rawBuffer, size_t rawSize)
{
	bool hasChanged = false;

	if (copy.size() != rawSize)
	{
		hasChanged = true;
	}
	else
	{
		if (memcmp(copy.data(), rawBuffer, rawSize) != 0)
		{
			hasChanged = true;
		}
	}

	if (hasChanged)
	{
		copy.resize(rawSize);
		if (rawSize != 0)
			std::memcpy(copy.data(), rawBuffer, rawSize);
	}

	return hasChanged;
}

void MultiplayerBehaviour::ConnectedUpdate(GameConnection& game)
{
	GameStructures::RogueAssistantHeader const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();
	u8 const* multiplayerBlob = game.GetObservedGameMemory().GetMultiplayerStateBlob();

	if (IsHost())
	{
		std::size_t const playerProfilesSize =
			static_cast<std::size_t>(rogueHeader.netPlayerProfileSize) * rogueHeader.netPlayerCount;
		std::size_t const playerStatesSize =
			static_cast<std::size_t>(rogueHeader.netPlayerStateSize) * rogueHeader.netPlayerCount;

		// Broadcast changed player profiles.
		if (UpdateBinaryBlob(m_ServerState.m_PlayerProfiles, &multiplayerBlob[rogueHeader.netPlayerProfileOffset],
							 playerProfilesSize))
		{
			BroadcastToConnectedPeers(RogueNetChannel::PlayerProfiles, m_ServerState.m_PlayerProfiles,
									  ENET_PACKET_FLAG_RELIABLE);
		}

		// Broadcast the game state at its configured interval.
		if (m_ServerState.m_GameStateTimer.Update())
		{
			BroadcastToConnectedPeers(
				RogueNetChannel::GameState,
				std::span<u8 const>(&multiplayerBlob[rogueHeader.netGameStateOffset], rogueHeader.netGameStateSize),
				ENET_PACKET_FLAG_RELIABLE);
		}

		// Broadcast all player states at their configured interval.
		if (m_ServerState.m_PlayerStateTimer.Update())
		{
			BroadcastToConnectedPeers(
				RogueNetChannel::PlayerState,
				std::span<u8 const>(&multiplayerBlob[rogueHeader.netPlayerStateOffset], playerStatesSize),
				ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
		}
	}
	else
	{
		// Send the local player state at its configured interval.
		if (m_ClientState.m_PlayerStateTimer.Update())
		{
			if (!IsValidClientPlayerId(rogueHeader, m_PlayerId))
			{
				LOG_ERROR("Refusing to send player state for out-of-range player ID %u", (unsigned)m_PlayerId);
				return;
			}

			(void)SendPacket(m_NetPeer, RogueNetChannel::PlayerState,
							 &multiplayerBlob[rogueHeader.netPlayerStateOffset +
											  static_cast<std::size_t>(rogueHeader.netPlayerStateSize) * m_PlayerId],
							 rogueHeader.netPlayerStateSize, ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT);
		}
	}
}

void MultiplayerBehaviour::HandleRomHandshake(GameConnection& game, ENetPeer* peer, std::span<u8 const> data)
{
	auto const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();
	if (data.size() != rogueHeader.netHandshakeSize)
	{
		RejectPeer(game, peer, "ROM multiplayer handshake size differs");
		return;
	}

	auto peerIt = m_PeerStates.find(peer);
	if (peerIt == m_PeerStates.end() || !peerIt->second.m_Compatible)
	{
		RejectPeer(game, peer, "ROM handshake arrived before compatibility was established");
		return;
	}

	if (IsHost())
	{
		if (peerIt->second.m_RomConnected)
		{
			RejectPeer(game, peer, "peer repeated the ROM multiplayer handshake");
			return;
		}
		if (m_ServerState.m_PendingHandshake != nullptr)
		{
			RejectPeer(game, peer, "host is already processing another player handshake");
			return;
		}
		m_ServerState.m_PendingHandshake = peer;
		m_ServerState.m_PendingHandshakeData.assign(data.begin(), data.end());
		(void)TrySubmitHostHandshake(game);
		return;
	}

	if (peer != m_NetPeer || m_ConnState != ConnectionState::AwaitingResponse)
	{
		RejectPeer(game, peer, "host sent an unexpected ROM multiplayer handshake");
		return;
	}
	if (!m_ClientState.m_PendingHandshakeData.empty())
	{
		RejectPeer(game, peer, "host repeated the ROM multiplayer handshake response");
		return;
	}
	u8 const playerId = data[rogueHeader.netHandshakePlayerIdOffset];
	if (!IsValidClientPlayerId(rogueHeader, playerId))
	{
		RejectPeer(game, peer, "host assigned an out-of-range multiplayer player ID");
		return;
	}

	m_ClientState.m_PendingHandshakeData.assign(data.begin(), data.end());
	(void)TrySubmitClientHandshake(game);
}

bool MultiplayerBehaviour::TrySubmitHostHandshake(GameConnection& game)
{
	if (m_ServerState.m_PendingHandshake == nullptr || m_ServerState.m_PendingHandshakeData.empty())
		return false;

	auto const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();
	GameAddress const multiplayerAddress = game.GetObservedGameMemory().GetMultiplayerStatePtr();
	if (!game.WriteRequest(CreateAnonymousMessageId(), multiplayerAddress + rogueHeader.netHandshakeOffset,
						   m_ServerState.m_PendingHandshakeData.data(), m_ServerState.m_PendingHandshakeData.size()))
	{
		return false;
	}

	m_ServerState.m_PendingHandshakeData.clear();
	return true;
}

bool MultiplayerBehaviour::TrySubmitClientHandshake(GameConnection& game)
{
	if (m_NetPeer == nullptr || m_ClientState.m_PendingHandshakeData.empty())
		return false;

	auto peerIt = m_PeerStates.find(m_NetPeer);
	if (peerIt == m_PeerStates.end() || !peerIt->second.m_Compatible)
		return false;

	auto const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();
	GameAddress const multiplayerAddress = game.GetObservedGameMemory().GetMultiplayerStatePtr();
	if (!game.WriteRequest(CreateAnonymousMessageId(), multiplayerAddress + rogueHeader.netHandshakeOffset,
						   m_ClientState.m_PendingHandshakeData.data(), m_ClientState.m_PendingHandshakeData.size()))
	{
		return false;
	}

	u8 const playerId = m_ClientState.m_PendingHandshakeData[rogueHeader.netHandshakePlayerIdOffset];
	m_ClientState.m_PendingHandshakeData.clear();
	m_PlayerId = playerId;
	peerIt->second.m_PlayerId = playerId;
	peerIt->second.m_RomConnected = true;
	m_ConnState = ConnectionState::ConnectionConfirmed;
	return true;
}

bool MultiplayerBehaviour::TrySubmitClientPlayerProfiles(GameConnection& game)
{
	if (m_ClientState.m_PendingPlayerProfiles.empty())
		return false;

	auto const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();
	GameAddress const multiplayerAddress = game.GetObservedGameMemory().GetMultiplayerStatePtr();
	if (!game.WriteRequest(CreateAnonymousMessageId(), multiplayerAddress + rogueHeader.netPlayerProfileOffset,
						   m_ClientState.m_PendingPlayerProfiles.data(),
						   m_ClientState.m_PendingPlayerProfiles.size()))
	{
		return false;
	}

	m_ClientState.m_PendingPlayerProfiles.clear();
	return true;
}

void MultiplayerBehaviour::HandleIncomingMessage(GameConnection& game, ENetEvent& netEvent)
{
	PacketPtr packet(netEvent.packet);
	if (packet == nullptr)
		return;

	auto const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();
	ASSERT_MSG(game.GetObservedGameMemory().IsMultiplayerStateValid(), "Multiplayer state invalid");

	if (netEvent.channelID == RogueNetChannel::Control)
	{
		HandleCompatibilityHello(game, netEvent);
		return;
	}

	auto peerIt = m_PeerStates.find(netEvent.peer);
	if (peerIt == m_PeerStates.end())
	{
		RejectPeer(game, netEvent.peer, "data arrived from an unknown peer");
		return;
	}
	if (!peerIt->second.m_Compatible)
	{
		if (netEvent.channelID == RogueNetChannel::Handshake && peerIt->second.m_EarlyHandshake.empty() &&
			packet->dataLength == rogueHeader.netHandshakeSize)
		{
			peerIt->second.m_EarlyHandshake.assign(packet->data, packet->data + packet->dataLength);
			return;
		}
		RejectPeer(game, netEvent.peer, "ROM data arrived before the compatibility hello");
		return;
	}
	if (netEvent.channelID != RogueNetChannel::Handshake && !peerIt->second.m_RomConnected)
	{
		RejectPeer(game, netEvent.peer, "gameplay data arrived before the ROM handshake");
		return;
	}

	GameAddress const multiplayerAddress = game.GetObservedGameMemory().GetMultiplayerStatePtr();
	switch (netEvent.channelID)
	{
	case RogueNetChannel::Handshake:
		HandleRomHandshake(game, netEvent.peer, std::span<u8 const>(packet->data, packet->dataLength));
		break;

	case RogueNetChannel::PlayerProfiles: {
		std::size_t const expectedSize =
			static_cast<std::size_t>(rogueHeader.netPlayerProfileSize) * rogueHeader.netPlayerCount;
		if (IsHost())
			RejectPeer(game, netEvent.peer, "client attempted to send player profiles");
		else if (packet->dataLength != expectedSize)
			RejectPeer(game, netEvent.peer, "player-profile payload size differs");
		else
		{
			// Each message contains all profiles, and the host only sends it after a
			// change. Keep the latest copy until the bridge has room to accept it.
			// A newer copy can replace an older one that is still waiting.
			m_ClientState.m_PendingPlayerProfiles.assign(packet->data, packet->data + packet->dataLength);
			(void)TrySubmitClientPlayerProfiles(game);
		}
		break;
	}

	case RogueNetChannel::GameState:
		if (IsHost())
			RejectPeer(game, netEvent.peer, "client attempted to send host game state");
		else if (packet->dataLength != rogueHeader.netGameStateSize)
			RejectPeer(game, netEvent.peer, "game-state payload size differs");
		else
			game.WriteRequest(CreateAnonymousMessageId(), multiplayerAddress + rogueHeader.netGameStateOffset,
							  packet->data, packet->dataLength);
		break;

	case RogueNetChannel::PlayerState:
		if (IsHost())
		{
			u8 const playerId = peerIt->second.m_PlayerId;
			if (packet->dataLength != rogueHeader.netPlayerStateSize)
				RejectPeer(game, netEvent.peer, "client player-state payload size differs");
			else if (!IsValidClientPlayerId(rogueHeader, playerId))
				RejectPeer(game, netEvent.peer, "client has an invalid multiplayer player ID");
			else
				game.WriteRequest(CreateAnonymousMessageId(),
					multiplayerAddress + rogueHeader.netPlayerStateOffset +
									  static_cast<GameAddress>(
										  static_cast<std::size_t>(rogueHeader.netPlayerStateSize) * playerId),
					packet->data, rogueHeader.netPlayerStateSize);
		}
		else
		{
			std::size_t const expectedSize =
				static_cast<std::size_t>(rogueHeader.netPlayerStateSize) * rogueHeader.netPlayerCount;
			if (packet->dataLength != expectedSize)
			{
				RejectPeer(game, netEvent.peer, "host player-state payload size differs");
				break;
			}
			for (u8 playerId = 0; playerId < rogueHeader.netPlayerCount; ++playerId)
			{
				if (playerId == m_PlayerId)
					continue;
				GameAddress const playerOffset = static_cast<GameAddress>(
					static_cast<std::size_t>(rogueHeader.netPlayerStateSize) * playerId);
				game.WriteRequest(CreateAnonymousMessageId(),
								  multiplayerAddress + rogueHeader.netPlayerStateOffset + playerOffset,
								  packet->data + playerOffset, rogueHeader.netPlayerStateSize);
			}
		}
		break;

	default:
		RejectPeer(game, netEvent.peer, "peer used an unknown multiplayer channel");
		break;
	}
}

bool MultiplayerBehaviour::SendMultiplayerConfirmationToGame(GameConnection& game)
{
	GameStructures::RogueAssistantHeader const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();
	GameAddress multiplayerAddress = game.GetObservedGameMemory().GetMultiplayerStatePtr();

	u8 const requestFlags = m_RequestFlags;
	return game.WriteRequest(CreateAnonymousMessageId(), multiplayerAddress + rogueHeader.netCurrentStateOffset,
							 &requestFlags, sizeof(requestFlags));
}
