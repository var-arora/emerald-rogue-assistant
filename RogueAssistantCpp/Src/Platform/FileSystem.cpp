#include "Platform/FileSystem.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <limits>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace rogue::platform
{
	namespace
	{
		std::atomic_uint64_t TemporaryFileSequence = 0;

		fs::path MakeTemporarySibling(fs::path const& destination)
		{
			auto const timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
			auto const sequence = TemporaryFileSequence.fetch_add(1, std::memory_order_relaxed);
			fs::path temporary = destination;
			temporary += ".tmp.";
			temporary += std::to_string(timestamp);
			temporary += ".";
			temporary += std::to_string(sequence);
			return temporary;
		}

		bool ReplaceFile(fs::path const& temporary, fs::path const& destination, std::string& error)
		{
#if defined(_WIN32)
			if (MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
				return true;
			error = "cannot replace destination file (Windows error " + std::to_string(GetLastError()) + ")";
			return false;
#else
			std::error_code ec;
			fs::rename(temporary, destination, ec);
			if (!ec)
				return true;
			error = "cannot replace destination file: " + ec.message();
			return false;
#endif
		}
	}

	bool EnsureDirectory(fs::path const& directory, std::string& error)
	{
		error.clear();
		if (directory.empty())
			return true;

		std::error_code ec;
		fs::create_directories(directory, ec);
		if (!ec && fs::is_directory(directory, ec))
			return true;
		error = "cannot create directory: " + ec.message();
		return false;
	}

	bool ReadFile(fs::path const& source, std::size_t maximumSize, std::vector<std::byte>& bytes, std::string& error)
	{
		bytes.clear();
		error.clear();
		std::error_code ec;
		std::uintmax_t const fileSize = fs::file_size(source, ec);
		if (ec)
		{
			error = "cannot determine file size: " + ec.message();
			return false;
		}
		if (fileSize > maximumSize ||
			fileSize > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
		{
			error = "file exceeds its size limit";
			return false;
		}

		std::ifstream stream(source, std::ios::binary);
		if (!stream)
		{
			error = "cannot open file for reading";
			return false;
		}
		bytes.resize(static_cast<std::size_t>(fileSize));
		if (!bytes.empty())
			stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		if (!stream || stream.peek() != std::ifstream::traits_type::eof())
		{
			bytes.clear();
			error = "cannot read file exactly";
			return false;
		}
		return true;
	}

	bool ReadTextFile(fs::path const& source, std::size_t maximumSize, std::string& text, std::string& error)
	{
		std::vector<std::byte> bytes;
		if (!ReadFile(source, maximumSize, bytes, error))
		{
			text.clear();
			return false;
		}
		if (bytes.empty())
			text.clear();
		else
			text.assign(reinterpret_cast<char const*>(bytes.data()), bytes.size());
		return true;
	}

	bool WriteFileAtomically(fs::path const& destination, std::span<std::byte const> bytes, std::string& error)
	{
		error.clear();
		if (!EnsureDirectory(destination.parent_path(), error))
			return false;

		fs::path const temporary = MakeTemporarySibling(destination);
		if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
		{
			error = "file is too large to write";
			return false;
		}
		{
			std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
			if (!stream)
			{
				error = "cannot open temporary file for writing";
				return false;
			}
			if (!bytes.empty())
			{
				stream.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
			}
			stream.flush();
			if (!stream)
			{
				error = "cannot write temporary file";
				stream.close();
				std::error_code ignored;
				fs::remove(temporary, ignored);
				return false;
			}
		}

		if (ReplaceFile(temporary, destination, error))
			return true;

		std::error_code ignored;
		fs::remove(temporary, ignored);
		return false;
	}

	bool WriteTextFileAtomically(fs::path const& destination, std::string_view text, std::string& error)
	{
		auto const* data = reinterpret_cast<std::byte const*>(text.data());
		return WriteFileAtomically(destination, std::span<std::byte const>(data, text.size()), error);
	}
}
