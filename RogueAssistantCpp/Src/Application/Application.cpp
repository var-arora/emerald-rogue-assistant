#include "Application/Application.h"

#include <utility>

namespace rogue::app
{
Application::Application(std::unique_ptr<ISessionRuntime> runtime, std::chrono::milliseconds tickInterval)
	: m_SessionWorker(std::move(runtime), tickInterval)
{
}

Application::~Application()
{
	Stop();
}

bool Application::Submit(UiCommand command)
{
	return m_SessionWorker.Submit(std::move(command));
}

UiSnapshot Application::Snapshot() const
{
	return m_SessionWorker.Snapshot();
}

void Application::Stop()
{
	m_SessionWorker.Stop();
}
} // namespace rogue::app
