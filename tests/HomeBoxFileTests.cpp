#include "Endian.h"
#include "Platform/FileSystem.h"
#include "Storage/HomeBoxFile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace rogue::storage;

namespace
{
class TemporaryDirectory
{
  public:
	TemporaryDirectory()
	{
		auto const id = std::chrono::steady_clock::now().time_since_epoch().count();
		path = fs::temp_directory_path() / ("rogue-home-box-tests-" + std::to_string(id));
		std::error_code ec;
		fs::create_directories(path, ec);
		REQUIRE(!ec);
	}

	~TemporaryDirectory()
	{
		std::error_code ignored;
		fs::remove_all(path, ignored);
	}

	fs::path path;
};

HomeBoxDimensions Dimensions()
{
	return HomeBoxDimensions{3, 1, 0x78563412U, 2, 3, 5};
}

HomeBoxData ExampleData()
{
	HomeBoxData data;
	data.dimensions = Dimensions();
	data.records = {
		HomeBoxRecord{0, {1, 2, 3}, {4, 5, 6, 7, 8}},
		HomeBoxRecord{1, {9, 10, 11}, {12, 13, 14, 15, 16}},
	};
	return data;
}

void AppendLittle(std::vector<std::byte>& output, std::uint32_t value)
{
	std::size_t const offset = output.size();
	output.resize(offset + sizeof(value));
	REQUIRE(rogue::endian::WriteLittle(output, offset, value));
}

std::vector<std::byte> EncodeOriginal(HomeBoxData const& data)
{
	std::vector<std::byte> encoded;
	AppendLittle(encoded, 3497814U);
	AppendLittle(encoded, 0U);
	AppendLittle(encoded, data.dimensions.remoteBoxCount);
	AppendLittle(encoded, data.dimensions.metadataSize);
	AppendLittle(encoded, data.dimensions.pokemonDataSize);
	for (HomeBoxRecord const& record : data.records)
	{
		std::uint32_t checksum = 0;
		for (std::uint8_t byte : record.metadata)
		{
			encoded.push_back(static_cast<std::byte>(byte));
			checksum += byte;
		}
		for (std::uint8_t byte : record.pokemonData)
		{
			encoded.push_back(static_cast<std::byte>(byte));
			checksum += byte;
		}
		AppendLittle(encoded, checksum);
	}
	AppendLittle(encoded, 7893612U);
	return encoded;
}

void WriteBytes(fs::path const& path, std::span<std::byte const> bytes)
{
	std::string error;
	REQUIRE(rogue::platform::WriteFileAtomically(path, bytes, error));
}

std::vector<std::byte> ReadBytes(fs::path const& path)
{
	std::vector<std::byte> bytes;
	std::string error;
	REQUIRE(rogue::platform::ReadFile(path, MaximumHomeBoxFileSize, bytes, error));
	return bytes;
}
} // namespace

TEST_CASE("Home Box writes the original assistant's exact file format", "[home-box][codec]")
{
	HomeBoxData const expected = ExampleData();
	std::vector<std::byte> encoded;
	std::string error;
	REQUIRE(EncodeHomeBox(expected, encoded, error));

	// Original Windows layout: five u32 header fields, two records with
	// byte-sum checksums, then the fixed footer. Records have no index field.
	constexpr std::array<std::uint8_t, 48> originalBytes{
		0x56, 0x5F, 0x35, 0x00, 0, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 0,
		1, 2, 3, 4, 5, 6, 7, 8, 36, 0, 0, 0,
		9, 10, 11, 12, 13, 14, 15, 16, 100, 0, 0, 0,
		0x6C, 0x72, 0x78, 0x00,
	};
	REQUIRE(encoded.size() == originalBytes.size());
	for (std::size_t index = 0; index < originalBytes.size(); ++index)
		REQUIRE(encoded[index] == static_cast<std::byte>(originalBytes[index]));
	REQUIRE(encoded == EncodeOriginal(expected));

	HomeBoxData decoded;
	REQUIRE(DecodeHomeBox(encoded, expected.dimensions, decoded, error));
	REQUIRE(decoded == expected);

	auto reversed = expected;
	std::reverse(reversed.records.begin(), reversed.records.end());
	std::vector<std::byte> canonical;
	REQUIRE(EncodeHomeBox(reversed, canonical, error));
	REQUIRE(canonical == encoded);
}

