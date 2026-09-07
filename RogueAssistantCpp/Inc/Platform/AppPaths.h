#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rogue::platform
{
	enum class HostPlatform : std::uint8_t
	{
		Windows,
		MacOS,
		Linux,
	};

	struct PathEnvironment
	{
		HostPlatform platform = HostPlatform::Linux;
		std::filesystem::path homeDirectory;
		std::filesystem::path roamingAppData;
		std::filesystem::path xdgDataHome;
		std::filesystem::path xdgConfigHome;
		std::filesystem::path currentWorkingDirectory;
		std::filesystem::path executablePath;
	};

	struct AppPaths
	{
		HostPlatform platform = HostPlatform::Linux;
		std::filesystem::path dataDirectory;
		std::filesystem::path configDirectory;
		std::filesystem::path settingsFile;
		std::filesystem::path logFile;
		std::filesystem::path scriptDirectory;
		std::filesystem::path resourceDirectory;
		std::optional<std::filesystem::path> legacyDataDirectory;
		std::optional<std::filesystem::path> legacySettingsFile;
	};

	struct MigrationReport
	{
		bool attempted = false;
		bool importedData = false;
		bool importedSettings = false;
		std::vector<std::string> diagnostics;

		[[nodiscard]] bool Succeeded() const { return diagnostics.empty(); }
	};

	[[nodiscard]] AppPaths BuildAppPaths(PathEnvironment const& environment);
	[[nodiscard]] std::optional<AppPaths> DiscoverAppPaths(std::string& error);
	[[nodiscard]] MigrationReport ImportLegacyWindowsData(AppPaths const& paths);
}
