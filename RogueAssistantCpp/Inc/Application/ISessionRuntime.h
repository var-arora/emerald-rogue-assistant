#pragma once

#include "Application/UiModel.h"

namespace rogue::app
{
class ISessionRuntime
{
  public:
	virtual ~ISessionRuntime() = default;

	virtual void Start() = 0;
	virtual void HandleCommand(UiCommand command) = 0;
	virtual void Tick() = 0;
	[[nodiscard]] virtual UiSnapshot Snapshot() const = 0;
	virtual void Stop() = 0;
};
} // namespace rogue::app
