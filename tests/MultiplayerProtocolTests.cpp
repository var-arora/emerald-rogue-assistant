#include "Multiplayer/MultiplayerProtocol.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <vector>

using namespace rogue::multiplayer;

namespace
{
Hello ExampleHello()
{
	return Hello{1, 0, 3, 1, 4, 512, 24, 80, 32, 48};
}
} // namespace

TEST_CASE("multiplayer hello has a stable 40-byte little-endian encoding", "[multiplayer][protocol]")
{
	Hello const expected = ExampleHello();
	std::vector<std::byte> encoded;
	std::string error;
	REQUIRE(EncodeHello(expected, encoded, error));
	REQUIRE(encoded.size() == HelloSize);
	REQUIRE(encoded[0] == std::byte{'R'});
	REQUIRE(encoded[1] == std::byte{'A'});
	REQUIRE(encoded[2] == std::byte{'M'});
	REQUIRE(encoded[3] == std::byte{'P'});
	REQUIRE(encoded[4] == std::byte{1});
	REQUIRE(encoded[8] == std::byte{3});
	REQUIRE(encoded[12] == std::byte{1});
	REQUIRE(encoded[20] == std::byte{0});
	REQUIRE(encoded[21] == std::byte{2});
	std::array<std::byte, HelloSize> const golden{
		std::byte{0x52}, std::byte{0x41}, std::byte{0x4D}, std::byte{0x50}, std::byte{0x01}, std::byte{0x00},
		std::byte{0x00}, std::byte{0x00}, std::byte{0x03}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
		std::byte{0x01}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x04}, std::byte{0x00},
		std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02}, std::byte{0x00}, std::byte{0x00},
		std::byte{0x18}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x50}, std::byte{0x00},
		std::byte{0x00}, std::byte{0x00}, std::byte{0x20}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
		std::byte{0x30}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
	};
	REQUIRE(std::equal(encoded.begin(), encoded.end(), golden.begin(), golden.end()));

	Hello decoded;
	REQUIRE(DecodeHello(encoded, decoded, error));
	REQUIRE(decoded == expected);
}

TEST_CASE("multiplayer hello rejects malformed and unsupported inputs", "[multiplayer][protocol]")
{
	std::vector<std::byte> encoded;
	std::string error;
	REQUIRE(EncodeHello(ExampleHello(), encoded, error));
	Hello decoded;

	auto malformed = encoded;
	malformed.pop_back();
	REQUIRE_FALSE(DecodeHello(malformed, decoded, error));
	malformed = encoded;
	malformed[0] = std::byte{'X'};
	REQUIRE_FALSE(DecodeHello(malformed, decoded, error));
	malformed = encoded;
	malformed[13] = std::byte{1};
	REQUIRE_FALSE(DecodeHello(malformed, decoded, error));

	Hello unsupported = ExampleHello();
	unsupported.romAssistantApi = 2;
	REQUIRE_FALSE(EncodeHello(unsupported, encoded, error));
	unsupported = ExampleHello();
	unsupported.edition = 2;
	REQUIRE_FALSE(EncodeHello(unsupported, encoded, error));
	unsupported = ExampleHello();
	unsupported.playerCount = 0;
	REQUIRE_FALSE(EncodeHello(unsupported, encoded, error));
}

TEST_CASE("multiplayer compatibility gates protocol, ROM API, edition, and layouts", "[multiplayer][protocol]")
{
	Hello const local = ExampleHello();
	Hello remote = local;
	remote.protocolMinor = 7;
	auto compatibility = CheckCompatibility(local, remote);
	REQUIRE(compatibility.compatible);
	REQUIRE(compatibility.negotiatedMinor == 0);

	remote = local;
	remote.protocolMajor = 2;
	compatibility = CheckCompatibility(local, remote);
	REQUIRE_FALSE(compatibility.compatible);
	REQUIRE(compatibility.error.find("assistant version") != std::string::npos);

	remote = local;
	remote.edition = 0;
	REQUIRE(CheckCompatibility(local, remote).error.find("edition") != std::string::npos);
	remote = local;
	remote.romAssistantApi = 2;
	REQUIRE(CheckCompatibility(local, remote).error.find("game version") != std::string::npos);
	remote = local;
	remote.playerCount++;
	REQUIRE(CheckCompatibility(local, remote).error.find("numbers of players") != std::string::npos);
	remote = local;
	remote.handshakeSize++;
	REQUIRE(CheckCompatibility(local, remote).error.find("game versions") != std::string::npos);
	remote = local;
	remote.multiplayerStateSize++;
	REQUIRE(CheckCompatibility(local, remote).error.find("game versions") != std::string::npos);
}
