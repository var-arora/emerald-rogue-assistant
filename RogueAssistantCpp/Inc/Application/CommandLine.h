#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rogue::app
{
struct DesktopOptions
{
	std::optional<std::uint16_t> bridgePort;
	bool showHelp = false;
	bool showVersion = false;
	std::string error;
};

[[nodiscard]] DesktopOptions ParseDesktopOptions(std::span<std::string_view const> arguments);
[[nodiscard]] std::uint16_t SelectBridgePort(std::optional<std::uint16_t> currentRunOverride,
											 std::uint16_t configuredPort);
[[nodiscard]] std::string DesktopUsage();
} // namespace rogue::app
