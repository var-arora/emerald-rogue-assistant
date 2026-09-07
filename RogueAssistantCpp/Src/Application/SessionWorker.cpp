#include "Application/SessionWorker.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace rogue::app
{
SessionWorker::SessionWorker(std::unique_ptr<ISessionRuntime> runtime, std::chrono::milliseconds tickInterval)
	: m_Runtime(std::move(runtime)), m_TickInterval(tickInterval)
{
	if (!m_Runtime)
		throw std::invalid_argument("SessionWorker requires a session runtime");
	if (m_TickInterval <= std::chrono::milliseconds::zero())
		throw std::invalid_argument("SessionWorker requires a positive tick interval");

	m_Thread = std::jthread([this](std::stop_token stopToken) { Run(std::move(stopToken)); });
}

SessionWorker::~SessionWorker()
{
	Stop();
}

bool SessionWorker::Submit(UiCommand command)
{
	if (!m_AcceptingCommands.load(std::memory_order_acquire))
		return false;

	{
		std::lock_guard<std::mutex> lock(m_CommandMutex);
		if (!m_AcceptingCommands.load(std::memory_order_relaxed) || m_Commands.size() >= MaximumPendingUiCommands)
		{
			return false;
		}
		m_Commands.push_back(std::move(command));
	}
	m_Wake.notify_one();
	return true;
}

UiSnapshot SessionWorker::Snapshot() const
{
	std::lock_guard<std::mutex> lock(m_SnapshotMutex);
	return m_Snapshot;
}

void SessionWorker::Stop()
{
	m_AcceptingCommands.store(false, std::memory_order_release);
	m_Thread.request_stop();
	m_Wake.notify_all();
	if (m_Thread.joinable())
		m_Thread.join();
}

void SessionWorker::Run(std::stop_token stopToken) noexcept
{
	WorkerState finalState = WorkerState::Stopped;
	std::string failure;
	UiSnapshot lastSnapshot;

	try
	{
		m_Runtime->Start();
		lastSnapshot = m_Runtime->Snapshot();
		Publish(lastSnapshot, WorkerState::Running);

		while (!stopToken.stop_requested())
		{
			std::deque<UiCommand> commands;
			{
				std::unique_lock<std::mutex> lock(m_CommandMutex);
				m_Wake.wait_for(lock, m_TickInterval,
								[this, &stopToken] { return stopToken.stop_requested() || !m_Commands.empty(); });
				commands.swap(m_Commands);
			}

			if (stopToken.stop_requested())
				break;
			for (UiCommand& command : commands)
				m_Runtime->HandleCommand(std::move(command));

			m_Runtime->Tick();
			lastSnapshot = m_Runtime->Snapshot();
			Publish(lastSnapshot, WorkerState::Running);
		}
	}
	catch (std::exception const& exception)
	{
		finalState = WorkerState::Failed;
		failure = exception.what();
	}
	catch (...)
	{
		finalState = WorkerState::Failed;
		failure = "The app stopped after an unknown error.";
	}

	m_AcceptingCommands.store(false, std::memory_order_release);
	Publish(lastSnapshot, WorkerState::Stopping, failure);
	try
	{
		m_Runtime->Stop();
		lastSnapshot = m_Runtime->Snapshot();
	}
	catch (std::exception const& exception)
	{
		finalState = WorkerState::Failed;
		if (failure.empty())
			failure = exception.what();
	}
	catch (...)
	{
		finalState = WorkerState::Failed;
		if (failure.empty())
			failure = "The app could not stop correctly.";
	}
	Publish(std::move(lastSnapshot), finalState, failure);
}

void SessionWorker::Publish(UiSnapshot snapshot, WorkerState state, std::string const& failure)
{
	snapshot.workerState = state;
	if (!failure.empty())
		snapshot.error = failure;

	std::lock_guard<std::mutex> lock(m_SnapshotMutex);
	snapshot.revision = m_NextRevision++;
	m_Snapshot = std::move(snapshot);
}
} // namespace rogue::app
