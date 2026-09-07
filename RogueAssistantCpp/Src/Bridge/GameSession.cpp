#include "Bridge/GameSession.h"

#include <stdexcept>
#include <utility>
#include <vector>

GameSession::GameSession(std::shared_ptr<IGameMemoryTransport> transport) : m_Transport(std::move(transport))
{
	if (!m_Transport)
		throw std::invalid_argument("GameSession requires a memory transport");
}

GameSession::~GameSession()
{
	Stop();
}

bool GameSession::Read(GameAddress address, std::uint32_t size, Completion completion)
{
	MemoryRequest request;
	request.operation = MemoryRequest::Operation::Read;
	request.address = address;
	request.readSize = size;
	return Submit(std::move(request), std::move(completion));
}

bool GameSession::Write(GameAddress address, std::span<std::byte const> data, Completion completion)
{
	MemoryRequest request;
	request.operation = MemoryRequest::Operation::Write;
	request.address = address;
	request.data.assign(data.begin(), data.end());
	return Submit(std::move(request), std::move(completion));
}

bool GameSession::Submit(MemoryRequest request, Completion completion)
{
	if (!CanSubmit() || !completion)
		return false;

	request.id = AllocateRequestId();
	MemoryResult::Status const validation = ValidateMemoryRequest(request);
	if (validation != MemoryResult::Status::Ok)
	{
		completion(MemoryResult{request.id, validation, {}});
		return false;
	}

	MemoryRequestId const id = request.id;
	PendingRequest pending{
		request.operation,
		request.operation == MemoryRequest::Operation::Read ? request.readSize
															: static_cast<std::uint32_t>(request.data.size()),
		std::move(completion),
	};
	m_Pending.emplace(id, std::move(pending));
	if (!m_Transport->Submit(std::move(request)))
	{
		m_Pending.erase(id);
		return false;
	}
	return true;
}

MemoryRequestId GameSession::AllocateRequestId()
{
	for (;;)
	{
		MemoryRequestId const candidate = m_NextRequestId++;
		if (m_NextRequestId == 0)
			m_NextRequestId = 1;
		if (candidate != 0 && !m_Pending.contains(candidate))
			return candidate;
	}
}

void GameSession::Poll()
{
	if (m_Stopped)
		return;

	for (MemoryResult& result : m_Transport->PollResults())
	{
		auto const found = m_Pending.find(result.id);
		if (found == m_Pending.end())
		{
			++m_ProtocolErrorCount;
			continue;
		}

		PendingRequest pending = std::move(found->second);
		m_Pending.erase(found);
		if (result.status == MemoryResult::Status::Ok)
		{
			bool const validRead =
				pending.operation == MemoryRequest::Operation::Read && result.data.size() == pending.expectedSize;
			bool const validWrite = pending.operation == MemoryRequest::Operation::Write && result.data.empty();
			if (!validRead && !validWrite)
			{
				result.status = MemoryResult::Status::ProtocolError;
				result.data.clear();
				++m_ProtocolErrorCount;
			}
		}
		pending.completion(std::move(result));
	}
}

void GameSession::Stop()
{
	if (m_Stopped)
		return;
	m_Stopped = true;

	auto pending = std::move(m_Pending);
	m_Pending.clear();
	for (auto& [id, request] : pending)
		request.completion(MemoryResult{id, MemoryResult::Status::Disconnected, {}});
}

bool GameSession::CanSubmit() const
{
	return !m_Stopped && m_Transport->State() == TransportState::Connected &&
		   m_Pending.size() < MaximumOutstandingMemoryRequests;
}
