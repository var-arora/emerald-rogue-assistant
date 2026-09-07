#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace rogue::platform
{
	[[nodiscard]] bool IsValidUtf8(std::string_view text);
	[[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view text);
	[[nodiscard]] std::string PathToUtf8(std::filesystem::path const& path);
}
