#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rogue::platform
{
	[[nodiscard]] bool EnsureDirectory(std::filesystem::path const& directory, std::string& error);
	[[nodiscard]] bool ReadFile(std::filesystem::path const& source, std::size_t maximumSize,
								std::vector<std::byte>& bytes, std::string& error);
	[[nodiscard]] bool ReadTextFile(std::filesystem::path const& source, std::size_t maximumSize, std::string& text,
									std::string& error);
	[[nodiscard]] bool WriteFileAtomically(
		std::filesystem::path const& destination, std::span<std::byte const> bytes, std::string& error);
	[[nodiscard]] bool WriteTextFileAtomically(
		std::filesystem::path const& destination, std::string_view text, std::string& error);
}
