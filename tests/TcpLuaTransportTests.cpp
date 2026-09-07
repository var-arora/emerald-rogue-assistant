#include "Bridge/BridgeProtocol.h"
#include "Bridge/GameSession.h"
#include "Bridge/TcpLuaTransport.h"
#include "Bridge/TcpSocket.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace
{
std::vector<std::byte> Encode(rogue::bridge::Frame const& frame)
{
	std::vector<std::byte> wire;
	std::string error;
	REQUIRE(rogue::bridge::TryEncodeFrame(frame, wire, error));
	return wire;
}

std::vector<MemoryResult> Pump(TcpLuaTransport& transport)
{
	auto results = transport.PollResults();
	std::this_thread::sleep_for(1ms);
	return results;
}

std::vector<MemoryResult> SendFragmented(rogue::bridge::TcpSocket& client, TcpLuaTransport& transport,
										 std::span<std::byte const> wire, std::size_t maximumChunk)
{
	std::vector<MemoryResult> results;
	auto const deadline = std::chrono::steady_clock::now() + 2s;
	std::size_t offset = 0;
	while (offset < wire.size() && std::chrono::steady_clock::now() < deadline)
	{
		std::size_t const chunk = std::min(maximumChunk, wire.size() - offset);
		auto const sent = client.Send(wire.subspan(offset, chunk));
		if (sent.status == rogue::bridge::SocketStatus::Ok)
			offset += sent.byteCount;
		else
			REQUIRE(sent.status == rogue::bridge::SocketStatus::WouldBlock);
		auto polled = Pump(transport);
		results.insert(results.end(), std::make_move_iterator(polled.begin()), std::make_move_iterator(polled.end()));
	}
	REQUIRE(offset == wire.size());
	return results;
}

std::vector<rogue::bridge::Frame> ReceiveFrames(rogue::bridge::TcpSocket& client, TcpLuaTransport& transport,
												std::size_t expectedCount)
{
	rogue::bridge::FrameDecoder decoder;
	std::vector<rogue::bridge::Frame> frames;
	std::array<std::byte, 4096> buffer{};
	auto const deadline = std::chrono::steady_clock::now() + 2s;
	while (frames.size() < expectedCount && std::chrono::steady_clock::now() < deadline)
	{
		(void)Pump(transport);
		for (;;)
		{
			auto const received = client.Receive(buffer);
			if (received.status == rogue::bridge::SocketStatus::WouldBlock)
				break;
			if (received.status == rogue::bridge::SocketStatus::Closed)
				break;
			REQUIRE(received.status == rogue::bridge::SocketStatus::Ok);
			REQUIRE(decoder.Append(std::span(buffer).first(received.byteCount)));
			auto decoded = decoder.PollFrames();
			frames.insert(frames.end(), std::make_move_iterator(decoded.begin()),
						  std::make_move_iterator(decoded.end()));
		}
	}
	REQUIRE(frames.size() >= expectedCount);
	return frames;
}

std::vector<rogue::bridge::Frame> ReceiveFramesUntilClosed(rogue::bridge::TcpSocket& client)
{
	rogue::bridge::FrameDecoder decoder;
	std::vector<rogue::bridge::Frame> frames;
	std::array<std::byte, 4096> buffer{};
	auto const deadline = std::chrono::steady_clock::now() + 2s;
	bool closed = false;
	while (!closed && std::chrono::steady_clock::now() < deadline)
	{
		auto const received = client.Receive(buffer);
		if (received.status == rogue::bridge::SocketStatus::WouldBlock)
		{
			std::this_thread::sleep_for(1ms);
			continue;
		}
		if (received.status == rogue::bridge::SocketStatus::Closed)
		{
			closed = true;
			continue;
		}
		REQUIRE(received.status == rogue::bridge::SocketStatus::Ok);
		REQUIRE(decoder.Append(std::span(buffer).first(received.byteCount)));
		auto decoded = decoder.PollFrames();
		frames.insert(frames.end(), std::make_move_iterator(decoded.begin()), std::make_move_iterator(decoded.end()));
	}
	REQUIRE(closed);
	return frames;
}

rogue::bridge::TcpSocket ConnectClient(TcpLuaTransport& transport)
{
	rogue::bridge::TcpSocket client;
	std::string error;
	REQUIRE(client.ConnectLoopback(transport.Port(), error));
	(void)Pump(transport);
	return client;
}

void CompleteHandshake(rogue::bridge::TcpSocket& client, TcpLuaTransport& transport)
{
	(void)SendFragmented(client, transport, Encode(rogue::bridge::EncodeClientHello()), 1);
	auto frames = ReceiveFrames(client, transport, 1);
	rogue::bridge::ServerHello hello;
	std::string error;
	REQUIRE(rogue::bridge::DecodeServerHello(frames.front(), hello, error));
	REQUIRE(hello.status == rogue::bridge::HelloStatus::Accepted);
	REQUIRE(hello.protocolMajor == rogue::bridge::ProtocolMajor);
	REQUIRE(hello.protocolMinor == rogue::bridge::ProtocolMinor);
	REQUIRE(transport.State() == TransportState::Connected);
}

std::vector<MemoryResult> WaitForResults(TcpLuaTransport& transport, std::size_t expectedCount)
{
	std::vector<MemoryResult> results;
	auto const deadline = std::chrono::steady_clock::now() + 2s;
	while (results.size() < expectedCount && std::chrono::steady_clock::now() < deadline)
	{
		auto polled = transport.PollResults();
		results.insert(results.end(), std::make_move_iterator(polled.begin()), std::make_move_iterator(polled.end()));
		std::this_thread::sleep_for(1ms);
	}
	REQUIRE(results.size() == expectedCount);
	return results;
}
} // namespace

