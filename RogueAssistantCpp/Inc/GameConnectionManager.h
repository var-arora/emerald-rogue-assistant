#pragma once

#include "Application/ISessionRuntime.h"
#include "Bridge/GameMemoryTransport.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class GameConnection;

using GameConnectionRef = std::shared_ptr<GameConnection>;

struct ActiveGameConnection
{
	std::uint64_t id = 0;
	GameConnectionRef game;
};

class GameConnectionManager final : public rogue::app::ISessionRuntime
{
  public:
	explicit GameConnectionManager(std::shared_ptr<IGameMemoryTransport> transport);
	explicit GameConnectionManager(std::optional<std::uint16_t> bridgePortOverride);
	~GameConnectionManager() override;

	void Start() override;
	void HandleCommand(rogue::app::UiCommand command) override;
	void Tick() override;
	[[nodiscard]] rogue::app::UiSnapshot Snapshot() const override;
	void Stop() override;

	void PushError(std::string error);

  private:
	void UpdateConnections();
	void DisconnectConnections();
	void ExportPortableScript();
	void ChangeBridgePort(std::string const& value);

	std::shared_ptr<IGameMemoryTransport> m_Transport;
	std::optional<std::uint16_t> m_BridgePortOverride;
	std::string m_RecentError;
	std::string m_BridgeScriptPath;
	std::string m_BridgeMessage;
	std::uint16_t m_BridgePort = 30125;
	bool m_UsesPortableBridge = false;
	bool m_ListeningForConnections = false;
	bool m_Started = false;
	std::uint64_t m_NextConnectionId = 1;
	std::vector<ActiveGameConnection> m_ActiveConnections;
	GameConnectionRef m_AcceptingConnection;
};
