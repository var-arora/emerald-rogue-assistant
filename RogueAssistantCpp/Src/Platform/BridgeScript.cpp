#include "Platform/BridgeScript.h"

#include "Platform/FileSystem.h"
#include "Platform/Utf8.h"

#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <shellapi.h>
#else
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;
#endif

namespace rogue::platform
{
namespace
{
constexpr std::size_t MaximumBridgeScriptSize = 2U * 1024U * 1024U;
constexpr std::string_view PortPrefix = "local BRIDGE_PORT = ";
constexpr std::string_view PortMarker = " -- ROGUE_ASSISTANT_BRIDGE_PORT";
constexpr std::string_view ExportedScriptName = "RogueAssistant_mGBA.lua";
} // namespace

ScriptExportResult ExportBridgeScript(std::filesystem::path const& source, std::filesystem::path const& scriptDirectory,
									  std::uint16_t port)
{
	ScriptExportResult result;
	result.path = scriptDirectory / ExportedScriptName;
	if (port == 0)
	{
		result.error = "bridge port must be from 1 through 65535";
		return result;
	}

	std::string script;
	if (!ReadTextFile(source, MaximumBridgeScriptSize, script, result.error))
	{
		result.error = "cannot read bundled bridge script: " + result.error;
		return result;
	}
	std::size_t const prefix = script.find(PortPrefix);
	if (prefix == std::string::npos || script.find(PortPrefix, prefix + PortPrefix.size()) != std::string::npos)
	{
		result.error = "bundled bridge script has no unique port field";
		return result;
	}
	std::size_t const valueBegin = prefix + PortPrefix.size();
	std::size_t const marker = script.find(PortMarker, valueBegin);
	if (marker == std::string::npos || script.find('\n', valueBegin) < marker)
	{
		result.error = "bundled bridge script port marker is malformed";
		return result;
	}
	for (std::size_t index = valueBegin; index < marker; ++index)
	{
		if (script[index] < '0' || script[index] > '9')
		{
			result.error = "bundled bridge script port is not numeric";
			return result;
		}
	}
	script.replace(valueBegin, marker - valueBegin, std::to_string(port));
	if (!WriteTextFileAtomically(result.path, script, result.error))
		result.error = "cannot export bridge script: " + result.error;
	return result;
}

bool RevealDirectory(std::filesystem::path const& directory, std::string& error)
{
	error.clear();
	std::error_code ec;
	if (!std::filesystem::is_directory(directory, ec))
	{
		error = ec ? "cannot inspect script directory: " + ec.message() : "script directory does not exist";
		return false;
	}

#if defined(_WIN32)
	HINSTANCE const result = ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
	if (reinterpret_cast<std::intptr_t>(result) <= 32)
	{
		error = "Windows could not reveal the script directory";
		return false;
	}
	return true;
#else
	std::string const encoded = PathToUtf8(directory);
#if defined(__APPLE__)
	char const* program = "/usr/bin/open";
#else
	char const* program = "xdg-open";
#endif
	char* arguments[] = {const_cast<char*>(program), const_cast<char*>(encoded.c_str()), nullptr};
	pid_t process = 0;
#if defined(__APPLE__)
	int const spawnResult = posix_spawn(&process, program, nullptr, nullptr, arguments, environ);
#else
	int const spawnResult = posix_spawnp(&process, program, nullptr, nullptr, arguments, environ);
#endif
	if (spawnResult != 0)
	{
		error = "cannot launch the platform file manager (error " + std::to_string(spawnResult) + ")";
		return false;
	}
	int status = 0;
	if (waitpid(process, &status, 0) < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		error = "the platform file manager rejected the script directory";
		return false;
	}
	return true;
#endif
}
} // namespace rogue::platform