TEST_CASE("TcpLuaTransport performs handshake and matches fragmented memory results", "[bridge][tcp]")
{
	using namespace rogue::bridge;
	TcpLuaTransport transport(0);
	REQUIRE(transport.Port() != 0);
	REQUIRE(transport.State() == TransportState::Listening);
	auto client = ConnectClient(transport);
	CompleteHandshake(client, transport);

	MemoryRequest read;
	read.id = 0xA1B2C3D4U;
	read.operation = MemoryRequest::Operation::Read;
	read.address = 0x08000100U;
	read.readSize = 3;
	REQUIRE(transport.Submit(read));
	auto requests = ReceiveFrames(client, transport, 1);
	ReadRequest decodedRead;
	std::string error;
	REQUIRE(DecodeReadRequest(requests.front(), decodedRead, error));
	REQUIRE(requests.front().requestId == read.id);
	REQUIRE(decodedRead.address == read.address);
	REQUIRE(decodedRead.size == read.readSize);

	Frame result{MessageType::ReadResult, 0, read.id, {std::byte{0x11}, std::byte{0x22}, std::byte{0x33}}};
	auto results = SendFragmented(client, transport, Encode(result), 2);
	if (results.empty())
		results = WaitForResults(transport, 1);
	REQUIRE(results.front().id == read.id);
	REQUIRE(results.front().status == MemoryResult::Status::Ok);
	REQUIRE(results.front().data == result.payload);

	MemoryRequest write;
	write.id = 99;
	write.operation = MemoryRequest::Operation::Write;
	write.address = 0x02000001U;
	write.data = {std::byte{0xAA}, std::byte{0xBB}, std::byte{0xCC}};
	REQUIRE(transport.Submit(write));
	requests = ReceiveFrames(client, transport, 1);
	WriteRequest decodedWrite;
	REQUIRE(DecodeWriteRequest(requests.front(), decodedWrite, error));
	REQUIRE(decodedWrite.address == write.address);
	REQUIRE(decodedWrite.data == write.data);
	results = SendFragmented(client, transport, Encode({MessageType::WriteResult, 0, write.id, {}}), 1);
	if (results.empty())
		results = WaitForResults(transport, 1);
	REQUIRE(results.front().id == write.id);
	REQUIRE(results.front().status == MemoryResult::Status::Ok);
	REQUIRE(results.front().data.empty());
}

