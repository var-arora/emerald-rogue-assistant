#include "Timer.h"

namespace
{
	constexpr TimeDurationNS NanosecondsPerSecond = 1'000'000'000;
}

TimeDurationNS const UpdateTimer::c_1UPS = NanosecondsPerSecond;
TimeDurationNS const UpdateTimer::c_5UPS = NanosecondsPerSecond / 5;
TimeDurationNS const UpdateTimer::c_10UPS = NanosecondsPerSecond / 10;
TimeDurationNS const UpdateTimer::c_20UPS = NanosecondsPerSecond / 20;
TimeDurationNS const UpdateTimer::c_25UPS = NanosecondsPerSecond / 25;
TimeDurationNS const UpdateTimer::c_30UPS = NanosecondsPerSecond / 30;
TimeDurationNS const UpdateTimer::c_60UPS = NanosecondsPerSecond / 60;

TimeDurationNS UpdateTimer::GetCurrentClock()
{
	auto timePoint = std::chrono::steady_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(timePoint.time_since_epoch());
	return duration.count();
}

UpdateTimer::UpdateTimer(TimeDurationNS const& interval)
	: m_LastUpdate()
	, m_Timer(0)
	, m_UpdateInterval(interval)
	, m_WasPaused(true)
{
}

bool UpdateTimer::Update()
{
	TimeDurationNS currentTime = GetCurrentClock();
	auto deltaTime = currentTime - m_LastUpdate;

	m_LastUpdate = currentTime;
	m_Timer += deltaTime;

	if(m_WasPaused || m_Timer >= m_UpdateInterval)
	{
		if (m_WasPaused)
		{
			m_Timer = 0;
			m_WasPaused = false;
		}
		else
		{
			m_Timer -= m_UpdateInterval;

			// Drop extra elapsed time so a long pause does not cause too many updates.
			if (m_Timer >= m_UpdateInterval * 3)
				m_Timer = m_UpdateInterval;

			if (m_Timer < 0)
				m_Timer = 0;
		}
		return true;
	}

	return false;
}
