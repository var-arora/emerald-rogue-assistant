#include "Bridge/BridgeProtocol.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
std::vector<std::byte> ParseHex(std::string_view text)
{
	REQUIRE(text.size() % 2 == 0);
	auto nibble = [](char character) -> std::uint8_t {
		if (character >= '0' && character <= '9')
			return static_cast<std::uint8_t>(character - '0');
		if (character >= 'a' && character <= 'f')
			return static_cast<std::uint8_t>(character - 'a' + 10);
		FAIL("invalid golden-vector hex digit");
	};

	std::vector<std::byte> bytes;
	bytes.reserve(text.size() / 2);
	for (std::size_t index = 0; index < text.size(); index += 2)
		bytes.push_back(static_cast<std::byte>((nibble(text[index]) << 4U) | nibble(text[index + 1])));
	return bytes;
}

std::map<std::string, std::vector<std::byte>> LoadGoldenVectors()
{
	std::ifstream input(std::filesystem::path(ROGUE_TEST_FIXTURE_DIR) / "bridge_protocol_1.golden");
	REQUIRE(input.is_open());

	std::map<std::string, std::vector<std::byte>> vectors;
	std::string line;
	while (std::getline(input, line))
	{
		if (line.empty() || line.front() == '#')
			continue;
		auto const separator = line.find('=');
		REQUIRE(separator != std::string::npos);
		vectors.emplace(line.substr(0, separator), ParseHex(std::string_view(line).substr(separator + 1)));
	}
	return vectors;
}

std::vector<std::byte> Encode(rogue::bridge::Frame const& frame)
{
	std::vector<std::byte> bytes;
	std::string error;
	REQUIRE(rogue::bridge::TryEncodeFrame(frame, bytes, error));
	REQUIRE(error.empty());
	return bytes;
}
} // namespace

TEST_CASE("bridge protocol encodings match the shared C++ and Lua vectors", "[bridge][golden]")
{
	using namespace rogue::bridge;
	auto const vectors = LoadGoldenVectors();
	REQUIRE(Encode(EncodeClientHello()) == vectors.at("client_hello"));
	REQUIRE(Encode(EncodeServerHello({})) == vectors.at("server_hello"));
	REQUIRE(Encode(EncodeReadRequest(0x11223344U, {0x08000100U, 32U})) == vectors.at("read_request"));

	WriteRequest write{0x02000000U, {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}}};
	Frame writeFrame;
	std::string error;
	REQUIRE(TryEncodeWriteRequest(0x01020304U, write, writeFrame, error));
	REQUIRE(Encode(writeFrame) == vectors.at("write_request"));

	Frame writeResult{MessageType::WriteResult, 0, 0x01020304U, {}};
	REQUIRE(Encode(writeResult) == vectors.at("write_result"));
	REQUIRE(Encode(EncodeClose()) == vectors.at("close"));
}

TEST_CASE("bridge decoder buffers fragmented and coalesced TCP frames", "[bridge]")
{
	using namespace rogue::bridge;
	auto const vectors = LoadGoldenVectors();
	std::vector<std::byte> wire = vectors.at("client_hello");
	auto const& read = vectors.at("read_request");
	wire.insert(wire.end(), read.begin(), read.end());

	FrameDecoder decoder;
	for (std::byte byte : std::span(wire).first(vectors.at("client_hello").size()))
		REQUIRE(decoder.Append(std::span(&byte, 1)));
	REQUIRE(decoder.QueuedFrameCount() == 1);
	REQUIRE(decoder.Append(std::span(wire).subspan(vectors.at("client_hello").size())));

	auto frames = decoder.PollFrames();
	REQUIRE(frames.size() == 2);
	ClientHello hello;
	std::string error;
	REQUIRE(DecodeClientHello(frames[0], hello, error));
	REQUIRE(hello.protocolMajor == ProtocolMajor);
	REQUIRE(hello.protocolMinor == ProtocolMinor);
	REQUIRE(hello.scriptVersion == ScriptVersion);
	ReadRequest request;
	REQUIRE(DecodeReadRequest(frames[1], request, error));
	REQUIRE(request.address == 0x08000100U);
	REQUIRE(request.size == 32U);
	REQUIRE(decoder.QueuedFrameCount() == 0);
}

TEST_CASE("bridge typed payloads reject inconsistent lengths and invalid UTF-8", "[bridge]")
{
	using namespace rogue::bridge;
	std::string error;
	WriteRequest write;
	Frame frame{MessageType::WriteRequest, 0, 1, ParseHex("0000000203000000aabb")};
	REQUIRE_FALSE(DecodeWriteRequest(frame, write, error));
	REQUIRE(error.find("does not match") != std::string::npos);

	ErrorMessage message{ProtocolErrorCode::InvalidSize, std::string("bad\xC0\x80", 5)};
	REQUIRE_FALSE(TryEncodeError(1, message, frame, error));
	REQUIRE(error.find("UTF-8") != std::string::npos);

	message.diagnostic.assign(MaximumDiagnosticLength + 1, 'x');
	REQUIRE_FALSE(TryEncodeError(1, message, frame, error));

	message = {};
	frame = {MessageType::Error, 0, 1, ParseHex("00000000")};
	REQUIRE_FALSE(DecodeErrorMessage(frame, message, error));
	REQUIRE(error == "unknown Error code");
}