TEST_CASE("Home Box rejects damaged files and unsupported formats", "[home-box][codec]")
{
	HomeBoxData const expected = ExampleData();
	auto const encoded = EncodeOriginal(expected);
	HomeBoxData decoded;
	std::string error;

	auto corruptRecord = encoded;
	corruptRecord[20] ^= std::byte{0x80};
	REQUIRE_FALSE(DecodeHomeBox(corruptRecord, expected.dimensions, decoded, error));
	REQUIRE(error.find("checksum") != std::string::npos);

	auto corruptFooter = encoded;
	corruptFooter.back() ^= std::byte{1};
	REQUIRE_FALSE(DecodeHomeBox(corruptFooter, expected.dimensions, decoded, error));
	REQUIRE(error.find("footer") != std::string::npos);

	for (std::size_t size = 0; size < encoded.size(); ++size)
		REQUIRE_FALSE(DecodeHomeBox(std::span(encoded).first(size), expected.dimensions, decoded, error));

	auto trailing = encoded;
	trailing.push_back(std::byte{0});
	REQUIRE_FALSE(DecodeHomeBox(trailing, expected.dimensions, decoded, error));

	auto wrongVersion = encoded;
	REQUIRE(rogue::endian::WriteLittle<std::uint32_t>(wrongVersion, 4, 1));
	REQUIRE_FALSE(DecodeHomeBox(wrongVersion, expected.dimensions, decoded, error));

	auto wrongMagic = encoded;
	wrongMagic[0] ^= std::byte{1};
	REQUIRE_FALSE(DecodeHomeBox(wrongMagic, expected.dimensions, decoded, error));
}

TEST_CASE("Home Box checks dimensions without adding identity fields to the file", "[home-box][codec]")
{
	HomeBoxData const expected = ExampleData();
	auto const encoded = EncodeOriginal(expected);
	HomeBoxData decoded;
	std::string error;
	for (HomeBoxDimensions mismatch :
		 {HomeBoxDimensions{3, 1, expected.dimensions.trainerId, 1, 3, 5},
		  HomeBoxDimensions{3, 1, expected.dimensions.trainerId, 2, 4, 5},
		  HomeBoxDimensions{3, 1, expected.dimensions.trainerId, 2, 3, 4}})
	{
		REQUIRE_FALSE(DecodeHomeBox(encoded, mismatch, decoded, error));
	}

	HomeBoxDimensions identity = expected.dimensions;
	identity.edition = 0;
	identity.trainerId = 7;
	REQUIRE(DecodeHomeBox(encoded, identity, decoded, error));
	REQUIRE(decoded.dimensions == identity);
	REQUIRE(decoded.records == expected.records);
	std::vector<std::byte> unchanged;
	REQUIRE(EncodeHomeBox(decoded, unchanged, error));
	REQUIRE(unchanged == encoded);

	HomeBoxDimensions unsupported = expected.dimensions;
	unsupported.romAssistantApi = 2;
	REQUIRE_FALSE(DecodeHomeBox(encoded, unsupported, decoded, error));
	REQUIRE(error.find("API 3") != std::string::npos);

	auto invalid = expected;
	invalid.records[1].remoteBoxIndex = 0;
	REQUIRE_FALSE(EncodeHomeBox(invalid, unchanged, error));
	invalid = expected;
	invalid.records[0].pokemonData.pop_back();
	REQUIRE_FALSE(EncodeHomeBox(invalid, unchanged, error));
}

TEST_CASE("Home Box loads and updates original files without changing their format", "[home-box][persistence]")
{
	TemporaryDirectory temporary;
	fs::path const primary = temporary.path / "1" / "trainer" / "boxes.dat";
	HomeBoxData updated = ExampleData();
	auto const original = EncodeOriginal(updated);
	WriteBytes(primary, original);

	auto loaded = LoadHomeBoxFile(primary, updated.dimensions);
	REQUIRE(loaded.Succeeded());
	REQUIRE(*loaded.data == updated);
	REQUIRE(ReadBytes(primary) == original);
	REQUIRE_FALSE(fs::exists(HomeBoxBackupPath(primary)));

	updated.records[0].pokemonData[0] = 99;
	std::string error;
	REQUIRE(SaveHomeBoxFile(primary, updated, error));
	REQUIRE(ReadBytes(HomeBoxBackupPath(primary)) == original);
	REQUIRE(ReadBytes(primary) == EncodeOriginal(updated));

	loaded = LoadHomeBoxFile(primary, updated.dimensions);
	REQUIRE(loaded.Succeeded());
	REQUIRE(*loaded.data == updated);
	auto const previous = ReadBytes(primary);
	updated.records[1].metadata[0] = 200;
	REQUIRE(SaveHomeBoxFile(primary, updated, error));
	REQUIRE(ReadBytes(HomeBoxBackupPath(primary)) == previous);
	REQUIRE(ReadBytes(primary) == EncodeOriginal(updated));

	// Saving introduces only the normal backup, not a format-migration file.
	REQUIRE(std::distance(fs::directory_iterator(primary.parent_path()), fs::directory_iterator{}) == 2);
}

