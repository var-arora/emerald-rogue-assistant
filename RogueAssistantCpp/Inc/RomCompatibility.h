#pragma once

#include <cstdint>

namespace rogue::rom
{
inline constexpr std::uint32_t RequiredAssistantApi = 3;
inline constexpr std::uint8_t VanillaEdition = 0;
inline constexpr std::uint8_t ExEdition = 1;

[[nodiscard]] constexpr bool IsSupportedEdition(std::uint8_t edition)
{
	return edition == VanillaEdition || edition == ExEdition;
}
} // namespace rogue::rom
