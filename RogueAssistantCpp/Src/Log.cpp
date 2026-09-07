#include "Log.h"

#include <array>
#include <chrono>
#include <cstdarg>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace
{
	std::mutex LogMutex;
	std::ofstream LogFile;

	std::tm LocalTime(std::time_t value)
	{
		std::tm result{};
#if defined(_WIN32)
		localtime_s(&result, &value);
#else
		localtime_r(&value, &result);
#endif
		return result;
	}

	std::string FormatPrefix(char const* level)
	{
		auto const now = std::chrono::system_clock::now();
		std::time_t const time = std::chrono::system_clock::to_time_t(now);
		std::tm local = LocalTime(time);
		std::ostringstream prefix;
		prefix << '[' << std::put_time(&local, "%H:%M:%S") << ']';
		prefix << '[' << level << ']';
		prefix << "[t" << std::this_thread::get_id() << "] ";
		return prefix.str();
	}
}

void RogueLog_Initialize(std::filesystem::path const& logFile)
{
	std::lock_guard<std::mutex> lock(LogMutex);
	LogFile.close();
	std::error_code ec;
	if (!logFile.parent_path().empty())
		std::filesystem::create_directories(logFile.parent_path(), ec);
	if (!ec)
		LogFile.open(logFile, std::ios::out | std::ios::trunc);
}

void RogueLog_Shutdown()
{
	std::lock_guard<std::mutex> lock(LogMutex);
	LogFile.flush();
	LogFile.close();
}

void RogueLog_Write(char const* level, char const* format, ...)
{
	std::array<char, 1024> message{};
	va_list args;
	va_start(args, format);
	int const written = std::vsnprintf(message.data(), message.size(), format, args);
	va_end(args);
	if (written < 0)
		return;

	std::lock_guard<std::mutex> lock(LogMutex);
	std::string const line = FormatPrefix(level) + message.data() + '\n';
	std::cerr << line;
	std::cerr.flush();
	if (LogFile)
	{
		LogFile << line;
		LogFile.flush();
	}

#if defined(_WIN32)
	if (IsDebuggerPresent())
		OutputDebugStringA(line.c_str());
#endif
}

void RogueLog_DebugBreak()
{
#if defined(_WIN32)
	__debugbreak();
#elif defined(SIGTRAP)
	std::raise(SIGTRAP);
#else
	std::abort();
#endif
}
