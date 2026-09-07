#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string>
#include <vector>

namespace rogue::bridge
{
inline constexpr std::uint16_t ProtocolMajor = 1;
inline constexpr std::uint16_t ProtocolMinor = 0;
inline constexpr std::uint32_t ScriptVersion = 1;

inline constexpr std::size_t FrameLengthSize = 4;
inline constexpr std::size_t FrameBodyHeaderSize = 8;
inline constexpr std::uint32_t MaximumFrameBodyLength = 1024U * 1024U;
inline constexpr std::size_t MaximumQueuedFrames = 256;
inline constexpr std::size_t MaximumQueuedFrameBytes = 4U * 1024U * 1024U;
inline constexpr std::size_t MaximumDiagnosticLength = 1024;

enum class MessageType : std::uint8_t
{
	ClientHello = 1,
	ServerHello = 2,
	ReadRequest = 3,
	WriteRequest = 4,
	ReadResult = 5,
	WriteResult = 6,
	Error = 7,
	Close = 8,
};

enum class HelloStatus : std::uint8_t
{
	Accepted = 0,
	Rejected = 1,
};

enum class ProtocolErrorCode : std::uint16_t
{
	UnsupportedProtocol = 1,
	Busy = 2,
	MalformedFrame = 3,
	InvalidRequestId = 4,
	InvalidAddress = 5,
	InvalidSize = 6,
	QueueFull = 7,
	InternalError = 8,
};

enum class DecodeError
{
	None,
	BodyTooSmall,
	FrameTooLarge,
	InvalidMessageType,
	UnsupportedFlags,
	NonzeroReserved,
	InvalidRequestId,
	QueueOverflow,
};

struct Frame
{
	MessageType type = MessageType::Close;
	std::uint8_t flags = 0;
	std::uint32_t requestId = 0;
	std::vector<std::byte> payload;
};

struct ClientHello
{
	std::uint16_t protocolMajor = ProtocolMajor;
	std::uint16_t protocolMinor = ProtocolMinor;
	std::uint32_t scriptVersion = ScriptVersion;
};

struct ServerHello
{
	HelloStatus status = HelloStatus::Accepted;
	std::uint16_t protocolMajor = ProtocolMajor;
	std::uint16_t protocolMinor = ProtocolMinor;
	std::uint16_t applicationMajor = 1;
	std::uint16_t applicationMinor = 0;
	std::uint16_t applicationPatch = 0;
};

struct ReadRequest
{
	std::uint32_t address = 0;
	std::uint32_t size = 0;
};

struct WriteRequest
{
	std::uint32_t address = 0;
	std::vector<std::byte> data;
};

struct ErrorMessage
{
	ProtocolErrorCode code = ProtocolErrorCode::InternalError;
	std::string diagnostic;
};

[[nodiscard]] bool TryEncodeFrame(Frame const& frame, std::vector<std::byte>& output, std::string& error);
[[nodiscard]] bool ValidateFrame(Frame const& frame, std::string& error);
[[nodiscard]] std::size_t WireSize(Frame const& frame);

[[nodiscard]] Frame EncodeClientHello(ClientHello const& hello = {});
[[nodiscard]] bool DecodeClientHello(Frame const& frame, ClientHello& hello, std::string& error);
[[nodiscard]] Frame EncodeServerHello(ServerHello const& hello);
[[nodiscard]] bool DecodeServerHello(Frame const& frame, ServerHello& hello, std::string& error);
[[nodiscard]] Frame EncodeReadRequest(std::uint32_t requestId, ReadRequest const& request);
[[nodiscard]] bool DecodeReadRequest(Frame const& frame, ReadRequest& request, std::string& error);
[[nodiscard]] bool TryEncodeWriteRequest(std::uint32_t requestId, WriteRequest const& request, Frame& frame,
										 std::string& error);
[[nodiscard]] bool DecodeWriteRequest(Frame const& frame, WriteRequest& request, std::string& error);
[[nodiscard]] bool TryEncodeError(std::uint32_t requestId, ErrorMessage const& message, Frame& frame,
								  std::string& error);
[[nodiscard]] bool DecodeErrorMessage(Frame const& frame, ErrorMessage& message, std::string& error);
[[nodiscard]] Frame EncodeClose();

class FrameDecoder
{
  public:
	[[nodiscard]] bool Append(std::span<std::byte const> bytes);
	[[nodiscard]] std::vector<Frame> PollFrames();
	[[nodiscard]] DecodeError Error() const;
	[[nodiscard]] std::string const& Diagnostic() const;
	[[nodiscard]] std::size_t QueuedFrameCount() const;
	void Reset();

  private:
	[[nodiscard]] bool ReadExpectedSize();
	[[nodiscard]] bool DecodeBufferedFrame();
	void Fail(DecodeError error, std::string diagnostic);

	std::vector<std::byte> m_Buffer;
	std::size_t m_ExpectedWireSize = 0;
	std::deque<Frame> m_Frames;
	std::size_t m_QueuedBytes = 0;
	DecodeError m_Error = DecodeError::None;
	std::string m_Diagnostic;
};

class FrameWriter
{
  public:
	[[nodiscard]] bool Queue(Frame const& frame, std::string& error);
	[[nodiscard]] std::span<std::byte const> PendingBytes() const;
	[[nodiscard]] bool ConsumeSent(std::size_t byteCount);
	[[nodiscard]] std::size_t QueuedFrameCount() const;
	[[nodiscard]] std::size_t QueuedByteCount() const;
	void Reset();

  private:
	std::deque<std::vector<std::byte>> m_Frames;
	std::size_t m_FrontOffset = 0;
	std::size_t m_QueuedBytes = 0;
};
} // namespace rogue::bridge
