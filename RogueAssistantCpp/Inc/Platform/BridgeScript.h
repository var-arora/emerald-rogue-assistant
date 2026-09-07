#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace rogue::platform
{
struct ScriptExportResult
{
	std::filesystem::path path;
	std::string error;

	[[nodiscard]] bool Succeeded() const
	{
		return error.empty();
	}
};

[[nodiscard]] ScriptExportResult ExportBridgeScript(std::filesystem::path const& source,
													std::filesystem::path const& scriptDirectory, std::uint16_t port);
[[nodiscard]] bool RevealDirectory(std::filesystem::path const& directory, std::string& error);
} // namespace rogue::platform
