#include "GameConnectionManager.h"

#include "Application/CommandLine.h"
#include "Behaviours/HomeBoxBehaviour.h"
#include "Behaviours/MultiplayerBehaviour.h"
#include "Bridge/TcpLuaTransport.h"
#include "GameConnection.h"
#include "Log.h"
#include "Platform/BridgeScript.h"
#include "Platform/Configuration.h"
#include "Platform/ResourceLocator.h"
#include "Platform/Utf8.h"
#include "RogueAssistantVersion.h"
#include "UserData.h"

#include <algorithm>
#include <stdexcept>
#include <string_view>
#include <utility>

GameConnectionManager::GameConnectionManager(std::shared_ptr<IGameMemoryTransport> transport)
	: m_Transport(std::move(transport))
{
	if (!m_Transport)
		throw std::invalid_argument("GameConnectionManager requires a memory transport");
}

GameConnectionManager::GameConnectionManager(std::optional<std::uint16_t> bridgePortOverride)
	: m_BridgePortOverride(bridgePortOverride), m_UsesPortableBridge(true)
{
}

GameConnectionManager::~GameConnectionManager()
{
	Stop();
}

void GameConnectionManager::Start()
{
	if (m_Started)
		return;
	if (!UserData::Init())
	{
		UserData::Shutdown();
		throw std::runtime_error("Cannot open the app data folder.");
	}

	m_Started = true;
	LOG_INFO("Emerald Rogue Assistant %s", ROGUE_ASSISTANT_VERSION_STRING);
	if (m_UsesPortableBridge)
	{
		int const savedPort =
			UserData::GetSavedInt(std::string(rogue::platform::BridgePortKey), static_cast<int>(m_BridgePort));
		m_BridgePort = rogue::app::SelectBridgePort(m_BridgePortOverride, static_cast<std::uint16_t>(savedPort));
		try
		{
			m_Transport = std::make_shared<TcpLuaTransport>(m_BridgePort);
		}
		catch (std::exception const& exception)
		{
			LOG_ERROR("Cannot start mGBA listener: %s", exception.what());
			throw std::runtime_error(
				"Cannot listen on port " + std::to_string(m_BridgePort) + ".");
		}
		ExportPortableScript();
	}
	m_ListeningForConnections = true;
	LOG_INFO("Game: Opening connection listener");
}

void GameConnectionManager::HandleCommand(rogue::app::UiCommand command)
{
	if (command.type == rogue::app::UiCommand::Type::SetBridgePort)
	{
		ChangeBridgePort(command.value);
		return;
	}
	if (command.type == rogue::app::UiCommand::Type::ExportBridgeScript)
	{
		ExportPortableScript();
		return;
	}
	if (command.type != rogue::app::UiCommand::Type::ProvideMultiplayerAddress)
		return;

	auto const connection =
		std::find_if(m_ActiveConnections.begin(), m_ActiveConnections.end(),
					 [&command](ActiveGameConnection const& active) { return active.id == command.connectionId; });
	if (connection == m_ActiveConnections.end())
		return;

	auto multiplayer = connection->game->FindBehaviour<MultiplayerBehaviour>();
	if (!multiplayer || !multiplayer->IsAwaitingAddress())
		return;

	std::string const address = multiplayer->SanitiseConnectionAddress(command.value);
	if (address.empty())
		return;
	rogue::platform::Settings validated;
	std::string validationError;
	std::string_view const settingKey = multiplayer->IsRequestingHostConnection()
											? rogue::platform::MultiplayerHostPortKey
											: rogue::platform::MultiplayerJoinIpKey;
	if (!rogue::platform::TrySetSetting(validated, settingKey, address, validationError))
	{
		LOG_WARN("Invalid multiplayer address: %s", validationError.c_str());
		PushError(multiplayer->IsRequestingHostConnection() ? "Enter a port from 1 to 65535."
															: "Enter a valid multiplayer host address.");
		return;
	}
	m_RecentError.clear();
	multiplayer->ProvideConnectionAddress(address);
	UserData::SetSavedString(std::string(settingKey), address);
}

void GameConnectionManager::Tick()
{
	if (!m_Started)
		return;
	UpdateConnections();
	UserData::Update();
}

rogue::app::UiSnapshot GameConnectionManager::Snapshot() const
{
	rogue::app::UiSnapshot snapshot;
	snapshot.transportState = m_Transport ? m_Transport->State() : TransportState::Disconnected;
	snapshot.error = m_RecentError;
	snapshot.bridgePort = m_BridgePort;
	snapshot.bridgeScriptPath = m_BridgeScriptPath;
	snapshot.bridgeMessage = m_BridgeMessage;
	if (m_Started)
	{
		snapshot.multiplayerHostPort =
			UserData::GetSavedString("Multiplayer.HostPort", std::to_string(MultiplayerBehaviour::c_DefaultPort));
		snapshot.multiplayerJoinAddress = UserData::GetSavedString("Multiplayer.JoinIP");
	}

	snapshot.connections.reserve(m_ActiveConnections.size());
	for (ActiveGameConnection const& active : m_ActiveConnections)
	{
		rogue::app::ConnectionSnapshot connection;
		connection.id = active.id;

		if (auto homeBox = active.game->FindBehaviour<HomeBoxBehaviour>())
		{
			connection.page = rogue::app::UiPage::HomeBox;
			connection.homeBox.loading = homeBox->IsLoading();
			connection.homeBox.saving = homeBox->IsSaving();
			connection.homeBox.requiresReopen = homeBox->RequiresReopen();
		}
		else if (auto multiplayer = active.game->FindBehaviour<MultiplayerBehaviour>())
		{
			connection.page = rogue::app::UiPage::Multiplayer;
			connection.multiplayer.requestingHost = multiplayer->IsRequestingHostConnection();
			connection.multiplayer.awaitingAddress = multiplayer->IsAwaitingAddress();
			connection.multiplayer.connected = multiplayer->IsConnected();
			connection.multiplayer.port = multiplayer->GetPort();
		}

		snapshot.connections.push_back(connection);
	}
	return snapshot;
}

