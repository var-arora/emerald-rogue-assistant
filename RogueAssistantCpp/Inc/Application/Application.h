#pragma once

#include "Application/SessionWorker.h"

namespace rogue::app
{
class Application
{
  public:
	explicit Application(std::unique_ptr<ISessionRuntime> runtime,
						 std::chrono::milliseconds tickInterval = std::chrono::milliseconds(33));
	~Application();

	Application(Application const&) = delete;
	Application& operator=(Application const&) = delete;

	[[nodiscard]] bool Submit(UiCommand command);
	[[nodiscard]] UiSnapshot Snapshot() const;
	void Stop();

  private:
	SessionWorker m_SessionWorker;
};
} // namespace rogue::app
