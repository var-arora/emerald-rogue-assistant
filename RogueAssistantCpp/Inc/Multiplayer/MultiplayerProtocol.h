#pragma once

#include "RomCompatibility.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace rogue::multiplayer
{
inline constexpr std::uint16_t ProtocolMajor = 1;
inline constexpr std::uint16_t ProtocolMinor = 0;
inline constexpr std::uint32_t RequiredRomAssistantApi = rom::RequiredAssistantApi;
inline constexpr std::size_t HelloSize = 40;

struct Hello
{
	std::uint16_t protocolMajor = ProtocolMajor;
	std::uint16_t protocolMinor = ProtocolMinor;
	std::uint32_t romAssistantApi = RequiredRomAssistantApi;
	std::uint8_t edition = 0;
	std::uint32_t playerCount = 0;
	std::uint32_t multiplayerStateSize = 0;
	std::uint32_t handshakeSize = 0;
	std::uint32_t gameStateSize = 0;
	std::uint32_t playerProfileSize = 0;
	std::uint32_t playerStateSize = 0;

	friend bool operator==(Hello const&, Hello const&) = default;
};

struct CompatibilityResult
{
	bool compatible = false;
	std::uint16_t negotiatedMinor = 0;
	std::string error;
};

[[nodiscard]] bool EncodeHello(Hello const& hello, std::vector<std::byte>& encoded, std::string& error);
[[nodiscard]] bool DecodeHello(std::span<std::byte const> encoded, Hello& hello, std::string& error);
[[nodiscard]] CompatibilityResult CheckCompatibility(Hello const& local, Hello const& remote);
} // namespace rogue::multiplayer
