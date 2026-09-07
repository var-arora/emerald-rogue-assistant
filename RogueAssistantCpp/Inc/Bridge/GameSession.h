#pragma once

#include "Bridge/GameMemoryTransport.h"

#include <functional>
#include <memory>
#include <span>
#include <unordered_map>

class GameSession
{
  public:
	using Completion = std::function<void(MemoryResult)>;

	explicit GameSession(std::shared_ptr<IGameMemoryTransport> transport);
	~GameSession();

	GameSession(GameSession const&) = delete;
	GameSession& operator=(GameSession const&) = delete;

	bool Read(GameAddress address, std::uint32_t size, Completion completion);
	bool Write(GameAddress address, std::span<std::byte const> data, Completion completion);
	void Poll();
	void Stop();

	[[nodiscard]] bool CanSubmit() const;
	[[nodiscard]] std::size_t OutstandingRequestCount() const
	{
		return m_Pending.size();
	}
	[[nodiscard]] TransportState State() const
	{
		return m_Transport->State();
	}
	[[nodiscard]] std::size_t ProtocolErrorCount() const
	{
		return m_ProtocolErrorCount;
	}

  private:
	struct PendingRequest
	{
		MemoryRequest::Operation operation;
		std::uint32_t expectedSize;
		Completion completion;
	};

	bool Submit(MemoryRequest request, Completion completion);
	MemoryRequestId AllocateRequestId();

	std::shared_ptr<IGameMemoryTransport> m_Transport;
	std::unordered_map<MemoryRequestId, PendingRequest> m_Pending;
	MemoryRequestId m_NextRequestId = 1;
	std::size_t m_ProtocolErrorCount = 0;
	bool m_Stopped = false;
};
