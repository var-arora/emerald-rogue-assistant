#include "Bridge/BridgeProtocol.h"

#include "Endian.h"
#include "Platform/Utf8.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace rogue::bridge
{
namespace
{
constexpr std::array<std::byte, 4> ClientMagic{
	std::byte{0x52}, std::byte{0x41}, std::byte{0x42}, std::byte{0x31}};

template <rogue::endian::Integer T>
void AppendLittle(std::vector<std::byte>& bytes, T value)
{
	std::size_t const offset = bytes.size();
	bytes.resize(offset + sizeof(T));
	[[maybe_unused]] bool const wrote = rogue::endian::WriteLittle<T>(bytes, offset, value);
}

bool IsKnownMessageType(MessageType type)
{
	switch (type)
	{
	case MessageType::ClientHello:
	case MessageType::ServerHello:
	case MessageType::ReadRequest:
	case MessageType::WriteRequest:
	case MessageType::ReadResult:
	case MessageType::WriteResult:
	case MessageType::Error:
	case MessageType::Close:
		return true;
	}
	return false;
}

bool RequiresZeroRequestId(MessageType type)
{
	return type == MessageType::ClientHello || type == MessageType::ServerHello || type == MessageType::Close;
}

bool RequiresNonzeroRequestId(MessageType type)
{
	return type == MessageType::ReadRequest || type == MessageType::WriteRequest || type == MessageType::ReadResult
		   || type == MessageType::WriteResult;
}

bool RequireFrameType(Frame const& frame, MessageType expected, std::string& error)
{
	if (frame.type != expected)
	{
		error = "unexpected bridge message type";
		return false;
	}
	return ValidateFrame(frame, error);
}

std::string BytesToString(std::span<std::byte const> bytes)
{
	std::string value;
	value.reserve(bytes.size());
	for (std::byte byte : bytes)
		value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
	return value;
}

bool DecodeProtocolErrorCode(std::uint16_t encoded, ProtocolErrorCode& decoded)
{
	switch (encoded)
	{
	case static_cast<std::uint16_t>(ProtocolErrorCode::UnsupportedProtocol):
		decoded = ProtocolErrorCode::UnsupportedProtocol;
		return true;
	case static_cast<std::uint16_t>(ProtocolErrorCode::Busy):
		decoded = ProtocolErrorCode::Busy;
		return true;
	case static_cast<std::uint16_t>(ProtocolErrorCode::MalformedFrame):
		decoded = ProtocolErrorCode::MalformedFrame;
		return true;
	case static_cast<std::uint16_t>(ProtocolErrorCode::InvalidRequestId):
		decoded = ProtocolErrorCode::InvalidRequestId;
		return true;
	case static_cast<std::uint16_t>(ProtocolErrorCode::InvalidAddress):
		decoded = ProtocolErrorCode::InvalidAddress;
		return true;
	case static_cast<std::uint16_t>(ProtocolErrorCode::InvalidSize):
		decoded = ProtocolErrorCode::InvalidSize;
		return true;
	case static_cast<std::uint16_t>(ProtocolErrorCode::QueueFull):
		decoded = ProtocolErrorCode::QueueFull;
		return true;
	case static_cast<std::uint16_t>(ProtocolErrorCode::InternalError):
		decoded = ProtocolErrorCode::InternalError;
		return true;
	default:
		return false;
	}
}
} // namespace

bool ValidateFrame(Frame const& frame, std::string& error)
{
	if (!IsKnownMessageType(frame.type))
	{
		error = "unknown bridge message type";
		return false;
	}
	if (frame.flags != 0)
	{
		error = "bridge protocol 1.0 requires zero flags";
		return false;
	}
	if (RequiresZeroRequestId(frame.type) && frame.requestId != 0)
	{
		error = "bridge control messages require request ID zero";
		return false;
	}
	if (RequiresNonzeroRequestId(frame.type) && frame.requestId == 0)
	{
		error = "bridge memory messages require a nonzero request ID";
		return false;
	}
	if (frame.payload.size() > MaximumFrameBodyLength - FrameBodyHeaderSize)
	{
		error = "bridge frame exceeds the 1 MiB body limit";
		return false;
	}
	error.clear();
	return true;
}

std::size_t WireSize(Frame const& frame)
{
	return FrameLengthSize + FrameBodyHeaderSize + frame.payload.size();
}

bool TryEncodeFrame(Frame const& frame, std::vector<std::byte>& output, std::string& error)
{
	output.clear();
	if (!ValidateFrame(frame, error))
		return false;

	std::uint32_t const bodyLength = static_cast<std::uint32_t>(FrameBodyHeaderSize + frame.payload.size());
	output.resize(FrameLengthSize + bodyLength);
	rogue::endian::WriteLittle(output, 0, bodyLength);
	output[4] = static_cast<std::byte>(frame.type);
	output[5] = static_cast<std::byte>(frame.flags);
	rogue::endian::WriteLittle<std::uint16_t>(output, 6, 0);
	rogue::endian::WriteLittle(output, 8, frame.requestId);
	std::copy(frame.payload.begin(), frame.payload.end(), output.begin() + 12);
	return true;
}

Frame EncodeClientHello(ClientHello const& hello)
{
	Frame frame;
	frame.type = MessageType::ClientHello;
	frame.payload.insert(frame.payload.end(), ClientMagic.begin(), ClientMagic.end());
	AppendLittle(frame.payload, hello.protocolMajor);
	AppendLittle(frame.payload, hello.protocolMinor);
	AppendLittle(frame.payload, hello.scriptVersion);
	return frame;
}

bool DecodeClientHello(Frame const& frame, ClientHello& hello, std::string& error)
{
	if (!RequireFrameType(frame, MessageType::ClientHello, error))
		return false;
	if (frame.payload.size() != 12 || !std::equal(ClientMagic.begin(), ClientMagic.end(), frame.payload.begin()))
	{
		error = "invalid ClientHello payload";
		return false;
	}
	rogue::endian::ReadLittle(frame.payload, 4, hello.protocolMajor);
	rogue::endian::ReadLittle(frame.payload, 6, hello.protocolMinor);
	rogue::endian::ReadLittle(frame.payload, 8, hello.scriptVersion);
	return true;
}

Frame EncodeServerHello(ServerHello const& hello)
{
	Frame frame;
	frame.type = MessageType::ServerHello;
	frame.payload.push_back(static_cast<std::byte>(hello.status));
	frame.payload.push_back(std::byte{0});
	AppendLittle(frame.payload, hello.protocolMajor);
	AppendLittle(frame.payload, hello.protocolMinor);
	AppendLittle(frame.payload, hello.applicationMajor);
	AppendLittle(frame.payload, hello.applicationMinor);
	AppendLittle(frame.payload, hello.applicationPatch);
	return frame;
}

bool DecodeServerHello(Frame const& frame, ServerHello& hello, std::string& error)
{
	if (!RequireFrameType(frame, MessageType::ServerHello, error))
		return false;
	if (frame.payload.size() != 12 || frame.payload[1] != std::byte{0})
	{
		error = "invalid ServerHello payload";
		return false;
	}
	std::uint8_t const status = std::to_integer<std::uint8_t>(frame.payload[0]);
	if (status > static_cast<std::uint8_t>(HelloStatus::Rejected))
	{
		error = "invalid ServerHello status";
		return false;
	}
	hello.status = static_cast<HelloStatus>(status);
	rogue::endian::ReadLittle(frame.payload, 2, hello.protocolMajor);
	rogue::endian::ReadLittle(frame.payload, 4, hello.protocolMinor);
	rogue::endian::ReadLittle(frame.payload, 6, hello.applicationMajor);
	rogue::endian::ReadLittle(frame.payload, 8, hello.applicationMinor);
	rogue::endian::ReadLittle(frame.payload, 10, hello.applicationPatch);
	return true;
}

Frame EncodeReadRequest(std::uint32_t requestId, ReadRequest const& request)
{
	Frame frame;
	frame.type = MessageType::ReadRequest;
	frame.requestId = requestId;
	AppendLittle(frame.payload, request.address);
	AppendLittle(frame.payload, request.size);
	return frame;
}

bool DecodeReadRequest(Frame const& frame, ReadRequest& request, std::string& error)
{
	if (!RequireFrameType(frame, MessageType::ReadRequest, error))
		return false;
	if (frame.payload.size() != 8)
	{
		error = "invalid ReadRequest payload";
		return false;
	}
	rogue::endian::ReadLittle(frame.payload, 0, request.address);
	rogue::endian::ReadLittle(frame.payload, 4, request.size);
	return true;
}

bool TryEncodeWriteRequest(std::uint32_t requestId, WriteRequest const& request, Frame& frame, std::string& error)
{
	if (request.data.size() > MaximumFrameBodyLength - FrameBodyHeaderSize - 8)
	{
		error = "WriteRequest payload exceeds the bridge frame limit";
		return false;
	}
	frame = {};
	frame.type = MessageType::WriteRequest;
	frame.requestId = requestId;
	AppendLittle(frame.payload, request.address);
	AppendLittle(frame.payload, static_cast<std::uint32_t>(request.data.size()));
	frame.payload.insert(frame.payload.end(), request.data.begin(), request.data.end());
	return ValidateFrame(frame, error);
}

bool DecodeWriteRequest(Frame const& frame, WriteRequest& request, std::string& error)
{
	if (!RequireFrameType(frame, MessageType::WriteRequest, error))
		return false;
	if (frame.payload.size() < 8)
	{
		error = "invalid WriteRequest payload";
		return false;
	}
	std::uint32_t encodedSize = 0;
	rogue::endian::ReadLittle(frame.payload, 0, request.address);
	rogue::endian::ReadLittle(frame.payload, 4, encodedSize);
	if (encodedSize != frame.payload.size() - 8)
	{
		error = "WriteRequest byte count does not match its payload";
		return false;
	}
	request.data.assign(frame.payload.begin() + 8, frame.payload.end());
	return true;
}

bool TryEncodeError(std::uint32_t requestId, ErrorMessage const& message, Frame& frame, std::string& error)
{
	if (message.diagnostic.size() > MaximumDiagnosticLength)
	{
		error = "bridge diagnostic exceeds 1024 bytes";
		return false;
	}
	if (!rogue::platform::IsValidUtf8(message.diagnostic))
	{
		error = "bridge diagnostic is not valid UTF-8";
		return false;
	}
	frame = {};
	frame.type = MessageType::Error;
	frame.requestId = requestId;
	AppendLittle(frame.payload, static_cast<std::uint16_t>(message.code));
	AppendLittle(frame.payload, static_cast<std::uint16_t>(message.diagnostic.size()));
	for (unsigned char character : message.diagnostic)
		frame.payload.push_back(static_cast<std::byte>(character));
	return ValidateFrame(frame, error);
}

bool DecodeErrorMessage(Frame const& frame, ErrorMessage& message, std::string& error)
{
	if (!RequireFrameType(frame, MessageType::Error, error))
		return false;
	if (frame.payload.size() < 4)
	{
		error = "invalid Error payload";
		return false;
	}
	std::uint16_t code = 0;
	std::uint16_t diagnosticSize = 0;
	rogue::endian::ReadLittle(frame.payload, 0, code);
	rogue::endian::ReadLittle(frame.payload, 2, diagnosticSize);
	if (diagnosticSize != frame.payload.size() - 4 || diagnosticSize > MaximumDiagnosticLength)
	{
		error = "Error diagnostic length does not match its payload";
		return false;
	}
	std::string diagnostic = BytesToString(std::span(frame.payload).subspan(4));
	if (!rogue::platform::IsValidUtf8(diagnostic))
	{
		error = "Error diagnostic is not valid UTF-8";
		return false;
	}
	if (!DecodeProtocolErrorCode(code, message.code))
	{
		error = "unknown Error code";
		return false;
	}
	message.diagnostic = std::move(diagnostic);
	return true;
}

Frame EncodeClose()
{
	Frame frame;
	frame.type = MessageType::Close;
	return frame;
}

bool FrameDecoder::Append(std::span<std::byte const> bytes)
{
	if (m_Error != DecodeError::None)
		return false;

	std::size_t inputOffset = 0;
	while (inputOffset < bytes.size())
	{
		if (m_ExpectedWireSize == 0 && m_Buffer.size() < FrameLengthSize)
		{
			std::size_t const count = std::min(FrameLengthSize - m_Buffer.size(), bytes.size() - inputOffset);
			m_Buffer.insert(m_Buffer.end(), bytes.begin() + static_cast<std::ptrdiff_t>(inputOffset),
							bytes.begin() + static_cast<std::ptrdiff_t>(inputOffset + count));
			inputOffset += count;
			if (m_Buffer.size() < FrameLengthSize)
				continue;
			if (!ReadExpectedSize())
				return false;
		}

		std::size_t const count = std::min(m_ExpectedWireSize - m_Buffer.size(), bytes.size() - inputOffset);
		m_Buffer.insert(m_Buffer.end(), bytes.begin() + static_cast<std::ptrdiff_t>(inputOffset),
						bytes.begin() + static_cast<std::ptrdiff_t>(inputOffset + count));
		inputOffset += count;
		if (m_Buffer.size() == m_ExpectedWireSize && !DecodeBufferedFrame())
			return false;
	}
	return true;
}

std::vector<Frame> FrameDecoder::PollFrames()
{
	std::vector<Frame> frames;
	frames.reserve(m_Frames.size());
	while (!m_Frames.empty())
	{
		frames.push_back(std::move(m_Frames.front()));
		m_Frames.pop_front();
	}
	m_QueuedBytes = 0;
	return frames;
}

DecodeError FrameDecoder::Error() const
{
	return m_Error;
}

std::string const& FrameDecoder::Diagnostic() const
{
	return m_Diagnostic;
}

std::size_t FrameDecoder::QueuedFrameCount() const
{
	return m_Frames.size();
}

void FrameDecoder::Reset()
{
	m_Buffer.clear();
	m_ExpectedWireSize = 0;
	m_Frames.clear();
	m_QueuedBytes = 0;
	m_Error = DecodeError::None;
	m_Diagnostic.clear();
}

bool FrameDecoder::ReadExpectedSize()
{
	std::uint32_t bodyLength = 0;
	rogue::endian::ReadLittle(m_Buffer, 0, bodyLength);
	if (bodyLength < FrameBodyHeaderSize)
	{
		Fail(DecodeError::BodyTooSmall, "bridge frame body is shorter than its fixed header");
		return false;
	}
	if (bodyLength > MaximumFrameBodyLength)
	{
		Fail(DecodeError::FrameTooLarge, "bridge frame body exceeds 1 MiB");
		return false;
	}
	m_ExpectedWireSize = FrameLengthSize + static_cast<std::size_t>(bodyLength);
	return true;
}

bool FrameDecoder::DecodeBufferedFrame()
{
	std::uint8_t const encodedType = std::to_integer<std::uint8_t>(m_Buffer[4]);
	if (encodedType < static_cast<std::uint8_t>(MessageType::ClientHello)
		|| encodedType > static_cast<std::uint8_t>(MessageType::Close))
	{
		Fail(DecodeError::InvalidMessageType, "unknown bridge message type");
		return false;
	}
	if (m_Buffer[5] != std::byte{0})
	{
		Fail(DecodeError::UnsupportedFlags, "bridge protocol 1.0 requires zero flags");
		return false;
	}
	std::uint16_t reserved = 0;
	rogue::endian::ReadLittle(m_Buffer, 6, reserved);
	if (reserved != 0)
	{
		Fail(DecodeError::NonzeroReserved, "bridge frame reserved field must be zero");
		return false;
	}

	Frame frame;
	frame.type = static_cast<MessageType>(encodedType);
	rogue::endian::ReadLittle(m_Buffer, 8, frame.requestId);
	frame.payload.assign(m_Buffer.begin() + 12, m_Buffer.end());
	std::string validationError;
	if (!ValidateFrame(frame, validationError))
	{
		Fail(DecodeError::InvalidRequestId, std::move(validationError));
		return false;
	}
	if (m_Frames.size() >= MaximumQueuedFrames || m_QueuedBytes > MaximumQueuedFrameBytes - m_ExpectedWireSize)
	{
		Fail(DecodeError::QueueOverflow, "bridge receive queue is full");
		return false;
	}
	m_QueuedBytes += m_ExpectedWireSize;
	m_Frames.push_back(std::move(frame));
	m_Buffer.clear();
	m_ExpectedWireSize = 0;
	return true;
}

void FrameDecoder::Fail(DecodeError error, std::string diagnostic)
{
	m_Error = error;
	m_Diagnostic = std::move(diagnostic);
	m_Buffer.clear();
	m_ExpectedWireSize = 0;
}

bool FrameWriter::Queue(Frame const& frame, std::string& error)
{
	std::vector<std::byte> encoded;
	if (!TryEncodeFrame(frame, encoded, error))
		return false;
	if (m_Frames.size() >= MaximumQueuedFrames || m_QueuedBytes > MaximumQueuedFrameBytes - encoded.size())
	{
		error = "bridge send queue is full";
		return false;
	}
	m_QueuedBytes += encoded.size();
	m_Frames.push_back(std::move(encoded));
	return true;
}

std::span<std::byte const> FrameWriter::PendingBytes() const
{
	if (m_Frames.empty())
		return {};
	return std::span<std::byte const>(m_Frames.front()).subspan(m_FrontOffset);
}

bool FrameWriter::ConsumeSent(std::size_t byteCount)
{
	if (byteCount > PendingBytes().size())
		return false;
	if (byteCount == 0)
		return true;
	m_FrontOffset += byteCount;
	m_QueuedBytes -= byteCount;
	if (m_FrontOffset == m_Frames.front().size())
	{
		m_Frames.pop_front();
		m_FrontOffset = 0;
	}
	return true;
}

std::size_t FrameWriter::QueuedFrameCount() const
{
	return m_Frames.size();
}

std::size_t FrameWriter::QueuedByteCount() const
{
	return m_QueuedBytes;
}

void FrameWriter::Reset()
{
	m_Frames.clear();
	m_FrontOffset = 0;
	m_QueuedBytes = 0;
}
} // namespace rogue::bridge
