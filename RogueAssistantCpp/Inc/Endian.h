#pragma once

#include "Defines.h"

#include <bit>
#include <cstddef>
#include <span>
#include <type_traits>
#include <vector>

namespace rogue::endian
{
	template <typename T>
	concept Integer = std::is_integral_v<T> && !std::is_same_v<T, bool>;

	template <Integer T>
	bool ReadLittle(std::span<u8 const> bytes, std::size_t offset, T& value)
	{
		if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
		{
			value = T{};
			return false;
		}

		using UnsignedT = std::make_unsigned_t<T>;
		UnsignedT encoded = 0;
		for (std::size_t index = 0; index < sizeof(T); ++index)
		{
			encoded |= static_cast<UnsignedT>(bytes[offset + index]) << (index * 8U);
		}

		if constexpr (std::is_signed_v<T>)
		{
			value = std::bit_cast<T>(encoded);
		}
		else
		{
			value = encoded;
		}

		return true;
	}

	template <Integer T>
	bool ReadLittle(std::span<std::byte const> bytes, std::size_t offset, T& value)
	{
		if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
		{
			value = T{};
			return false;
		}

		using UnsignedT = std::make_unsigned_t<T>;
		UnsignedT encoded = 0;
		for (std::size_t index = 0; index < sizeof(T); ++index)
		{
			encoded |= std::to_integer<UnsignedT>(bytes[offset + index]) << (index * 8U);
		}

		if constexpr (std::is_signed_v<T>)
			value = std::bit_cast<T>(encoded);
		else
			value = encoded;
		return true;
	}

	template <Integer T>
	bool WriteLittle(std::span<u8> bytes, std::size_t offset, T value)
	{
		if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
		{
			return false;
		}

		using UnsignedT = std::make_unsigned_t<T>;
		UnsignedT const encoded = [&value]() {
			if constexpr (std::is_signed_v<T>)
			{
				return std::bit_cast<UnsignedT>(value);
			}
			else
			{
				return value;
			}
		}();

		for (std::size_t index = 0; index < sizeof(T); ++index)
		{
			bytes[offset + index] = static_cast<u8>(encoded >> (index * 8U));
		}
		return true;
	}

	template <Integer T>
	bool WriteLittle(std::span<std::byte> bytes, std::size_t offset, T value)
	{
		if (offset > bytes.size() || sizeof(T) > bytes.size() - offset)
			return false;

		using UnsignedT = std::make_unsigned_t<T>;
		UnsignedT const encoded = [&value]() {
			if constexpr (std::is_signed_v<T>)
				return std::bit_cast<UnsignedT>(value);
			else
				return value;
		}();

		for (std::size_t index = 0; index < sizeof(T); ++index)
			bytes[offset + index] = static_cast<std::byte>(encoded >> (index * 8U));
		return true;
	}

	template <Integer T>
	void AppendLittle(std::vector<u8>& bytes, T value)
	{
		std::size_t const offset = bytes.size();
		bytes.resize(offset + sizeof(T));
		[[maybe_unused]] bool const wrote = WriteLittle<T>(bytes, offset, value);
	}
}
