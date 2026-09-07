#include "Bridge/GameMemoryTransport.h"
#include "Bridge/GameSession.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace
{
class FakeTransport final : public IGameMemoryTransport
{
  public:
	bool Submit(MemoryRequest request) override
	{
		if (!acceptRequests || state != TransportState::Connected)
			return false;
		submitted.push_back(std::move(request));
		return true;
	}

	std::vector<MemoryResult> PollResults() override
	{
		auto output = std::move(results);
		results.clear();
		return output;
	}

	TransportState State() const override
	{
		return state;
	}
	void Stop() override
	{
		state = TransportState::Stopped;
	}

	TransportState state = TransportState::Connected;
	bool acceptRequests = true;
	std::vector<MemoryRequest> submitted;
	std::vector<MemoryResult> results;
};

std::vector<std::byte> Bytes(std::initializer_list<unsigned char> values)
{
	std::vector<std::byte> bytes;
	bytes.reserve(values.size());
	for (unsigned char value : values)
		bytes.push_back(static_cast<std::byte>(value));
	return bytes;
}
} // namespace

TEST_CASE("memory request validation enforces GBA ranges and bounded payloads", "[transport][validation]")
{
	MemoryRequest request;
	request.id = 1;
	request.operation = MemoryRequest::Operation::Read;
	request.address = 0x08000100;
	request.readSize = 132;
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::Ok);

	request.address = 0x0203FFF0;
	request.readSize = 16;
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::Ok);
	request.readSize = 17;
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::InvalidAddress);
	request.address = 0xFFFFFFF0;
	request.readSize = 32;
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::InvalidAddress);
	request.address = 0x08000000;
	request.readSize = MaximumMemoryPayloadSize + 1;
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::InvalidSize);

	request.operation = MemoryRequest::Operation::Write;
	request.address = 0x02000001;
	request.readSize = 0;
	request.data = Bytes({1, 2, 3});
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::Ok);
	request.address = 0x08000000;
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::InvalidAddress);
	request.address = 0x03007FFF;
	request.data = Bytes({1, 2});
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::InvalidAddress);

	request.id = 0;
	REQUIRE(ValidateMemoryRequest(request) == MemoryResult::Status::ProtocolError);
}

TEST_CASE("GameSession matches out-of-order results by request ID on polling", "[transport][session]")
{
	auto transport = std::make_shared<FakeTransport>();
	GameSession session(transport);
	std::vector<int> completionOrder;
	std::vector<std::byte> firstData;
	std::vector<std::byte> secondData;

	REQUIRE(session.Read(0x02000000, 2, [&](MemoryResult result) {
		completionOrder.push_back(1);
		REQUIRE(result.status == MemoryResult::Status::Ok);
		firstData = std::move(result.data);
	}));
	REQUIRE(session.Read(0x02000010, 3, [&](MemoryResult result) {
		completionOrder.push_back(2);
		REQUIRE(result.status == MemoryResult::Status::Ok);
		secondData = std::move(result.data);
	}));

	REQUIRE(transport->submitted.size() == 2);
	auto const firstId = transport->submitted[0].id;
	auto const secondId = transport->submitted[1].id;
	REQUIRE(firstId != secondId);
	transport->results.push_back(MemoryResult{secondId, MemoryResult::Status::Ok, Bytes({3, 4, 5})});
	transport->results.push_back(MemoryResult{firstId, MemoryResult::Status::Ok, Bytes({1, 2})});
	REQUIRE(completionOrder.empty());

	session.Poll();
	REQUIRE(completionOrder == std::vector<int>{2, 1});
	REQUIRE(firstData == Bytes({1, 2}));
	REQUIRE(secondData == Bytes({3, 4, 5}));
	REQUIRE(session.OutstandingRequestCount() == 0);
}

TEST_CASE("GameSession bounds outstanding work and validates result shapes", "[transport][session]")
{
	auto transport = std::make_shared<FakeTransport>();
	GameSession session(transport);
	std::size_t disconnectedCompletions = 0;
	for (std::size_t index = 0; index < MaximumOutstandingMemoryRequests; ++index)
	{
		REQUIRE(session.Read(0x02000000, 1, [&](MemoryResult const& result) {
			if (result.status == MemoryResult::Status::Disconnected)
				++disconnectedCompletions;
		}));
	}
	REQUIRE_FALSE(session.CanSubmit());
	REQUIRE_FALSE(session.Read(0x02000000, 1, [](MemoryResult const&) {}));
	REQUIRE(transport->submitted.size() == MaximumOutstandingMemoryRequests);

	auto const firstId = transport->submitted.front().id;
	transport->results.push_back(MemoryResult{firstId, MemoryResult::Status::Ok, Bytes({1, 2})});
	session.Poll();
	REQUIRE(session.ProtocolErrorCount() == 1);
	REQUIRE(session.CanSubmit());

	transport->results.push_back(MemoryResult{0xFFFFFFFF, MemoryResult::Status::Ok, {}});
	session.Poll();
	REQUIRE(session.ProtocolErrorCount() == 2);

	session.Stop();
	REQUIRE(session.State() == TransportState::Connected);
	REQUIRE(disconnectedCompletions == MaximumOutstandingMemoryRequests - 1);
	REQUIRE(session.OutstandingRequestCount() == 0);
	transport->Stop();
	REQUIRE(session.State() == TransportState::Stopped);
}

TEST_CASE("invalid requests complete locally without reaching the transport", "[transport][session]")
{
	auto transport = std::make_shared<FakeTransport>();
	GameSession session(transport);
	MemoryResult::Status status = MemoryResult::Status::Ok;
	REQUIRE_FALSE(
		session.Write(0x08000000, Bytes({1}), [&](MemoryResult const& result) { status = result.status; }));
	REQUIRE(status == MemoryResult::Status::InvalidAddress);
	REQUIRE(transport->submitted.empty());
	REQUIRE(session.OutstandingRequestCount() == 0);
}
