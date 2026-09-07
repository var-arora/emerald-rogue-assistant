#pragma once

#include "Bridge/BridgeProtocol.h"
#include "Bridge/GameMemoryTransport.h"
#include "Bridge/TcpSocket.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class TcpLuaTransport final : public IGameMemoryTransport
{
  public:
	explicit TcpLuaTransport(std::uint16_t port = 30125);
	~TcpLuaTransport() override;

	TcpLuaTransport(TcpLuaTransport const&) = delete;
	TcpLuaTransport& operator=(TcpLuaTransport const&) = delete;

	bool Submit(MemoryRequest request) override;
	std::vector<MemoryResult> PollResults() override;
	[[nodiscard]] TransportState State() const override;
	void Stop() override;

	[[nodiscard]] std::uint16_t Port() const;
	[[nodiscard]] std::string const& Diagnostic() const;

  private:
	struct PendingRequest
	{
		MemoryRequest::Operation operation = MemoryRequest::Operation::Read;
		std::uint32_t expectedSize = 0;
	};

	struct Peer
	{
		explicit Peer(rogue::bridge::TcpSocket socket);

		rogue::bridge::TcpSocket socket;
		rogue::bridge::FrameDecoder decoder;
		rogue::bridge::FrameWriter writer;
		bool handshakeAccepted = false;
		bool closeAfterWrite = false;
	};

	struct ClosingPeer
	{
		rogue::bridge::TcpSocket socket;
		rogue::bridge::FrameWriter writer;
	};

	void AcceptClients();
	void ReceiveClient();
	void ProcessFrames(std::vector<rogue::bridge::Frame> const& frames);
	void ProcessFrame(rogue::bridge::Frame const& frame);
	void FlushClient();
	void FlushClosingPeers();
	void RejectBusyClient(rogue::bridge::TcpSocket socket);
	[[nodiscard]] bool QueueFrame(Peer& peer, rogue::bridge::Frame const& frame);
	[[nodiscard]] bool QueueError(Peer& peer, std::uint32_t requestId, rogue::bridge::ProtocolErrorCode code,
								  std::string const& diagnostic);
	void BeginProtocolClose(rogue::bridge::ProtocolErrorCode code, std::string diagnostic);
	void BeginOrderlyClose();
	void CloseClient(MemoryResult::Status pendingStatus, std::string diagnostic = {});
	void CompletePending(MemoryResult::Status status);

	rogue::bridge::TcpSocket m_Listener;
	std::optional<Peer> m_Client;
	std::deque<ClosingPeer> m_ClosingPeers;
	std::unordered_map<MemoryRequestId, PendingRequest> m_Pending;
	std::vector<MemoryResult> m_Results;
	TransportState m_State = TransportState::Disconnected;
	std::uint16_t m_Port = 0;
	std::string m_Diagnostic;
};
