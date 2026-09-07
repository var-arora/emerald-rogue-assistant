#pragma once

#include "GameData.h"

#include <cstddef>
#include <cstdint>
#include <vector>

using MemoryRequestId = std::uint32_t;

enum class TransportState
{
	Stopped,
	Disconnected,
	Listening,
	Connected,
};

struct MemoryRequest
{
	MemoryRequestId id = 0;

	enum class Operation
	{
		Read,
		Write,
	};

	Operation operation = Operation::Read;
	GameAddress address = 0;
	std::vector<std::byte> data;
	std::uint32_t readSize = 0;
};

struct MemoryResult
{
	MemoryRequestId id = 0;

	enum class Status
	{
		Ok,
		InvalidAddress,
		InvalidSize,
		Disconnected,
		ProtocolError,
	};

	Status status = Status::ProtocolError;
	std::vector<std::byte> data;
};

class IGameMemoryTransport
{
  public:
	virtual ~IGameMemoryTransport() = default;
	virtual bool Submit(MemoryRequest request) = 0;
	virtual std::vector<MemoryResult> PollResults() = 0;
	virtual TransportState State() const = 0;
	virtual void Stop() = 0;
};

inline constexpr std::size_t MaximumOutstandingMemoryRequests = 256;
inline constexpr std::uint32_t MaximumMemoryPayloadSize = 1024U * 1024U;

[[nodiscard]] MemoryResult::Status ValidateMemoryRequest(MemoryRequest const& request);
