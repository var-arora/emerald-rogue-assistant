#pragma once

#include <cstdio>
#include <filesystem>

#if !defined(NDEBUG)
#define _ASSERTS
#endif

void RogueLog_Initialize(std::filesystem::path const& logFile);
void RogueLog_Shutdown();
void RogueLog_Write(char const* level, char const* format, ...);
void RogueLog_DebugBreak();

#define LOG_INFO(...)  RogueLog_Write("INFO", __VA_ARGS__)
#define LOG_WARN(...)  RogueLog_Write("WARN", __VA_ARGS__)
#define LOG_ERROR(...) RogueLog_Write("ERROR", __VA_ARGS__)

#ifdef _ASSERTS
#define ASSERT_MSG(condition, ...) do { if(!(condition)) { LOG_ERROR(__VA_ARGS__); RogueLog_DebugBreak(); } } while(0)
#else
#define ASSERT_MSG(condition, ...) do { if(!(condition)) { LOG_ERROR(__VA_ARGS__); } } while(0)
#endif

#define ASSERT_FAIL(...) ASSERT_MSG(false, __VA_ARGS__)
