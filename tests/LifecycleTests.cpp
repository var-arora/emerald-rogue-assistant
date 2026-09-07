#include "Application/Application.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{
struct RuntimeState
{
	std::mutex mutex;
	std::condition_variable changed;
	std::vector<std::string> events;
	std::thread::id ownerThread;
	std::string lastCommand;
	std::size_t ticks = 0;
	bool stopped = false;
	bool blockTick = false;
	bool releaseTick = false;
	bool wrongThread = false;
	bool throwOnStart = false;
};

class FakeRuntime final : public rogue::app::ISessionRuntime
{
  public:
	explicit FakeRuntime(std::shared_ptr<RuntimeState> state) : m_State(std::move(state))
	{
	}

	void Start() override
	{
		std::lock_guard<std::mutex> lock(m_State->mutex);
		m_State->ownerThread = std::this_thread::get_id();
		m_State->events.emplace_back("start");
		m_State->changed.notify_all();
		if (m_State->throwOnStart)
			throw std::runtime_error("start failed");
	}

	void HandleCommand(rogue::app::UiCommand command) override
	{
		std::lock_guard<std::mutex> lock(m_State->mutex);
		m_State->wrongThread = m_State->wrongThread || std::this_thread::get_id() != m_State->ownerThread;
		m_State->lastCommand = std::move(command.value);
		m_State->events.emplace_back("command");
		m_State->changed.notify_all();
	}

	void Tick() override
	{
		std::unique_lock<std::mutex> lock(m_State->mutex);
		m_State->wrongThread = m_State->wrongThread || std::this_thread::get_id() != m_State->ownerThread;
		++m_State->ticks;
		m_State->events.emplace_back("tick");
		m_State->changed.notify_all();
		m_State->changed.wait(lock, [this] { return !m_State->blockTick || m_State->releaseTick; });
	}

	rogue::app::UiSnapshot Snapshot() const override
	{
		std::lock_guard<std::mutex> lock(m_State->mutex);
		rogue::app::UiSnapshot snapshot;
		snapshot.transportState = TransportState::Connected;
		snapshot.error = m_State->lastCommand;
		return snapshot;
	}

	void Stop() override
	{
		std::lock_guard<std::mutex> lock(m_State->mutex);
		m_State->wrongThread = m_State->wrongThread || std::this_thread::get_id() != m_State->ownerThread;
		m_State->events.emplace_back("stop");
		m_State->stopped = true;
		m_State->changed.notify_all();
	}

  private:
	std::shared_ptr<RuntimeState> m_State;
};

bool WaitFor(std::shared_ptr<RuntimeState> const& state, std::function<bool(RuntimeState const&)> predicate)
{
	std::unique_lock<std::mutex> lock(state->mutex);
	return state->changed.wait_for(lock, 2s, [&] { return predicate(*state); });
}
} // namespace

TEST_CASE("Application confines runtime work and callbacks to SessionWorker", "[lifecycle]")
{
	auto state = std::make_shared<RuntimeState>();
	rogue::app::Application application(std::make_unique<FakeRuntime>(state), 1ms);
	REQUIRE(WaitFor(state, [](RuntimeState const& value) { return value.ticks != 0; }));
	REQUIRE(state->ownerThread != std::this_thread::get_id());

	rogue::app::UiCommand command;
	command.connectionId = 42;
	command.value = "127.0.0.1:30025";
	auto const submitted = application.Submit(std::move(command));
	REQUIRE(submitted);
	REQUIRE(WaitFor(state, [](RuntimeState const& value) { return value.lastCommand == "127.0.0.1:30025"; }));

	application.Stop();
	REQUIRE(state->stopped);
	REQUIRE_FALSE(state->wrongThread);
	REQUIRE_FALSE(application.Submit({}));
	auto const snapshot = application.Snapshot();
	REQUIRE(snapshot.workerState == rogue::app::WorkerState::Stopped);
	REQUIRE(snapshot.transportState == TransportState::Connected);
	REQUIRE(snapshot.error == "127.0.0.1:30025");
	REQUIRE(snapshot.revision != 0);
	REQUIRE(state->events.front() == "start");
	REQUIRE(state->events.back() == "stop");
}

TEST_CASE("SessionWorker bounds UI work while its runtime is backpressured", "[lifecycle]")
{
	auto state = std::make_shared<RuntimeState>();
	state->blockTick = true;
	rogue::app::Application application(std::make_unique<FakeRuntime>(state), 1ms);
	REQUIRE(WaitFor(state, [](RuntimeState const& value) { return value.ticks != 0; }));

	for (std::size_t index = 0; index < rogue::app::MaximumPendingUiCommands; ++index)
		REQUIRE(application.Submit({}));
	REQUIRE_FALSE(application.Submit({}));

	{
		std::lock_guard<std::mutex> lock(state->mutex);
		state->releaseTick = true;
		state->changed.notify_all();
	}
	application.Stop();
	REQUIRE(state->stopped);
}

TEST_CASE("SessionWorker publishes failures after deterministic runtime shutdown", "[lifecycle]")
{
	auto state = std::make_shared<RuntimeState>();
	state->throwOnStart = true;
	rogue::app::Application application(std::make_unique<FakeRuntime>(state), 1ms);
	REQUIRE(WaitFor(state, [](RuntimeState const& value) { return value.stopped; }));
	application.Stop();

	auto snapshot = application.Snapshot();
	REQUIRE(snapshot.workerState == rogue::app::WorkerState::Failed);
	REQUIRE(snapshot.error == "start failed");
	snapshot.error = "mutated copy";
	REQUIRE(application.Snapshot().error == "start failed");
	REQUIRE(state->events == std::vector<std::string>{"start", "stop"});
	REQUIRE_FALSE(state->wrongThread);
}
