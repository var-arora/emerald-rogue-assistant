#pragma once
#include "Defines.h"

#include <cstddef>

enum class GameMessageChannel : u16
{
	Anonymous,
	CommonRead,
};

struct GameMessageID
{
	u32 CompactedID = 0;

	[[nodiscard]] constexpr GameMessageChannel GetChannel() const
	{
		return static_cast<GameMessageChannel>(CompactedID & 0xFFFFU);
	}

	[[nodiscard]] constexpr u16 GetParam16() const
	{
		return static_cast<u16>(CompactedID >> 16U);
	}

	[[nodiscard]] constexpr u8 GetParam8(std::size_t index) const
	{
		return index < 2 ? static_cast<u8>(GetParam16() >> (index * 8U)) : 0;
	}
};

static_assert(sizeof(GameMessageID) == sizeof(u32));

[[nodiscard]] constexpr GameMessageID CreateMessageId(GameMessageChannel channel, u16 param = 0)
{
	return GameMessageID{
		static_cast<u32>(static_cast<u16>(channel)) | (static_cast<u32>(param) << 16U)};
}

[[nodiscard]] constexpr GameMessageID CreateAnonymousMessageId()
{
	return CreateMessageId(GameMessageChannel::Anonymous);
}
