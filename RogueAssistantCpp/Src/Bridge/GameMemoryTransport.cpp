#include "Bridge/GameMemoryTransport.h"

#include <array>

namespace
{
struct AddressRange
{
	std::uint64_t begin;
	std::uint64_t end;
};

constexpr AddressRange Ewram{0x02000000ULL, 0x02040000ULL};
constexpr AddressRange Iwram{0x03000000ULL, 0x03008000ULL};
constexpr std::array ReadableRanges{
	Ewram,
	Iwram,
	AddressRange{0x05000000ULL, 0x05000400ULL},
	AddressRange{0x06000000ULL, 0x06018000ULL},
	AddressRange{0x07000000ULL, 0x07000400ULL},
	AddressRange{0x08000000ULL, 0x0E000000ULL},
};
constexpr std::array WritableRanges{Ewram, Iwram};

template <std::size_t Size>
bool IsRangeAllowed(GameAddress address, std::uint32_t size, std::array<AddressRange, Size> const& ranges)
{
	std::uint64_t const begin = address;
	std::uint64_t const end = begin + size;
	if (end > (std::uint64_t{1} << 32U))
		return false;

	for (AddressRange const& range : ranges)
	{
		if (begin >= range.begin && end <= range.end)
			return true;
	}
	return false;
}
} // namespace

MemoryResult::Status ValidateMemoryRequest(MemoryRequest const& request)
{
	if (request.id == 0)
		return MemoryResult::Status::ProtocolError;

	if (request.operation == MemoryRequest::Operation::Read)
	{
		if (!request.data.empty() || request.readSize == 0 || request.readSize > MaximumMemoryPayloadSize)
			return MemoryResult::Status::InvalidSize;
		return IsRangeAllowed(request.address, request.readSize, ReadableRanges) ? MemoryResult::Status::Ok
																				 : MemoryResult::Status::InvalidAddress;
	}

	if (request.readSize != 0 || request.data.empty() || request.data.size() > MaximumMemoryPayloadSize)
		return MemoryResult::Status::InvalidSize;
	return IsRangeAllowed(request.address, static_cast<std::uint32_t>(request.data.size()), WritableRanges)
			   ? MemoryResult::Status::Ok
			   : MemoryResult::Status::InvalidAddress;
}
