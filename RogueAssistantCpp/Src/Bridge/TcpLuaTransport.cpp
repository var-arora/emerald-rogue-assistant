#include "Bridge/TcpLuaTransport.h"

#include "RogueAssistantVersion.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace
{
constexpr std::size_t ReceiveChunkSize = 64U * 1024U;
constexpr std::size_t MaximumIoBytesPerPoll = 256U * 1024U;
constexpr std::size_t MaximumAcceptsPerPoll = 8;
constexpr std::size_t MaximumClosingPeers = 8;
constexpr std::chrono::milliseconds ShutdownFlushTimeout{50};

MemoryResult::Status ToMemoryStatus(rogue::bridge::ProtocolErrorCode code)
{
	switch (code)
	{
	case rogue::bridge::ProtocolErrorCode::InvalidAddress:
		return MemoryResult::Status::InvalidAddress;
	case rogue::bridge::ProtocolErrorCode::InvalidSize:
		return MemoryResult::Status::InvalidSize;
	case rogue::bridge::ProtocolErrorCode::UnsupportedProtocol:
	case rogue::bridge::ProtocolErrorCode::Busy:
	case rogue::bridge::ProtocolErrorCode::MalformedFrame:
	case rogue::bridge::ProtocolErrorCode::InvalidRequestId:
	case rogue::bridge::ProtocolErrorCode::QueueFull:
	case rogue::bridge::ProtocolErrorCode::InternalError:
		return MemoryResult::Status::ProtocolError;
	}
	return MemoryResult::Status::ProtocolError;
}
} // namespace

TcpLuaTransport::Peer::Peer(rogue::bridge::TcpSocket acceptedSocket) : socket(std::move(acceptedSocket))
{
}

TcpLuaTransport::TcpLuaTransport(std::uint16_t port)
{
	std::string error;
	if (!m_Listener.ListenLoopback(port, error))
		throw std::runtime_error(error);
	m_Port = m_Listener.BoundPort(error);
	if (m_Port == 0)
	{
		m_Listener.Close();
		throw std::runtime_error(error.empty() ? "cannot determine bridge listener port" : error);
	}
	m_State = TransportState::Listening;
}

TcpLuaTransport::~TcpLuaTransport()
{
	Stop();
}

bool TcpLuaTransport::Submit(MemoryRequest request)
{
	if (m_State != TransportState::Connected || !m_Client || !m_Client->handshakeAccepted ||
		m_Client->closeAfterWrite || m_Pending.contains(request.id))
	{
		return false;
	}

	MemoryResult::Status const validation = ValidateMemoryRequest(request);
	if (validation != MemoryResult::Status::Ok)
	{
		m_Results.push_back({request.id, validation, {}});
		return true;
	}

	rogue::bridge::Frame frame;
	std::string error;
	if (request.operation == MemoryRequest::Operation::Read)
	{
		if (request.readSize > rogue::bridge::MaximumFrameBodyLength - rogue::bridge::FrameBodyHeaderSize)
		{
			m_Results.push_back({request.id, MemoryResult::Status::InvalidSize, {}});
			return true;
		}
		frame = rogue::bridge::EncodeReadRequest(request.id, {request.address, request.readSize});
	}
	else
	{
		if (!rogue::bridge::TryEncodeWriteRequest(request.id, {request.address, std::move(request.data)}, frame, error))
		{
			m_Results.push_back({request.id, MemoryResult::Status::InvalidSize, {}});
			return true;
		}
	}

	if (!m_Client->writer.Queue(frame, error))
	{
		m_Diagnostic = std::move(error);
		return false;
	}
	m_Pending.emplace(request.id,
					  PendingRequest{request.operation, request.operation == MemoryRequest::Operation::Read
															? request.readSize
															: static_cast<std::uint32_t>(frame.payload.size() - 8)});
	return true;
}

std::vector<MemoryResult> TcpLuaTransport::PollResults()
{
	if (m_State != TransportState::Stopped)
	{
		AcceptClients();
		FlushClosingPeers();
		if (m_Client)
		{
			ReceiveClient();
			if (m_Client)
				FlushClient();
		}
	}

	auto results = std::move(m_Results);
	m_Results.clear();
	return results;
}

TransportState TcpLuaTransport::State() const
{
	return m_State;
}

