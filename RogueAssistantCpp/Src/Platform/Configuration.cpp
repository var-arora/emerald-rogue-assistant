#include "Platform/Configuration.h"

#include "Platform/FileSystem.h"
#include "Platform/Utf8.h"

#include <charconv>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;

namespace rogue::platform
{
	namespace
	{
		constexpr std::uintmax_t MaximumSettingsFileSize = 64U * 1024U;

		bool ParsePort(std::string_view text, std::uint16_t& port)
		{
			if (text.empty())
				return false;

			std::uint32_t parsed = 0;
			auto const result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
			if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
				|| parsed == 0 || parsed > std::numeric_limits<std::uint16_t>::max())
			{
				return false;
			}
			port = static_cast<std::uint16_t>(parsed);
			return true;
		}

		bool IsValidJoinAddress(std::string_view value)
		{
			if (value.size() > 255 || !IsValidUtf8(value))
				return false;
			for (unsigned char character : value)
			{
				if (character < 0x20U || character == 0x7FU)
					return false;
			}
			return true;
		}
	}

	bool TrySetSetting(Settings& settings, std::string_view key, std::string_view value, std::string& error)
	{
		error.clear();
		if (key == MultiplayerHostPortKey)
		{
			std::uint16_t port = 0;
			if (!ParsePort(value, port))
			{
				error = "Multiplayer.HostPort must be an integer from 1 through 65535";
				return false;
			}
			settings.multiplayerHostPort = port;
			return true;
		}
		if (key == MultiplayerJoinIpKey)
		{
			if (!IsValidJoinAddress(value))
			{
				error = "Multiplayer.JoinIP must be valid UTF-8 without control characters and at most 255 bytes";
				return false;
			}
			settings.multiplayerJoinIp = value;
			return true;
		}
		if (key == BridgePortKey)
		{
			std::uint16_t port = 0;
			if (!ParsePort(value, port))
			{
				error = "Bridge.Port must be an integer from 1 through 65535";
				return false;
			}
			settings.bridgePort = port;
			return true;
		}

		error = "unknown settings key: " + std::string(key);
		return false;
	}

	std::string GetSetting(Settings const& settings, std::string_view key)
	{
		if (key == MultiplayerHostPortKey)
			return std::to_string(settings.multiplayerHostPort);
		if (key == MultiplayerJoinIpKey)
			return settings.multiplayerJoinIp;
		if (key == BridgePortKey)
			return std::to_string(settings.bridgePort);
		return {};
	}

	SettingsLoadResult LoadSettings(fs::path const& path)
	{
		SettingsLoadResult result;
		std::error_code ec;
		if (!fs::exists(path, ec))
		{
			if (ec)
				result.diagnostics.push_back("cannot inspect settings.ini: " + ec.message());
			return result;
		}
		result.fileFound = true;

		std::uintmax_t const fileSize = fs::file_size(path, ec);
		if (ec)
		{
			result.diagnostics.push_back("cannot determine settings.ini size: " + ec.message());
			return result;
		}
		if (fileSize > MaximumSettingsFileSize)
		{
			result.diagnostics.push_back("settings.ini exceeds 64 KiB");
			return result;
		}

		std::ifstream stream(path, std::ios::binary);
		if (!stream)
		{
			result.diagnostics.push_back("cannot open settings.ini");
			return result;
		}

		Settings parsed;
		std::set<std::string, std::less<>> seenKeys;
		std::string line;
		std::size_t lineNumber = 0;
		while (std::getline(stream, line))
		{
			++lineNumber;
			if (!line.empty() && line.back() == '\r')
				line.pop_back();
			if (line.empty() || line.front() == '#')
				continue;

			std::size_t const separator = line.find('=');
			if (separator == std::string::npos || separator == 0 || line.find('=', separator + 1) != std::string::npos)
			{
				result.diagnostics.push_back("settings.ini line " + std::to_string(lineNumber) + " is malformed");
				continue;
			}

			std::string const key = line.substr(0, separator);
			std::string_view const value(line.data() + separator + 1, line.size() - separator - 1);
			if (!seenKeys.insert(key).second)
			{
				result.diagnostics.push_back("settings.ini line " + std::to_string(lineNumber)
					+ " duplicates " + key);
				continue;
			}

			std::string error;
			if (!TrySetSetting(parsed, key, value, error))
				result.diagnostics.push_back("settings.ini line " + std::to_string(lineNumber) + ": " + error);
		}

		if (!stream.eof())
			result.diagnostics.push_back("cannot finish reading settings.ini");
		if (result.Succeeded())
		{
			result.settings = std::move(parsed);
			result.needsRewrite = seenKeys.size() != 3;
		}
		return result;
	}

	bool SaveSettings(fs::path const& path, Settings const& settings, std::string& error)
	{
		error.clear();
		if (settings.multiplayerHostPort == 0 || settings.bridgePort == 0 || !IsValidJoinAddress(settings.multiplayerJoinIp))
		{
			error = "settings contain an invalid port or join address";
			return false;
		}

		std::ostringstream contents;
		contents << MultiplayerHostPortKey << '=' << settings.multiplayerHostPort << '\n';
		contents << MultiplayerJoinIpKey << '=' << settings.multiplayerJoinIp << '\n';
		contents << BridgePortKey << '=' << settings.bridgePort << '\n';
		return WriteTextFileAtomically(path, contents.str(), error);
	}
}
