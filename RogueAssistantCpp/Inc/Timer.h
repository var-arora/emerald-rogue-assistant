#pragma once
#include <chrono>
#include <cstdint>

using TimeDurationNS = std::int64_t;

class UpdateTimer
{
public:
	static TimeDurationNS const c_1UPS;
	static TimeDurationNS const c_5UPS;
	static TimeDurationNS const c_10UPS;
	static TimeDurationNS const c_20UPS;
	static TimeDurationNS const c_25UPS;
	static TimeDurationNS const c_30UPS;
	static TimeDurationNS const c_60UPS;

	UpdateTimer(TimeDurationNS const& interval);

	bool Update();

	static TimeDurationNS GetCurrentClock();

private:
	TimeDurationNS m_LastUpdate;
	TimeDurationNS m_Timer;
	TimeDurationNS m_UpdateInterval;
	bool m_WasPaused;
};