TEST_CASE("bridge decoder fails closed on malformed frame headers", "[bridge]")
{
	using namespace rogue::bridge;
	auto const vectors = LoadGoldenVectors();

	SECTION("short body")
	{
		FrameDecoder decoder;
		auto wire = ParseHex("07000000");
		REQUIRE_FALSE(decoder.Append(wire));
		REQUIRE(decoder.Error() == DecodeError::BodyTooSmall);
	}
	SECTION("oversized body")
	{
		FrameDecoder decoder;
		auto wire = ParseHex("01001000");
		REQUIRE_FALSE(decoder.Append(wire));
		REQUIRE(decoder.Error() == DecodeError::FrameTooLarge);
	}
	SECTION("flags")
	{
		FrameDecoder decoder;
		auto wire = vectors.at("close");
		wire[5] = std::byte{1};
		REQUIRE_FALSE(decoder.Append(wire));
		REQUIRE(decoder.Error() == DecodeError::UnsupportedFlags);
	}
	SECTION("reserved")
	{
		FrameDecoder decoder;
		auto wire = vectors.at("close");
		wire[6] = std::byte{1};
		REQUIRE_FALSE(decoder.Append(wire));
		REQUIRE(decoder.Error() == DecodeError::NonzeroReserved);
	}
	SECTION("message type")
	{
		FrameDecoder decoder;
		auto wire = vectors.at("close");
		wire[4] = std::byte{0xFF};
		REQUIRE_FALSE(decoder.Append(wire));
		REQUIRE(decoder.Error() == DecodeError::InvalidMessageType);
	}
	SECTION("request ID")
	{
		FrameDecoder decoder;
		auto wire = vectors.at("close");
		wire[8] = std::byte{1};
		REQUIRE_FALSE(decoder.Append(wire));
		REQUIRE(decoder.Error() == DecodeError::InvalidRequestId);
	}
}

TEST_CASE("bridge decoder bounds both frame count and queued bytes", "[bridge]")
{
	using namespace rogue::bridge;
	auto const close = LoadGoldenVectors().at("close");
	FrameDecoder decoder;
	for (std::size_t index = 0; index < MaximumQueuedFrames; ++index)
		REQUIRE(decoder.Append(close));
	REQUIRE_FALSE(decoder.Append(close));
	REQUIRE(decoder.Error() == DecodeError::QueueOverflow);
	REQUIRE(decoder.QueuedFrameCount() == MaximumQueuedFrames);

	decoder.Reset();
	REQUIRE(decoder.Error() == DecodeError::None);
	REQUIRE(decoder.QueuedFrameCount() == 0);
	REQUIRE(decoder.Append(close));

	decoder.Reset();
	Frame large{MessageType::ReadResult, 0, 1,
				std::vector<std::byte>(MaximumFrameBodyLength - FrameBodyHeaderSize, std::byte{0x55})};
	auto const largeWire = Encode(large);
	REQUIRE(decoder.Append(largeWire));
	REQUIRE(decoder.Append(largeWire));
	REQUIRE(decoder.Append(largeWire));
	REQUIRE_FALSE(decoder.Append(largeWire));
	REQUIRE(decoder.Error() == DecodeError::QueueOverflow);
}

TEST_CASE("bridge writer retains unsent suffixes across partial sends", "[bridge]")
{
	using namespace rogue::bridge;
	FrameWriter writer;
	std::string error;
	REQUIRE(writer.Queue(EncodeClientHello(), error));
	REQUIRE(writer.Queue(EncodeReadRequest(7, {0x08000000U, 4}), error));
	std::size_t const originalBytes = writer.QueuedByteCount();

	FrameDecoder peer;
	std::array<std::size_t, 5> const sendSizes{1, 3, 2, 7, 5};
	std::size_t sendIndex = 0;
	while (!writer.PendingBytes().empty())
	{
		auto const pending = writer.PendingBytes();
		std::size_t const count = std::min(sendSizes[sendIndex++ % sendSizes.size()], pending.size());
		REQUIRE(peer.Append(pending.first(count)));
		REQUIRE(writer.ConsumeSent(count));
	}
	REQUIRE(writer.QueuedByteCount() == 0);
	REQUIRE(writer.QueuedFrameCount() == 0);
	REQUIRE_FALSE(writer.ConsumeSent(1));
	REQUIRE(originalBytes > 0);
	REQUIRE(peer.PollFrames().size() == 2);
}

TEST_CASE("bridge writer applies bounded backpressure without dropping queued frames", "[bridge]")
{
	using namespace rogue::bridge;
	FrameWriter writer;
	std::string error;
	for (std::size_t index = 0; index < MaximumQueuedFrames; ++index)
		REQUIRE(writer.Queue(EncodeClose(), error));
	REQUIRE_FALSE(writer.Queue(EncodeClose(), error));
	REQUIRE(writer.QueuedFrameCount() == MaximumQueuedFrames);
	REQUIRE(error.find("full") != std::string::npos);
	writer.Reset();
	REQUIRE(writer.QueuedFrameCount() == 0);
}
