#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace rogue::bridge
{
enum class SocketStatus
{
	Ok,
	WouldBlock,
	Closed,
	Error,
};

struct SocketIoResult
{
	SocketStatus status = SocketStatus::Error;
	std::size_t byteCount = 0;
	std::string diagnostic;
};

class TcpSocket
{
  public:
	TcpSocket() = default;
	~TcpSocket();

	TcpSocket(TcpSocket const&) = delete;
	TcpSocket& operator=(TcpSocket const&) = delete;
	TcpSocket(TcpSocket&& other) noexcept;
	TcpSocket& operator=(TcpSocket&& other) noexcept;

	[[nodiscard]] bool ListenLoopback(std::uint16_t port, std::string& error);
	[[nodiscard]] bool ConnectLoopback(std::uint16_t port, std::string& error);
	[[nodiscard]] SocketStatus Accept(TcpSocket& client, std::string& error);
	[[nodiscard]] SocketIoResult Send(std::span<std::byte const> bytes);
	[[nodiscard]] SocketIoResult Receive(std::span<std::byte> bytes);
	[[nodiscard]] std::uint16_t BoundPort(std::string& error) const;
	[[nodiscard]] bool IsOpen() const;
	void Close();

  private:
	explicit TcpSocket(std::uintptr_t handle);
	[[nodiscard]] bool SetNonBlocking(std::string& error);

	static constexpr std::uintptr_t InvalidHandle = ~std::uintptr_t{0};
	std::uintptr_t m_Handle = InvalidHandle;
};
} // namespace rogue::bridge
