#include "Application/SessionWorker.h"
#include "Bridge/TcpSocket.h"
#include "GameConnectionManager.h"
#include "Platform/Configuration.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace
{
using namespace std::chrono_literals;
using rogue::app::SessionWorker;
using rogue::app::UiCommand;
using rogue::app::UiSnapshot;
using rogue::app::WorkerState;
using rogue::bridge::TcpSocket;

struct TestPaths
{
	std::filesystem::path root = std::filesystem::temp_directory_path() /
		("rogue-connection-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	rogue::platform::AppPaths paths;

	TestPaths()
	{
		paths.dataDirectory = root / "data";
		paths.configDirectory = root / "config";
		paths.settingsFile = paths.configDirectory / "settings.ini";
		paths.logFile = paths.dataDirectory / "logs" / "RogueAssistant.log";
		paths.scriptDirectory = paths.dataDirectory / "scripts";
		paths.resourceDirectory = ROGUE_TEST_RESOURCE_DIR;
	}

	~TestPaths()
	{
		std::error_code error;
		std::filesystem::remove_all(root, error);
	}
};

std::uint16_t ReservePort(TcpSocket& socket)
{
	std::string error;
	REQUIRE(socket.ListenLoopback(0, error));
	auto const port = socket.BoundPort(error);
	REQUIRE(port != 0);
	return port;
}

template <typename Predicate>
bool WaitFor(SessionWorker const& worker, Predicate predicate)
{
	auto const deadline = std::chrono::steady_clock::now() + 5s;
	do
	{
		if (predicate(worker.Snapshot()))
			return true;
		std::this_thread::sleep_for(2ms);
	} while (std::chrono::steady_clock::now() < deadline);
	return false;
}

bool SetPort(SessionWorker& worker, std::uint16_t port)
{
	UiCommand command;
	command.type = UiCommand::Type::SetBridgePort;
	command.value = std::to_string(port);
	return worker.Submit(std::move(command));
}
} // namespace

TEST_CASE("A busy startup port does not stop the session worker", "[connection][lifecycle]")
{
	TestPaths data;
	TcpSocket occupied;
	auto const port = ReservePort(occupied);
	SessionWorker worker(std::make_unique<GameConnectionManager>(port, data.paths), 2ms);
	REQUIRE(WaitFor(worker, [port](UiSnapshot const& snapshot) {
		return snapshot.workerState == WorkerState::Running &&
			snapshot.error == "Cannot listen on port " + std::to_string(port) + ".";
	}));
	REQUIRE(worker.Snapshot().transportState == TransportState::Disconnected);
	REQUIRE(SetPort(worker, port));
	REQUIRE(WaitFor(worker, [port](UiSnapshot const& snapshot) {
		return snapshot.error == "Cannot use port " + std::to_string(port) + ".";
	}));

	auto recoveredPort = port;
	SECTION("Retry the same port after it becomes free")
	{
		occupied.Close();
	}
	SECTION("Choose another port while the first is still busy")
	{
		TcpSocket other;
		recoveredPort = ReservePort(other);
	}
	SECTION("Close the app while the port is still busy")
	{
		worker.Stop();
		REQUIRE(worker.Snapshot().workerState == WorkerState::Stopped);
		REQUIRE_FALSE(SetPort(worker, port));
		return;
	}

	REQUIRE(SetPort(worker, recoveredPort));
	REQUIRE(WaitFor(worker, [recoveredPort](UiSnapshot const& snapshot) {
		return snapshot.workerState == WorkerState::Running && snapshot.error.empty() &&
			snapshot.transportState == TransportState::Listening && snapshot.bridgePort == recoveredPort;
	}));
	worker.Stop();
	REQUIRE(rogue::platform::LoadSettings(data.paths.settingsFile).settings.bridgePort == recoveredPort);
	std::ifstream script(data.paths.scriptDirectory / "RogueAssistant_mGBA.lua");
	REQUIRE(script.is_open());
	std::string const contents{std::istreambuf_iterator<char>(script), std::istreambuf_iterator<char>()};
	REQUIRE(contents.find("local BRIDGE_PORT = " + std::to_string(recoveredPort)) != std::string::npos);
}

TEST_CASE("A failed port change keeps the working listener", "[connection]")
{
	TestPaths data;
	TcpSocket available;
	auto const port = ReservePort(available);
	TcpSocket occupied;
	auto const busyPort = ReservePort(occupied);
	available.Close();
	SessionWorker worker(std::make_unique<GameConnectionManager>(port, data.paths), 2ms);
	REQUIRE(WaitFor(worker, [](UiSnapshot const& snapshot) {
		return snapshot.transportState == TransportState::Listening && snapshot.error.empty();
	}));
	REQUIRE(SetPort(worker, busyPort));
	REQUIRE(WaitFor(worker, [busyPort](UiSnapshot const& snapshot) {
		return snapshot.error == "Cannot use port " + std::to_string(busyPort) + ".";
	}));
	REQUIRE(worker.Snapshot().bridgePort == port);
	REQUIRE(worker.Snapshot().transportState == TransportState::Listening);
	REQUIRE(SetPort(worker, port));
	REQUIRE(WaitFor(worker, [](UiSnapshot const& snapshot) { return snapshot.error.empty(); }));
	std::string error;
	TcpSocket client;
	REQUIRE(client.ConnectLoopback(port, error));
}
