#pragma once

#include "Bridge/GameMemoryTransport.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rogue::app
{
inline constexpr std::size_t MaximumPendingUiCommands = 256;

enum class WorkerState
{
	Starting,
	Running,
	Stopping,
	Stopped,
	Failed,
};

enum class UiPage
{
	Awaiting,
	Multiplayer,
	HomeBox,
};

struct MultiplayerSnapshot
{
	bool requestingHost = false;
	bool awaitingAddress = false;
	bool connected = false;
	std::uint16_t port = 0;
	friend bool operator==(MultiplayerSnapshot const&, MultiplayerSnapshot const&) = default;
};

struct HomeBoxSnapshot
{
	bool loading = false;
	bool saving = false;
	bool requiresReopen = false;
	friend bool operator==(HomeBoxSnapshot const&, HomeBoxSnapshot const&) = default;
};

struct ConnectionSnapshot
{
	std::uint64_t id = 0;
	UiPage page = UiPage::Awaiting;
	MultiplayerSnapshot multiplayer;
	HomeBoxSnapshot homeBox;
	friend bool operator==(ConnectionSnapshot const&, ConnectionSnapshot const&) = default;
};

struct UiSnapshot
{
	std::uint64_t revision = 0;
	WorkerState workerState = WorkerState::Starting;
	TransportState transportState = TransportState::Disconnected;
	std::vector<ConnectionSnapshot> connections;
	std::string error;
	std::string multiplayerHostPort = "30025";
	std::string multiplayerJoinAddress;
	std::uint16_t bridgePort = 30125;
	std::string bridgeScriptPath;
	std::string bridgeMessage;
	friend bool operator==(UiSnapshot const&, UiSnapshot const&) = default;
};

struct UiCommand
{
	enum class Type
	{
		ProvideMultiplayerAddress,
		SetBridgePort,
		ExportBridgeScript,
	};

	Type type = Type::ProvideMultiplayerAddress;
	std::uint64_t connectionId = 0;
	std::string value;
};
} // namespace rogue::app