void TcpLuaTransport::Stop()
{
	if (m_State == TransportState::Stopped)
		return;
	if (m_Client)
	{
		if (m_Client->handshakeAccepted)
		{
			// Pending memory requests no longer have a consumer during shutdown. Drop
			// them so the control message cannot sit behind up to 4 MiB of queued data.
			m_Client->writer.Reset();
			if (QueueFrame(*m_Client, rogue::bridge::EncodeClose()))
			{
				auto const deadline = std::chrono::steady_clock::now() + ShutdownFlushTimeout;
				while (m_Client && !m_Client->writer.PendingBytes().empty() &&
					   std::chrono::steady_clock::now() < deadline)
				{
					std::size_t const pendingBefore = m_Client->writer.QueuedByteCount();
					FlushClient();
					if (m_Client && m_Client->writer.QueuedByteCount() == pendingBefore)
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
		}
		if (m_Client)
			m_Client->socket.Close();
		m_Client.reset();
	}
	for (ClosingPeer& peer : m_ClosingPeers)
		peer.socket.Close();
	m_ClosingPeers.clear();
	CompletePending(MemoryResult::Status::Disconnected);
	m_Listener.Close();
	m_State = TransportState::Stopped;
}

std::uint16_t TcpLuaTransport::Port() const
{
	return m_Port;
}

std::string const& TcpLuaTransport::Diagnostic() const
{
	return m_Diagnostic;
}

void TcpLuaTransport::AcceptClients()
{
	for (std::size_t acceptedCount = 0; acceptedCount < MaximumAcceptsPerPoll; ++acceptedCount)
	{
		rogue::bridge::TcpSocket socket;
		std::string error;
		rogue::bridge::SocketStatus const status = m_Listener.Accept(socket, error);
		if (status == rogue::bridge::SocketStatus::WouldBlock)
			return;
		if (status != rogue::bridge::SocketStatus::Ok)
		{
			m_Diagnostic = std::move(error);
			return;
		}

		if (!m_Client)
		{
			m_Client.emplace(std::move(socket));
		}
		else
		{
			RejectBusyClient(std::move(socket));
		}
	}
}

void TcpLuaTransport::ReceiveClient()
{
	if (!m_Client || m_Client->closeAfterWrite)
		return;

	std::array<std::byte, ReceiveChunkSize> buffer{};
	std::size_t receivedThisPoll = 0;
	while (m_Client && !m_Client->closeAfterWrite && receivedThisPoll < MaximumIoBytesPerPoll)
	{
		std::size_t const available = std::min(buffer.size(), MaximumIoBytesPerPoll - receivedThisPoll);
		auto const result = m_Client->socket.Receive(std::span(buffer).first(available));
		if (result.status == rogue::bridge::SocketStatus::WouldBlock)
			return;
		if (result.status == rogue::bridge::SocketStatus::Closed)
		{
			CloseClient(MemoryResult::Status::Disconnected);
			return;
		}
		if (result.status == rogue::bridge::SocketStatus::Error)
		{
			CloseClient(MemoryResult::Status::Disconnected, result.diagnostic);
			return;
		}
		if (result.byteCount == 0)
			return;

		receivedThisPoll += result.byteCount;
		if (!m_Client->decoder.Append(std::span(buffer).first(result.byteCount)))
		{
			BeginProtocolClose(rogue::bridge::ProtocolErrorCode::MalformedFrame, m_Client->decoder.Diagnostic());
			return;
		}
		ProcessFrames(m_Client->decoder.PollFrames());
	}
}

void TcpLuaTransport::ProcessFrames(std::vector<rogue::bridge::Frame> const& frames)
{
	for (rogue::bridge::Frame const& frame : frames)
	{
		if (!m_Client || m_Client->closeAfterWrite)
			return;
		ProcessFrame(frame);
	}
}

void TcpLuaTransport::ProcessFrame(rogue::bridge::Frame const& frame)
{
	using namespace rogue::bridge;
	if (!m_Client)
		return;

	if (!m_Client->handshakeAccepted)
	{
		ClientHello hello;
		std::string error;
		if (!DecodeClientHello(frame, hello, error))
		{
			BeginProtocolClose(ProtocolErrorCode::MalformedFrame, std::move(error));
			return;
		}

		ServerHello response;
		response.protocolMajor = ProtocolMajor;
		response.protocolMinor = ProtocolMinor;
		response.applicationMajor = ROGUE_ASSISTANT_VERSION_MAJOR;
		response.applicationMinor = ROGUE_ASSISTANT_VERSION_MINOR;
		response.applicationPatch = ROGUE_ASSISTANT_VERSION_PATCH;
		if (hello.protocolMajor != ProtocolMajor || hello.protocolMinor != ProtocolMinor ||
			hello.scriptVersion != ScriptVersion)
		{
			response.status = HelloStatus::Rejected;
			(void)QueueFrame(*m_Client, EncodeServerHello(response));
			BeginProtocolClose(ProtocolErrorCode::UnsupportedProtocol,
							   "Emerald Rogue Assistant requires bridge protocol 1.0 and script version 1");
			return;
		}

		if (!QueueFrame(*m_Client, EncodeServerHello(response)))
		{
			CloseClient(MemoryResult::Status::Disconnected, "cannot queue ServerHello");
			return;
		}
		m_Client->handshakeAccepted = true;
		m_State = TransportState::Connected;
		m_Diagnostic.clear();
		return;
	}

	auto const pending = m_Pending.find(frame.requestId);
	if (frame.type == MessageType::ReadResult)
	{
		if (pending == m_Pending.end() || pending->second.operation != MemoryRequest::Operation::Read ||
			frame.payload.size() != pending->second.expectedSize)
		{
			BeginProtocolClose(ProtocolErrorCode::InvalidRequestId, "unexpected or malformed ReadResult");
			return;
		}
		m_Results.push_back({frame.requestId, MemoryResult::Status::Ok, frame.payload});
		m_Pending.erase(pending);
		return;
	}
	if (frame.type == MessageType::WriteResult)
	{
		if (pending == m_Pending.end() || pending->second.operation != MemoryRequest::Operation::Write ||
			!frame.payload.empty())
		{
			BeginProtocolClose(ProtocolErrorCode::InvalidRequestId, "unexpected or malformed WriteResult");
			return;
		}
		m_Results.push_back({frame.requestId, MemoryResult::Status::Ok, {}});
		m_Pending.erase(pending);
		return;
	}
	if (frame.type == MessageType::Error)
	{
		ErrorMessage message;
		std::string error;
		if (!DecodeErrorMessage(frame, message, error) || frame.requestId == 0 || pending == m_Pending.end())
		{
			BeginProtocolClose(ProtocolErrorCode::MalformedFrame,
							   error.empty() ? "unexpected protocol Error message" : std::move(error));
			return;
		}
		m_Results.push_back({frame.requestId, ToMemoryStatus(message.code), {}});
		m_Pending.erase(pending);
		m_Diagnostic = message.diagnostic;
		(void)QueueFrame(*m_Client, EncodeClose());
		m_Client->handshakeAccepted = false;
		m_Client->closeAfterWrite = true;
		m_State = TransportState::Listening;
		CompletePending(MemoryResult::Status::Disconnected);
		return;
	}
	if (frame.type == MessageType::Close && frame.requestId == 0 && frame.payload.empty())
	{
		BeginOrderlyClose();
		return;
	}

	BeginProtocolClose(ProtocolErrorCode::MalformedFrame, "unexpected bridge message from mGBA");
}

void TcpLuaTransport::FlushClient()
{
	if (!m_Client)
		return;
	std::size_t sentThisPoll = 0;
	while (m_Client && !m_Client->writer.PendingBytes().empty() && sentThisPoll < MaximumIoBytesPerPoll)
	{
		auto const pending = m_Client->writer.PendingBytes();
		std::size_t const available = std::min(pending.size(), MaximumIoBytesPerPoll - sentThisPoll);
		auto const result = m_Client->socket.Send(pending.first(available));
		if (result.status == rogue::bridge::SocketStatus::WouldBlock)
			return;
		if (result.status != rogue::bridge::SocketStatus::Ok || result.byteCount == 0)
		{
			CloseClient(MemoryResult::Status::Disconnected, result.diagnostic);
			return;
		}
		(void)m_Client->writer.ConsumeSent(result.byteCount);
		sentThisPoll += result.byteCount;
	}
	if (m_Client && m_Client->closeAfterWrite && m_Client->writer.PendingBytes().empty())
		CloseClient(MemoryResult::Status::Disconnected);
}

void TcpLuaTransport::FlushClosingPeers()
{
	for (auto peer = m_ClosingPeers.begin(); peer != m_ClosingPeers.end();)
	{
		auto const pending = peer->writer.PendingBytes();
		if (pending.empty())
		{
			peer->socket.Close();
			peer = m_ClosingPeers.erase(peer);
			continue;
		}
		auto const result = peer->socket.Send(pending.first(std::min(pending.size(), MaximumIoBytesPerPoll)));
		if (result.status == rogue::bridge::SocketStatus::WouldBlock)
		{
			++peer;
			continue;
		}
		if (result.status != rogue::bridge::SocketStatus::Ok || result.byteCount == 0)
		{
			peer->socket.Close();
			peer = m_ClosingPeers.erase(peer);
			continue;
		}
		(void)peer->writer.ConsumeSent(result.byteCount);
		++peer;
	}
}

void TcpLuaTransport::RejectBusyClient(rogue::bridge::TcpSocket socket)
{
	using namespace rogue::bridge;
	if (m_ClosingPeers.size() >= MaximumClosingPeers)
	{
		socket.Close();
		return;
	}

	ClosingPeer peer;
	peer.socket = std::move(socket);
	std::string error;
	ServerHello hello;
	hello.status = HelloStatus::Rejected;
	hello.applicationMajor = ROGUE_ASSISTANT_VERSION_MAJOR;
	hello.applicationMinor = ROGUE_ASSISTANT_VERSION_MINOR;
	hello.applicationPatch = ROGUE_ASSISTANT_VERSION_PATCH;
	Frame errorFrame;
	bool const encodedError =
		TryEncodeError(0, {ProtocolErrorCode::Busy, "another mGBA instance is already connected"}, errorFrame, error);
	if (!peer.writer.Queue(EncodeServerHello(hello), error) || !encodedError || !peer.writer.Queue(errorFrame, error) ||
		!peer.writer.Queue(EncodeClose(), error))
	{
		peer.socket.Close();
		return;
	}
	m_ClosingPeers.push_back(std::move(peer));
}

bool TcpLuaTransport::QueueFrame(Peer& peer, rogue::bridge::Frame const& frame)
{
	std::string error;
	if (peer.writer.Queue(frame, error))
		return true;
	m_Diagnostic = std::move(error);
	return false;
}

bool TcpLuaTransport::QueueError(Peer& peer, std::uint32_t requestId, rogue::bridge::ProtocolErrorCode code,
								 std::string const& diagnostic)
{
	rogue::bridge::Frame frame;
	std::string error;
	if (!rogue::bridge::TryEncodeError(requestId, {code, diagnostic}, frame, error))
	{
		m_Diagnostic = std::move(error);
		return false;
	}
	return QueueFrame(peer, frame);
}

void TcpLuaTransport::BeginProtocolClose(rogue::bridge::ProtocolErrorCode code, std::string diagnostic)
{
	if (!m_Client)
		return;
	m_Diagnostic = diagnostic;
	bool const queued =
		QueueError(*m_Client, 0, code, diagnostic) && QueueFrame(*m_Client, rogue::bridge::EncodeClose());
	m_Client->handshakeAccepted = false;
	m_Client->closeAfterWrite = true;
	m_State = TransportState::Listening;
	CompletePending(MemoryResult::Status::ProtocolError);
	if (!queued)
		CloseClient(MemoryResult::Status::ProtocolError, std::move(diagnostic));
}

void TcpLuaTransport::BeginOrderlyClose()
{
	if (!m_Client)
		return;
	bool const queued = QueueFrame(*m_Client, rogue::bridge::EncodeClose());
	m_Client->handshakeAccepted = false;
	m_Client->closeAfterWrite = true;
	m_State = TransportState::Listening;
	CompletePending(MemoryResult::Status::Disconnected);
	if (!queued)
		CloseClient(MemoryResult::Status::Disconnected);
}

void TcpLuaTransport::CloseClient(MemoryResult::Status pendingStatus, std::string diagnostic)
{
	if (!diagnostic.empty())
		m_Diagnostic = std::move(diagnostic);
	CompletePending(pendingStatus);
	if (m_Client)
		m_Client->socket.Close();
	m_Client.reset();
	if (m_State != TransportState::Stopped)
		m_State = TransportState::Listening;
}

void TcpLuaTransport::CompletePending(MemoryResult::Status status)
{
	for (auto const& [id, request] : m_Pending)
	{
		(void)request;
		m_Results.push_back({id, status, {}});
	}
	m_Pending.clear();
}
