#pragma once

#include <SFML/System/String.hpp>

#include <string_view>

namespace rogue::ui
{
[[nodiscard]] inline sf::String DecodeUtf8(std::string_view text)
{
	return sf::String::fromUtf8(text.begin(), text.end());
}
} // namespace rogue::ui
