#include "Storage/HomeBoxFile.h"

#include "Endian.h"
#include "Platform/FileSystem.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace rogue::storage
{
namespace
{
constexpr std::uint32_t HeaderMagic = 3497814;
constexpr std::uint32_t FooterMagic = 7893612;
constexpr std::size_t HeaderSize = 20;
constexpr std::size_t FooterSize = 4;

bool CheckedAdd(std::size_t left, std::size_t right, std::size_t& result)
{
	if (right > std::numeric_limits<std::size_t>::max() - left)
		return false;
	result = left + right;
	return true;
}

bool CheckedMultiply(std::size_t left, std::size_t right, std::size_t& result)
{
	if (left != 0 && right > std::numeric_limits<std::size_t>::max() / left)
		return false;
	result = left * right;
	return true;
}

bool CalculateFileSize(HomeBoxDimensions const& dimensions, std::size_t& size)
{
	std::size_t recordSize = sizeof(std::uint32_t);
	if (!CheckedAdd(recordSize, dimensions.metadataSize, recordSize) ||
		!CheckedAdd(recordSize, dimensions.pokemonDataSize, recordSize))
	{
		return false;
	}
	std::size_t recordsSize = 0;
	if (!CheckedMultiply(recordSize, dimensions.remoteBoxCount, recordsSize))
		return false;
	size = HeaderSize;
	return CheckedAdd(size, recordsSize, size) && CheckedAdd(size, FooterSize, size) &&
		   size <= MaximumHomeBoxFileSize;
}

bool ValidateDimensions(HomeBoxDimensions const& dimensions, std::string& error)
{
	if (dimensions.romAssistantApi != SupportedRomAssistantApi)
	{
		error = "Home Box requires ROM Assistant API 3";
		return false;
	}
	if (!rom::IsSupportedEdition(dimensions.edition))
	{
		error = "Home Box edition is neither Vanilla nor EX";
		return false;
	}
	if (dimensions.remoteBoxCount > MaximumHomeBoxRecordCount)
	{
		error = "Home Box record count exceeds 255";
		return false;
	}
	if (dimensions.metadataSize == 0 || dimensions.metadataSize > MaximumHomeBoxRecordDataSize ||
		dimensions.pokemonDataSize == 0 || dimensions.pokemonDataSize > MaximumHomeBoxRecordDataSize)
	{
		error = "Home Box record dimensions are zero or oversized";
		return false;
	}
	std::size_t ignored = 0;
	if (!CalculateFileSize(dimensions, ignored))
	{
		error = "Home Box dimensions exceed the file size limit";
		return false;
	}
	return true;
}

void AppendLittle(std::vector<std::byte>& output, std::uint32_t value)
{
	std::size_t const offset = output.size();
	output.resize(offset + sizeof(value));
	(void)endian::WriteLittle<std::uint32_t>(output, offset, value);
}

void AppendBytes(std::vector<std::byte>& output, std::span<std::uint8_t const> bytes)
{
	output.reserve(output.size() + bytes.size());
	for (std::uint8_t byte : bytes)
		output.push_back(static_cast<std::byte>(byte));
}

template <typename T> bool ReadLittle(std::span<std::byte const> input, std::size_t& position, T& value)
{
	if (!endian::ReadLittle<T>(input, position, value))
		return false;
	position += sizeof(T);
	return true;
}

bool ReadBytes(std::span<std::byte const> input, std::size_t& position, std::size_t size,
			   std::vector<std::uint8_t>& output)
{
	if (position > input.size() || size > input.size() - position)
		return false;
	output.resize(size);
	for (std::size_t index = 0; index < size; ++index)
		output[index] = std::to_integer<std::uint8_t>(input[position + index]);
	position += size;
	return true;
}

std::uint32_t RecordChecksum(HomeBoxRecord const& record)
{
	std::uint32_t checksum = 0;
	for (std::uint8_t byte : record.metadata)
		checksum += byte;
	for (std::uint8_t byte : record.pokemonData)
		checksum += byte;
	return checksum;
}

bool DecodeRecords(std::span<std::byte const> encoded, HomeBoxDimensions const& expected, HomeBoxData& output,
				  std::string& error)
{
	std::size_t expectedSize = 0;
	if (!CalculateFileSize(expected, expectedSize) || encoded.size() != expectedSize)
	{
		error = encoded.size() > expectedSize ? "Home Box file has trailing data"
											  : "Home Box file is truncated";
		return false;
	}

	std::size_t position = 0;
	std::uint32_t magic = 0;
	std::uint32_t version = 0;
	std::uint32_t boxCount = 0;
	std::uint32_t metadataSize = 0;
	std::uint32_t pokemonDataSize = 0;
	if (!ReadLittle(encoded, position, magic) || !ReadLittle(encoded, position, version) ||
		!ReadLittle(encoded, position, boxCount) || !ReadLittle(encoded, position, metadataSize) ||
		!ReadLittle(encoded, position, pokemonDataSize))
	{
		error = "Home Box header is truncated";
		return false;
	}
	if (magic != HeaderMagic || version != HomeBoxFormatVersion || boxCount != expected.remoteBoxCount ||
		metadataSize != expected.metadataSize || pokemonDataSize != expected.pokemonDataSize)
	{
		error = "Home Box header does not match the expected format and dimensions";
		return false;
	}

	HomeBoxData decoded;
	decoded.dimensions = expected;
	decoded.records.reserve(boxCount);
	for (std::uint32_t recordIndex = 0; recordIndex < boxCount; ++recordIndex)
	{
		HomeBoxRecord record;
		record.remoteBoxIndex = recordIndex;
		if (!ReadBytes(encoded, position, metadataSize, record.metadata) ||
			!ReadBytes(encoded, position, pokemonDataSize, record.pokemonData))
		{
			error = "Home Box record is truncated";
			return false;
		}
		std::uint32_t checksum = 0;
		if (!ReadLittle(encoded, position, checksum) || checksum != RecordChecksum(record))
		{
			error = "Home Box record checksum mismatch";
			return false;
		}
		decoded.records.push_back(std::move(record));
	}
	if (!ReadLittle(encoded, position, magic) || magic != FooterMagic || position != encoded.size())
	{
		error = "Home Box footer is invalid";
		return false;
	}
	output = std::move(decoded);
	return true;
}

bool ReadCandidate(std::filesystem::path const& path, HomeBoxDimensions const& expected, std::vector<std::byte>& bytes,
				   HomeBoxData& data, std::string& error)
{
	if (!platform::ReadFile(path, MaximumHomeBoxFileSize, bytes, error))
		return false;
	return DecodeHomeBox(bytes, expected, data, error);
}
} // namespace

bool ValidateHomeBoxLayout(HomeBoxLayout const& layout, std::string& error)
{
	error.clear();
	if (layout.stateSize == 0 || layout.stateSize > MaximumHomeBoxStateSize || layout.totalBoxCount == 0 ||
		layout.localBoxCount > layout.totalBoxCount || layout.totalBoxCount > MaximumHomeBoxRecordCount)
	{
		error = "Home Box counts or state size are invalid";
		return false;
	}
	if (layout.minimalBoxSize == 0 || layout.minimalBoxSize > MaximumHomeBoxRecordDataSize ||
		layout.pokemonBoxSize == 0 || layout.pokemonBoxSize > MaximumHomeBoxRecordDataSize)
	{
		error = "Home Box record sizes are zero or oversized";
		return false;
	}

	auto spanFits = [&layout](std::uint32_t offset, std::size_t size) {
		return offset <= layout.stateSize && size <= static_cast<std::size_t>(layout.stateSize - offset);
	};
	std::size_t minimalBytes = 0;
	if (!CheckedMultiply(layout.minimalBoxSize, layout.totalBoxCount, minimalBytes) ||
		!spanFits(layout.minimalBoxOffset, minimalBytes) ||
		!spanFits(layout.pokemonStoragePointerOffset, sizeof(std::uint32_t)) ||
		!spanFits(layout.remoteIndexOrderOffset, layout.totalBoxCount) ||
		!spanFits(layout.trainerIdOffset, sizeof(std::uint32_t)))
	{
		error = "Home Box offsets and sizes exceed the observed state";
		return false;
	}
	return true;
}

bool EncodeHomeBox(HomeBoxData const& data, std::vector<std::byte>& encoded, std::string& error)
{
	encoded.clear();
	error.clear();
	if (!ValidateDimensions(data.dimensions, error))
		return false;
	if (data.records.size() != data.dimensions.remoteBoxCount)
	{
		error = "Home Box record count does not match its header";
		return false;
	}

	std::vector<HomeBoxRecord const*> recordsByIndex(data.dimensions.remoteBoxCount, nullptr);
	for (HomeBoxRecord const& record : data.records)
	{
		if (record.remoteBoxIndex >= data.dimensions.remoteBoxCount || recordsByIndex[record.remoteBoxIndex] != nullptr)
		{
			error = "Home Box has a duplicate or out-of-range record index";
			return false;
		}
		if (record.metadata.size() != data.dimensions.metadataSize ||
			record.pokemonData.size() != data.dimensions.pokemonDataSize)
		{
			error = "Home Box record data does not match its declared dimensions";
			return false;
		}
		recordsByIndex[record.remoteBoxIndex] = &record;
	}

	std::size_t fileSize = 0;
	(void)CalculateFileSize(data.dimensions, fileSize);
	encoded.reserve(fileSize);
	AppendLittle(encoded, HeaderMagic);
	AppendLittle(encoded, HomeBoxFormatVersion);
	AppendLittle(encoded, data.dimensions.remoteBoxCount);
	AppendLittle(encoded, data.dimensions.metadataSize);
	AppendLittle(encoded, data.dimensions.pokemonDataSize);

	for (std::uint32_t recordIndex = 0; recordIndex < data.dimensions.remoteBoxCount; ++recordIndex)
	{
		HomeBoxRecord const& record = *recordsByIndex[recordIndex];
		AppendBytes(encoded, record.metadata);
		AppendBytes(encoded, record.pokemonData);
		AppendLittle(encoded, RecordChecksum(record));
	}
	AppendLittle(encoded, FooterMagic);
	return encoded.size() == fileSize;
}

bool DecodeHomeBox(std::span<std::byte const> encoded, HomeBoxDimensions const& expected, HomeBoxData& data,
				   std::string& error)
{
	error.clear();
	if (!ValidateDimensions(expected, error))
		return false;
	if (encoded.size() > MaximumHomeBoxFileSize)
	{
		error = "Home Box file exceeds 64 MiB";
		return false;
	}
	if (encoded.size() < sizeof(std::uint32_t))
	{
		error = "Home Box file is truncated";
		return false;
	}
	std::uint32_t magic = 0;
	if (!endian::ReadLittle(encoded, 0, magic) || magic != HeaderMagic)
	{
		error = "Home Box file has an unknown magic value";
		return false;
	}
	return DecodeRecords(encoded, expected, data, error);
}

std::filesystem::path HomeBoxBackupPath(std::filesystem::path const& primary)
{
	auto path = primary;
	path += ".bak";
	return path;
}

HomeBoxLoadResult LoadHomeBoxFile(std::filesystem::path const& primary, HomeBoxDimensions const& expected)
{
	HomeBoxLoadResult result;
	std::string dimensionsError;
	if (!ValidateDimensions(expected, dimensionsError))
	{
		result.error = dimensionsError;
		return result;
	}

	std::error_code ec;
	bool const primaryExists = std::filesystem::exists(primary, ec);
	if (ec)
	{
		result.error = "cannot inspect the Home Box primary: " + ec.message();
		return result;
	}
	result.primaryMissing = !primaryExists;
	std::string primaryError;
	std::vector<std::byte> bytes;
	HomeBoxData loaded;
	std::filesystem::path loadedPath;
	if (primaryExists)
	{
		if (ReadCandidate(primary, expected, bytes, loaded, primaryError))
		{
			result.source = HomeBoxLoadSource::Primary;
			loadedPath = primary;
		}
		else
		{
			result.primaryInvalid = true;
		}
	}

	if (loadedPath.empty())
	{
		std::filesystem::path const backup = HomeBoxBackupPath(primary);
		ec.clear();
		bool const backupExists = std::filesystem::exists(backup, ec);
		if (ec)
		{
			result.error = "cannot inspect the Home Box backup: " + ec.message();
			return result;
		}
		if (backupExists)
		{
			std::string backupError;
			if (ReadCandidate(backup, expected, bytes, loaded, backupError))
			{
				result.source = HomeBoxLoadSource::Backup;
				loadedPath = backup;
				result.warning = result.primaryInvalid ? "Home Box primary is invalid; recovered from .bak. The "
														 "invalid primary will not be overwritten."
													   : "Home Box primary is missing; recovered from .bak.";
			}
			else
			{
				result.error = "Home Box backup is invalid: " + backupError;
				if (result.primaryInvalid)
					result.error = "Home Box primary is invalid: " + primaryError + "; " + result.error;
				return result;
			}
		}
		else if (result.primaryInvalid)
		{
			result.error = "Home Box primary is invalid: " + primaryError;
			return result;
		}
		else
		{
			return result;
		}
	}

	result.data = std::move(loaded);
	return result;
}

bool SaveHomeBoxFile(std::filesystem::path const& primary, HomeBoxData const& data, std::string& error)
{
	std::vector<std::byte> replacement;
	if (!EncodeHomeBox(data, replacement, error))
		return false;

	std::error_code ec;
	bool const primaryExists = std::filesystem::exists(primary, ec);
	if (ec)
	{
		error = "cannot inspect the existing Home Box file: " + ec.message();
		return false;
	}
	if (primaryExists)
	{
		std::vector<std::byte> previous;
		HomeBoxData decoded;
		if (!ReadCandidate(primary, data.dimensions, previous, decoded, error))
		{
			error = "refusing to replace an invalid Home Box primary: " + error;
			return false;
		}
		if (!platform::WriteFileAtomically(HomeBoxBackupPath(primary), previous, error))
		{
			error = "cannot retain the previous Home Box backup: " + error;
			return false;
		}
	}

	if (!platform::WriteFileAtomically(primary, replacement, error))
	{
		error = "cannot atomically save Home Box data: " + error;
		return false;
	}
	return true;
}
} // namespace rogue::storage
