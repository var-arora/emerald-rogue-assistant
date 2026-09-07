#pragma once

#include "Application/ISessionRuntime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace rogue::app
{
class SessionWorker
{
  public:
	explicit SessionWorker(std::unique_ptr<ISessionRuntime> runtime,
						   std::chrono::milliseconds tickInterval = std::chrono::milliseconds(33));
	~SessionWorker();

	SessionWorker(SessionWorker const&) = delete;
	SessionWorker& operator=(SessionWorker const&) = delete;

	[[nodiscard]] bool Submit(UiCommand command);
	[[nodiscard]] UiSnapshot Snapshot() const;
	void Stop();

  private:
	void Run(std::stop_token stopToken) noexcept;
	void Publish(UiSnapshot snapshot, WorkerState state, std::string const& failure = {});

	std::unique_ptr<ISessionRuntime> m_Runtime;
	std::chrono::milliseconds m_TickInterval;
	std::jthread m_Thread;
	std::atomic<bool> m_AcceptingCommands{true};

	mutable std::mutex m_CommandMutex;
	std::condition_variable m_Wake;
	std::deque<UiCommand> m_Commands;

	mutable std::mutex m_SnapshotMutex;
	UiSnapshot m_Snapshot;
	std::uint64_t m_NextRevision = 1;
};
} // namespace rogue::app
