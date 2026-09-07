#pragma once

#include "RomCompatibility.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rogue::storage
{
inline constexpr std::uint32_t SupportedRomAssistantApi = rom::RequiredAssistantApi;
inline constexpr std::uint32_t HomeBoxFormatVersion = 0;
inline constexpr std::size_t MaximumHomeBoxFileSize = 64U * 1024U * 1024U;
// Bridge 1.0's 1 MiB body also carries its eight-byte message header.
inline constexpr std::uint32_t MaximumHomeBoxRecordDataSize = 1024U * 1024U - 8U;
inline constexpr std::uint32_t MaximumHomeBoxRecordCount = 255;
inline constexpr std::uint32_t MaximumHomeBoxStateSize = 1024U * 1024U - 8U;

struct HomeBoxDimensions
{
	std::uint32_t romAssistantApi = SupportedRomAssistantApi;
	std::uint8_t edition = 0;
	std::uint32_t trainerId = 0;
	std::uint32_t remoteBoxCount = 0;
	std::uint32_t metadataSize = 0;
	std::uint32_t pokemonDataSize = 0;

	friend bool operator==(HomeBoxDimensions const&, HomeBoxDimensions const&) = default;
};

struct HomeBoxRecord
{
	std::uint32_t remoteBoxIndex = 0;
	std::vector<std::uint8_t> metadata;
	std::vector<std::uint8_t> pokemonData;

	friend bool operator==(HomeBoxRecord const&, HomeBoxRecord const&) = default;
};

struct HomeBoxData
{
	HomeBoxDimensions dimensions;
	std::vector<HomeBoxRecord> records;

	friend bool operator==(HomeBoxData const&, HomeBoxData const&) = default;
};

enum class HomeBoxLoadSource
{
	None,
	Primary,
	Backup,
};

struct HomeBoxLoadResult
{
	std::optional<HomeBoxData> data;
	HomeBoxLoadSource source = HomeBoxLoadSource::None;
	bool primaryMissing = false;
	bool primaryInvalid = false;
	std::string warning;
	std::string error;

	[[nodiscard]] bool Succeeded() const
	{
		return data.has_value();
	}
	[[nodiscard]] bool NotFound() const
	{
		return !data && primaryMissing && error.empty();
	}
};

struct HomeBoxLayout
{
	std::uint32_t stateSize = 0;
	std::uint32_t localBoxCount = 0;
	std::uint32_t totalBoxCount = 0;
	std::uint32_t minimalBoxOffset = 0;
	std::uint32_t minimalBoxSize = 0;
	std::uint32_t pokemonStoragePointerOffset = 0;
	std::uint32_t pokemonBoxSize = 0;
	std::uint32_t remoteIndexOrderOffset = 0;
	std::uint32_t trainerIdOffset = 0;
};

[[nodiscard]] bool ValidateHomeBoxLayout(HomeBoxLayout const& layout, std::string& error);
[[nodiscard]] bool EncodeHomeBox(HomeBoxData const& data, std::vector<std::byte>& encoded, std::string& error);
[[nodiscard]] bool DecodeHomeBox(std::span<std::byte const> encoded, HomeBoxDimensions const& expected,
								 HomeBoxData& data, std::string& error);

[[nodiscard]] std::filesystem::path HomeBoxBackupPath(std::filesystem::path const& primary);
[[nodiscard]] HomeBoxLoadResult LoadHomeBoxFile(std::filesystem::path const& primary,
												HomeBoxDimensions const& expected);
[[nodiscard]] bool SaveHomeBoxFile(std::filesystem::path const& primary, HomeBoxData const& data, std::string& error);
} // namespace rogue::storage