TEST_CASE("backup recovery warns and refuses to overwrite an invalid primary", "[home-box][persistence]")
{
	TemporaryDirectory temporary;
	fs::path const primary = temporary.path / "boxes.dat";
	HomeBoxData const expected = ExampleData();
	std::vector<std::byte> valid;
	std::string error;
	REQUIRE(EncodeHomeBox(expected, valid, error));
	WriteBytes(HomeBoxBackupPath(primary), valid);
	std::vector<std::byte> const invalid{std::byte{'b'}, std::byte{'a'}, std::byte{'d'}};
	WriteBytes(primary, invalid);

	auto const loaded = LoadHomeBoxFile(primary, expected.dimensions);
	REQUIRE(loaded.Succeeded());
	REQUIRE(loaded.source == HomeBoxLoadSource::Backup);
	REQUIRE(loaded.primaryInvalid);
	REQUIRE(loaded.warning.find("will not be overwritten") != std::string::npos);
	REQUIRE_FALSE(SaveHomeBoxFile(primary, *loaded.data, error));
	REQUIRE(error.find("refusing to replace") != std::string::npos);
	REQUIRE(ReadBytes(primary) == invalid);
}

TEST_CASE("a missing primary recovers from backup and ignores interrupted temporary siblings",
		  "[home-box][persistence]")
{
	TemporaryDirectory temporary;
	fs::path const primary = temporary.path / "boxes.dat";
	HomeBoxData updated = ExampleData();
	std::vector<std::byte> valid;
	std::string error;
	REQUIRE(EncodeHomeBox(updated, valid, error));
	WriteBytes(HomeBoxBackupPath(primary), valid);
	WriteBytes(fs::path(primary.string() + ".tmp.interrupted"), std::vector<std::byte>{std::byte{0xFF}});

	auto loaded = LoadHomeBoxFile(primary, updated.dimensions);
	REQUIRE(loaded.Succeeded());
	REQUIRE(loaded.source == HomeBoxLoadSource::Backup);
	REQUIRE(loaded.primaryMissing);
	updated.records[1].metadata[0] = 42;
	REQUIRE(SaveHomeBoxFile(primary, updated, error));
	loaded = LoadHomeBoxFile(primary, updated.dimensions);
	REQUIRE(loaded.Succeeded());
	REQUIRE(loaded.source == HomeBoxLoadSource::Primary);
	REQUIRE(*loaded.data == updated);
}

TEST_CASE("Home Box runtime layout validation checks counts, spans, and size bounds", "[home-box][layout]")
{
	HomeBoxLayout layout{24, 2, 4, 0, 3, 12, 5, 16, 20};
	std::string error;
	REQUIRE(ValidateHomeBoxLayout(layout, error));

	layout.totalBoxCount = 256;
	REQUIRE_FALSE(ValidateHomeBoxLayout(layout, error));
	layout.totalBoxCount = 4;
	layout.minimalBoxOffset = 13;
	REQUIRE_FALSE(ValidateHomeBoxLayout(layout, error));
	layout.minimalBoxOffset = 0;
	layout.trainerIdOffset = 22;
	REQUIRE_FALSE(ValidateHomeBoxLayout(layout, error));
	layout.trainerIdOffset = 20;
	layout.pokemonBoxSize = MaximumHomeBoxRecordDataSize + 1;
	REQUIRE_FALSE(ValidateHomeBoxLayout(layout, error));
	layout.pokemonBoxSize = 5;
	layout.stateSize = MaximumHomeBoxStateSize + 1;
	REQUIRE_FALSE(ValidateHomeBoxLayout(layout, error));
}
