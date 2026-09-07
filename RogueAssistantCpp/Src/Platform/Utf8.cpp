#include "Platform/Utf8.h"

#include <cstdint>

namespace rogue::platform
{
	bool IsValidUtf8(std::string_view text)
	{
		std::size_t index = 0;
		while (index < text.size())
		{
			std::uint8_t const lead = static_cast<std::uint8_t>(text[index]);
			if (lead <= 0x7F)
			{
				++index;
				continue;
			}

			std::size_t continuationCount = 0;
			std::uint32_t codePoint = 0;
			std::uint32_t minimum = 0;
			if ((lead & 0xE0U) == 0xC0U)
			{
				continuationCount = 1;
				codePoint = lead & 0x1FU;
				minimum = 0x80;
			}
			else if ((lead & 0xF0U) == 0xE0U)
			{
				continuationCount = 2;
				codePoint = lead & 0x0FU;
				minimum = 0x800;
			}
			else if ((lead & 0xF8U) == 0xF0U)
			{
				continuationCount = 3;
				codePoint = lead & 0x07U;
				minimum = 0x10000;
			}
			else
			{
				return false;
			}

			if (continuationCount > text.size() - index - 1)
				return false;

			for (std::size_t offset = 1; offset <= continuationCount; ++offset)
			{
				std::uint8_t const continuation = static_cast<std::uint8_t>(text[index + offset]);
				if ((continuation & 0xC0U) != 0x80U)
					return false;
				codePoint = (codePoint << 6U) | (continuation & 0x3FU);
			}

			if (codePoint < minimum || codePoint > 0x10FFFFU
				|| (codePoint >= 0xD800U && codePoint <= 0xDFFFU))
			{
				return false;
			}
			index += continuationCount + 1;
		}
		return true;
	}

	std::filesystem::path PathFromUtf8(std::string_view text)
	{
		std::u8string const encoded(text.begin(), text.end());
		return std::filesystem::path(encoded);
	}

	std::string PathToUtf8(std::filesystem::path const& path)
	{
		auto const encoded = path.generic_u8string();
		return std::string(reinterpret_cast<char const*>(encoded.data()), encoded.size());
	}
}
