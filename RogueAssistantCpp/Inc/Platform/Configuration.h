#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rogue::platform
{
	inline constexpr std::string_view MultiplayerHostPortKey = "Multiplayer.HostPort";
	inline constexpr std::string_view MultiplayerJoinIpKey = "Multiplayer.JoinIP";
	inline constexpr std::string_view BridgePortKey = "Bridge.Port";

	struct Settings
	{
		std::uint16_t multiplayerHostPort = 30025;
		std::string multiplayerJoinIp;
		std::uint16_t bridgePort = 30125;
	};

	struct SettingsLoadResult
	{
		Settings settings;
		bool fileFound = false;
		bool needsRewrite = false;
		std::vector<std::string> diagnostics;

		[[nodiscard]] bool Succeeded() const { return diagnostics.empty(); }
	};

	[[nodiscard]] SettingsLoadResult LoadSettings(std::filesystem::path const& path);
	[[nodiscard]] bool SaveSettings(
		std::filesystem::path const& path, Settings const& settings, std::string& error);
	[[nodiscard]] bool TrySetSetting(
		Settings& settings, std::string_view key, std::string_view value, std::string& error);
	[[nodiscard]] std::string GetSetting(Settings const& settings, std::string_view key);
}
