#include "DataStream.h"
#include "Endian.h"
#include "GameConnectionMessage.h"
#include "GameData.h"
#include "RogueAssistantVersion.h"
#include "RomCompatibility.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

TEST_CASE("the ROM assistant ABI has the expected fixed layout", "[characterization][rom]")
{
	STATIC_REQUIRE(sizeof(GameAddress) == 4);
	STATIC_REQUIRE(sizeof(GameStructures::GFRomHeader) == 260);
	STATIC_REQUIRE(sizeof(GameStructures::RogueAssistantHeader) == 132);
	STATIC_REQUIRE(offsetof(GameStructures::RogueAssistantHeader, rogueAssistantCompatVersion) == 4);
	STATIC_REQUIRE(offsetof(GameStructures::RogueAssistantHeader, homeBoxPtr) == 128);
	STATIC_REQUIRE(sizeof(GameStructures::RogueAssistantState) == 54);

	STATIC_REQUIRE(std::is_standard_layout_v<GameStructures::GFRomHeader>);
	STATIC_REQUIRE(std::is_trivially_copyable_v<GameStructures::GFRomHeader>);
	STATIC_REQUIRE(std::is_standard_layout_v<GameStructures::RogueAssistantHeader>);
	STATIC_REQUIRE(std::is_trivially_copyable_v<GameStructures::RogueAssistantHeader>);
}

TEST_CASE("internal game message identifiers retain their compact representation", "[characterization][messages]")
{
	STATIC_REQUIRE(sizeof(GameMessageChannel) == 2);
	STATIC_REQUIRE(sizeof(GameMessageID) == 4);

	GameMessageID const id = CreateMessageId(GameMessageChannel::CommonRead, 0x1234);
	REQUIRE(id.CompactedID == 0x12340001);
	REQUIRE(id.GetChannel() == GameMessageChannel::CommonRead);
	REQUIRE(id.GetParam16() == 0x1234);
	REQUIRE(id.GetParam8(0) == 0x34);
	REQUIRE(id.GetParam8(1) == 0x12);
}

TEST_CASE("little-endian integers have stable byte encodings", "[primitives][endian]")
{
	std::vector<u8> bytes;
	rogue::endian::AppendLittle<u16>(bytes, 0x1234);
	rogue::endian::AppendLittle<u32>(bytes, 0x89ABCDEF);
	REQUIRE(bytes == std::vector<u8>{0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x89});

	u32 decoded = 0;
	REQUIRE(rogue::endian::ReadLittle<u32>(bytes, 2, decoded));
	REQUIRE(decoded == 0x89ABCDEF);
	REQUIRE_FALSE(rogue::endian::ReadLittle<u32>(bytes, 3, decoded));
	REQUIRE(decoded == 0);

	std::array<std::byte, 4> transportBytes{};
	REQUIRE(rogue::endian::WriteLittle<u32>(transportBytes, 0, 0x12345678));
	REQUIRE(transportBytes == std::array{std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12}});
	REQUIRE(rogue::endian::ReadLittle<u32>(transportBytes, 0, decoded));
	REQUIRE(decoded == 0x12345678);
}

TEST_CASE("DataStream does not append an EOF byte and serializes little-endian values", "[primitives][stream]")
{
	DataStream output;
	u16 first = 0x1234;
	u32 second = 0x89ABCDEF;
	REQUIRE(output.Serialize(first));
	REQUIRE(output.Serialize(second));
	REQUIRE(output.GetSize() == 6);
	REQUIRE(std::vector<u8>(output.GetData(), output.GetData() + output.GetSize()) ==
			std::vector<u8>{0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x89});

	std::string const raw(reinterpret_cast<char const*>(output.GetData()), output.GetSize());
	std::istringstream input(raw);
	DataStream roundTrip(input);
	REQUIRE(roundTrip.GetSize() == output.GetSize());
	u16 decodedFirst = 0;
	u32 decodedSecond = 0;
	REQUIRE(roundTrip.Serialize(decodedFirst));
	REQUIRE(roundTrip.Serialize(decodedSecond));
	REQUIRE(decodedFirst == first);
	REQUIRE(decodedSecond == second);

	std::istringstream emptyInput;
	DataStream empty(emptyInput);
	REQUIRE(empty.GetSize() == 0);
}

TEST_CASE("application versioning is independent from ROM compatibility", "[characterization][version]")
{
	std::string expectedVersion = std::to_string(ROGUE_ASSISTANT_VERSION_MAJOR);
	expectedVersion += "." + std::to_string(ROGUE_ASSISTANT_VERSION_MINOR);
	expectedVersion += "." + std::to_string(ROGUE_ASSISTANT_VERSION_PATCH);
	if (!std::string_view(ROGUE_ASSISTANT_VERSION_PRERELEASE).empty())
		expectedVersion += "-" ROGUE_ASSISTANT_VERSION_PRERELEASE;
	REQUIRE(std::string_view(ROGUE_ASSISTANT_VERSION_STRING) == expectedVersion);
	STATIC_REQUIRE(rogue::rom::RequiredAssistantApi == 3);
	STATIC_REQUIRE(rogue::rom::IsSupportedEdition(0));
	STATIC_REQUIRE(rogue::rom::IsSupportedEdition(1));
	STATIC_REQUIRE_FALSE(rogue::rom::IsSupportedEdition(2));
}