void GameConnectionManager::Stop()
{
	if (m_Transport)
		m_Transport->Stop();
	m_ListeningForConnections = false;
	DisconnectConnections();

	if (m_Started)
	{
		LOG_INFO("Game: Closing connection listener");
		UserData::Shutdown();
		m_Started = false;
	}
}

void GameConnectionManager::ExportPortableScript()
{
	if (!m_UsesPortableBridge || !m_Started)
		return;
	rogue::platform::ResourceLocator const resources(UserData::GetResourceDirectory());
	auto const exported = rogue::platform::ExportBridgeScript(
		resources.Resolve(rogue::platform::Resource::BridgeScript), UserData::GetScriptDirectory(), m_BridgePort);
	if (!exported.Succeeded())
	{
		m_BridgeMessage.clear();
		LOG_ERROR("Cannot export mGBA script: %s", exported.error.c_str());
		PushError("Cannot export the mGBA script.");
		return;
	}
	m_BridgeScriptPath = rogue::platform::PathToUtf8(exported.path);
	m_BridgeMessage = "mGBA script exported.";
}

void GameConnectionManager::ChangeBridgePort(std::string const& value)
{
	if (!m_UsesPortableBridge)
		return;
	rogue::platform::Settings candidate;
	std::string error;
	if (!rogue::platform::TrySetSetting(candidate, rogue::platform::BridgePortKey, value, error))
	{
		LOG_WARN("Invalid connection port: %s", error.c_str());
		PushError("Enter a port from 1 to 65535.");
		return;
	}
	if (candidate.bridgePort != m_BridgePort)
	{
		std::shared_ptr<IGameMemoryTransport> replacement;
		try
		{
			replacement = std::make_shared<TcpLuaTransport>(candidate.bridgePort);
		}
		catch (std::exception const& exception)
		{
			LOG_ERROR("Cannot change connection port: %s", exception.what());
			PushError("Cannot use port " + std::to_string(candidate.bridgePort) + ".");
			return;
		}

		m_Transport->Stop();
		DisconnectConnections();
		m_Transport = std::move(replacement);
		m_BridgePort = candidate.bridgePort;
		m_BridgePortOverride.reset();
		m_ListeningForConnections = true;
	}
	UserData::SetSavedString(std::string(rogue::platform::BridgePortKey), std::to_string(m_BridgePort));
	ExportPortableScript();
}

void GameConnectionManager::PushError(std::string error)
{
	m_RecentError = std::move(error);
}

void GameConnectionManager::UpdateConnections()
{
	if (m_ActiveConnections.empty() && !m_AcceptingConnection)
	{
		// SessionWorker polls a listening TCP transport before a GameSession
		// exists. Drop results from an old session because its callbacks are done.
		(void)m_Transport->PollResults();
		if (m_Transport->State() != TransportState::Connected && m_Transport->State() != TransportState::Stopped)
		{
			m_ListeningForConnections = true;
		}
	}

	if (m_ListeningForConnections && m_AcceptingConnection == nullptr &&
		m_Transport->State() == TransportState::Connected)
	{
		m_AcceptingConnection = std::make_shared<GameConnection>(*this, m_Transport);
	}

	if (m_ListeningForConnections && m_ActiveConnections.empty() && m_AcceptingConnection)
	{
		LOG_INFO("Game: Accepting connection");
		m_RecentError.clear();
		m_ListeningForConnections = false;
		m_ActiveConnections.push_back({m_NextConnectionId++, std::move(m_AcceptingConnection)});
	}

	for (ActiveGameConnection& active : m_ActiveConnections)
		active.game->Update();

	for (auto active = m_ActiveConnections.begin(); active != m_ActiveConnections.end();)
	{
		if (active->game->HasDisconnected())
		{
			LOG_INFO("Game: Disconnected");
			active = m_ActiveConnections.erase(active);
			if (m_ActiveConnections.empty() && m_Transport->State() != TransportState::Connected &&
				m_Transport->State() != TransportState::Stopped)
			{
				m_ListeningForConnections = true;
			}
		}
		else
		{
			++active;
		}
	}
}

void GameConnectionManager::DisconnectConnections()
{
	if (m_AcceptingConnection)
	{
		m_AcceptingConnection->Disconnect();
		m_AcceptingConnection.reset();
	}
	for (ActiveGameConnection& active : m_ActiveConnections)
		active.game->Disconnect();
	m_ActiveConnections.clear();
}
