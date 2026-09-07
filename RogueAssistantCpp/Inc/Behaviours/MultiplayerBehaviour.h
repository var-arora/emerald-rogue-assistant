#pragma once
#include "Defines.h"
#include "GameConnectionBehaviour.h"
#include "Multiplayer/MultiplayerProtocol.h"
#include "Timer.h"
#include "enet/enet.h"

#include <chrono>
#include <queue>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class MultiplayerBehaviour : public IGameConnectionBehaviour
{
  public:
	static u16 const c_DefaultPort;

	MultiplayerBehaviour();

	virtual void OnAttach(GameConnection& game) override;
	virtual void OnDetach(GameConnection& game) override;
	virtual void OnUpdate(GameConnection& game) override;

	bool IsRequestingHostConnection() const;
	inline bool IsHost() const
	{
		return m_NetServer != nullptr;
	}
	inline u16 GetPort() const
	{
		return m_Port;
	}

	bool IsAwaitingAddress() const
	{
		return !m_HasAttemptedConnection;
	}
	bool IsConnected() const
	{
		return m_ConnState >= ConnectionState::Connected;
	}
	std::string SanitiseConnectionAddress(std::string const& address);
	void ProvideConnectionAddress(std::string const& address);

  private:
	enum class ConnectionState
	{
		Connecting,
		AwaitingCompatibility,
		AwaitingHandshake,
		AwaitingResponse,
		ConnectionConfirmed,
		Connected,

		Default = Connecting
	};

	struct PeerState
	{
		bool m_HelloReceived = false;
		bool m_Compatible = false;
		bool m_RomConnected = false;
		u8 m_PlayerId = 0;
		std::chrono::steady_clock::time_point m_CompatibilityDeadline{};
		std::vector<u8> m_EarlyHandshake;
	};

	struct ServerState
	{
		std::vector<u8> m_PlayerProfiles;
		ENetPeer* m_PendingHandshake;
		std::vector<u8> m_PendingHandshakeData;
		UpdateTimer m_GameStateTimer;
		UpdateTimer m_PlayerStateTimer;

		ServerState()
			: m_PendingHandshake(nullptr), m_GameStateTimer(UpdateTimer::c_5UPS),
			  m_PlayerStateTimer(UpdateTimer::c_30UPS)
		{
		}
	};

	struct ClientState
	{
		std::vector<u8> m_PendingHandshakeData;
		std::vector<u8> m_PendingPlayerProfiles;
		UpdateTimer m_PlayerStateTimer;

		ClientState() : m_PlayerStateTimer(UpdateTimer::c_30UPS)
		{
		}
	};

	void OpenHostConnection(GameConnection& game);
	void OpenClientConnection(GameConnection& game);
	void CloseConnection(GameConnection& game);

	void PollConnection(GameConnection& game);
	void ConnectedUpdate(GameConnection& game);
	void HandleIncomingMessage(GameConnection& game, ENetEvent& netEvent);
	void HandleCompatibilityHello(GameConnection& game, ENetEvent& netEvent);
	void HandleRomHandshake(GameConnection& game, ENetPeer* peer, std::span<u8 const> data);
	void HandlePeerDisconnect(GameConnection& game, ENetPeer* peer);
	void RejectPeer(GameConnection& game, ENetPeer* peer, std::string const& error, std::string_view userMessage = {});
	bool TrySubmitHostHandshake(GameConnection& game);
	bool TrySubmitClientHandshake(GameConnection& game);
	bool TrySubmitClientPlayerProfiles(GameConnection& game);
	void SendCompatibilityHello(GameConnection& game, ENetPeer* peer);
	void BroadcastToConnectedPeers(enet_uint8 channel, std::span<u8 const> data, enet_uint32 flags);
	[[nodiscard]] rogue::multiplayer::Hello BuildCompatibilityHello(GameConnection const& game) const;

	[[nodiscard]] bool SendMultiplayerConfirmationToGame(GameConnection& game);

	// SessionWorker owns all behavior state. The UI receives copies of this
	// state and sends commands; it does not access this object.
	u16 m_Port;
	ConnectionState m_ConnState;
	std::string m_ConnectionAddressRaw;

	bool m_HasAttemptedConnection;
	u8 m_RequestFlags;
	bool m_EnetInitialised;
	ENetHost* m_NetServer;

	u8 m_PlayerId;
	ENetHost* m_NetClient;
	ENetPeer* m_NetPeer;
	std::chrono::steady_clock::time_point m_ConnectDeadline;
	std::unordered_map<ENetPeer*, PeerState> m_PeerStates;

	ServerState m_ServerState;
	ClientState m_ClientState;
};