TEST_CASE("TcpLuaTransport rejects a second mGBA peer as busy", "[bridge][tcp]")
{
	using namespace rogue::bridge;
	TcpLuaTransport transport(0);
	auto primary = ConnectClient(transport);
	CompleteHandshake(primary, transport);
	auto second = ConnectClient(transport);
	auto frames = ReceiveFrames(second, transport, 3);

	ServerHello hello;
	ErrorMessage message;
	std::string error;
	REQUIRE(DecodeServerHello(frames[0], hello, error));
	REQUIRE(hello.status == HelloStatus::Rejected);
	REQUIRE(DecodeErrorMessage(frames[1], message, error));
	REQUIRE(message.code == ProtocolErrorCode::Busy);
	REQUIRE(frames[2].type == MessageType::Close);
	REQUIRE(transport.State() == TransportState::Connected);
}

TEST_CASE("TcpLuaTransport rejects incompatible bridge handshakes", "[bridge][tcp]")
{
	using namespace rogue::bridge;
	TcpLuaTransport transport(0);
	auto client = ConnectClient(transport);
	(void)SendFragmented(client, transport, Encode(EncodeClientHello({2, 0, ScriptVersion})), 3);
	auto frames = ReceiveFrames(client, transport, 3);

	ServerHello hello;
	ErrorMessage message;
	std::string error;
	REQUIRE(DecodeServerHello(frames[0], hello, error));
	REQUIRE(hello.status == HelloStatus::Rejected);
	REQUIRE(DecodeErrorMessage(frames[1], message, error));
	REQUIRE(message.code == ProtocolErrorCode::UnsupportedProtocol);
	REQUIRE(frames[2].type == MessageType::Close);
	REQUIRE(transport.State() == TransportState::Listening);
}

TEST_CASE("TcpLuaTransport prioritizes the close frame during shutdown", "[bridge][tcp][shutdown]")
{
	using namespace rogue::bridge;
	TcpLuaTransport transport(0);
	auto client = ConnectClient(transport);
	CompleteHandshake(client, transport);

	auto submitLargeWrite = [&transport](MemoryRequestId id)
	{
		MemoryRequest request;
		request.id = id;
		request.operation = MemoryRequest::Operation::Write;
		request.address = 0x02000000U;
		request.data.resize(200U * 1024U, std::byte{0x5A});
		return transport.Submit(std::move(request));
	};
	REQUIRE(submitLargeWrite(1));
	REQUIRE(submitLargeWrite(2));

	transport.Stop();
	auto const frames = ReceiveFramesUntilClosed(client);
	REQUIRE(std::ranges::any_of(frames, [](Frame const& frame) { return frame.type == MessageType::Close; }));
}

TEST_CASE("TcpLuaTransport remains bounded while mGBA is paused and then reconnects", "[bridge][tcp]")
{
	using namespace rogue::bridge;
	TcpLuaTransport transport(0);
	auto client = ConnectClient(transport);
	CompleteHandshake(client, transport);
	GameSession session(std::shared_ptr<IGameMemoryTransport>(&transport, [](IGameMemoryTransport*) {}));
	std::size_t disconnected = 0;
	for (std::size_t index = 0; index < MaximumOutstandingMemoryRequests; ++index)
	{
		REQUIRE(session.Read(0x02000000U, 1, [&](MemoryResult const& result) {
			if (result.status == MemoryResult::Status::Disconnected)
				++disconnected;
		}));
	}
	REQUIRE_FALSE(session.CanSubmit());
	for (int index = 0; index < 10; ++index)
	{
		session.Poll();
		std::this_thread::sleep_for(2ms);
	}
	REQUIRE(transport.State() == TransportState::Connected);
	REQUIRE(session.OutstandingRequestCount() == MaximumOutstandingMemoryRequests);

	client.Close();
	auto const deadline = std::chrono::steady_clock::now() + 2s;
	while (transport.State() != TransportState::Listening && std::chrono::steady_clock::now() < deadline)
	{
		session.Poll();
		std::this_thread::sleep_for(1ms);
	}
	REQUIRE(transport.State() == TransportState::Listening);
	session.Poll();
	REQUIRE(disconnected == MaximumOutstandingMemoryRequests);

	auto replacement = ConnectClient(transport);
	CompleteHandshake(replacement, transport);
	REQUIRE(transport.State() == TransportState::Connected);
	session.Stop();
}
