#include "Bridge/TcpSocket.h"

#include <algorithm>
#include <array>
#include <limits>
#include <mutex>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <WS2tcpip.h>
#include <WinSock2.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace rogue::bridge
{
namespace
{
#if defined(_WIN32)
using NativeSocket = SOCKET;
using SocketLength = int;
constexpr NativeSocket NativeInvalidSocket = INVALID_SOCKET;

bool EnsureSocketRuntime(std::string& error)
{
	static std::once_flag once;
	static int startupResult = WSASYSNOTREADY;
	std::call_once(once, [] {
		WSADATA data{};
		startupResult = WSAStartup(MAKEWORD(2, 2), &data);
	});
	if (startupResult != 0)
	{
		error = "WSAStartup failed with error " + std::to_string(startupResult);
		return false;
	}
	return true;
}

int LastSocketError()
{
	return WSAGetLastError();
}

bool IsWouldBlock(int error)
{
	return error == WSAEWOULDBLOCK;
}

bool IsInterrupted(int error)
{
	return error == WSAEINTR;
}

std::string DescribeSocketError(int error)
{
	return "socket error " + std::to_string(error);
}

void CloseNativeSocket(NativeSocket socket)
{
	closesocket(socket);
}
#else
using NativeSocket = int;
using SocketLength = socklen_t;
constexpr NativeSocket NativeInvalidSocket = -1;

bool EnsureSocketRuntime(std::string&)
{
	return true;
}

int LastSocketError()
{
	return errno;
}

bool IsWouldBlock(int error)
{
	return error == EAGAIN || error == EWOULDBLOCK;
}

bool IsInterrupted(int error)
{
	return error == EINTR;
}

std::string DescribeSocketError(int error)
{
	return std::strerror(error);
}

void CloseNativeSocket(NativeSocket socket)
{
	close(socket);
}
#endif

NativeSocket ToNative(std::uintptr_t handle)
{
	return static_cast<NativeSocket>(handle);
}

std::uintptr_t FromNative(NativeSocket socket)
{
	return static_cast<std::uintptr_t>(socket);
}
} // namespace

TcpSocket::TcpSocket(std::uintptr_t handle) : m_Handle(handle)
{
}

TcpSocket::~TcpSocket()
{
	Close();
}

TcpSocket::TcpSocket(TcpSocket&& other) noexcept : m_Handle(std::exchange(other.m_Handle, InvalidHandle))
{
}

TcpSocket& TcpSocket::operator=(TcpSocket&& other) noexcept
{
	if (this != &other)
	{
		Close();
		m_Handle = std::exchange(other.m_Handle, InvalidHandle);
	}
	return *this;
}

bool TcpSocket::ListenLoopback(std::uint16_t port, std::string& error)
{
	Close();
	if (!EnsureSocketRuntime(error))
		return false;

	NativeSocket const socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == NativeInvalidSocket)
	{
		error = "cannot create bridge listener: " + DescribeSocketError(LastSocketError());
		return false;
	}
	m_Handle = FromNative(socket);

#if defined(_WIN32)
	BOOL exclusive = TRUE;
	if (setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<char const*>(&exclusive),
				   static_cast<SocketLength>(sizeof(exclusive))) != 0)
#else
	int reuse = 1;
	if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, static_cast<SocketLength>(sizeof(reuse))) != 0)
#endif
	{
		error = "cannot configure bridge listener: " + DescribeSocketError(LastSocketError());
		Close();
		return false;
	}

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(socket, reinterpret_cast<sockaddr*>(&address), static_cast<SocketLength>(sizeof(address))) != 0)
	{
		error =
			"cannot listen on 127.0.0.1 port " + std::to_string(port) + ": " + DescribeSocketError(LastSocketError());
		Close();
		return false;
	}
	if (listen(socket, 4) != 0)
	{
		error = "cannot listen for mGBA: " + DescribeSocketError(LastSocketError());
		Close();
		return false;
	}
	return SetNonBlocking(error);
}

bool TcpSocket::ConnectLoopback(std::uint16_t port, std::string& error)
{
	Close();
	if (!EnsureSocketRuntime(error))
		return false;

	NativeSocket const socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (socket == NativeInvalidSocket)
	{
		error = "cannot create bridge client: " + DescribeSocketError(LastSocketError());
		return false;
	}
	m_Handle = FromNative(socket);

	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_port = htons(port);
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(socket, reinterpret_cast<sockaddr*>(&address), static_cast<SocketLength>(sizeof(address))) != 0)
	{
		error = "cannot connect to bridge: " + DescribeSocketError(LastSocketError());
		Close();
		return false;
	}
	return SetNonBlocking(error);
}

SocketStatus TcpSocket::Accept(TcpSocket& client, std::string& error)
{
	if (!IsOpen())
	{
		error = "bridge listener is closed";
		return SocketStatus::Error;
	}

	for (;;)
	{
		NativeSocket const accepted = accept(ToNative(m_Handle), nullptr, nullptr);
		if (accepted != NativeInvalidSocket)
		{
			TcpSocket socket(FromNative(accepted));
			if (!socket.SetNonBlocking(error))
				return SocketStatus::Error;
			client = std::move(socket);
			return SocketStatus::Ok;
		}
		int const socketError = LastSocketError();
		if (IsInterrupted(socketError))
			continue;
		if (IsWouldBlock(socketError))
			return SocketStatus::WouldBlock;
		error = "cannot accept mGBA connection: " + DescribeSocketError(socketError);
		return SocketStatus::Error;
	}
}

SocketIoResult TcpSocket::Send(std::span<std::byte const> bytes)
{
	if (!IsOpen())
		return {SocketStatus::Closed, 0, "socket is closed"};
	if (bytes.empty())
		return {SocketStatus::Ok, 0, {}};

	std::size_t const maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
	int const size = static_cast<int>(std::min(bytes.size(), maximum));
	for (;;)
	{
#if defined(_WIN32)
		int const sent = send(ToNative(m_Handle), reinterpret_cast<char const*>(bytes.data()), size, 0);
#else
#if defined(MSG_NOSIGNAL)
		int const flags = MSG_NOSIGNAL;
#else
		int const flags = 0;
#endif
		ssize_t const sent = send(ToNative(m_Handle), bytes.data(), static_cast<std::size_t>(size), flags);
#endif
		if (sent > 0)
			return {SocketStatus::Ok, static_cast<std::size_t>(sent), {}};
		if (sent == 0)
			return {SocketStatus::Closed, 0, "socket closed during send"};
		int const socketError = LastSocketError();
		if (IsInterrupted(socketError))
			continue;
		if (IsWouldBlock(socketError))
			return {SocketStatus::WouldBlock, 0, {}};
		return {SocketStatus::Error, 0, "cannot send bridge data: " + DescribeSocketError(socketError)};
	}
}

SocketIoResult TcpSocket::Receive(std::span<std::byte> bytes)
{
	if (!IsOpen())
		return {SocketStatus::Closed, 0, "socket is closed"};
	if (bytes.empty())
		return {SocketStatus::Ok, 0, {}};

	std::size_t const maximum = static_cast<std::size_t>(std::numeric_limits<int>::max());
	int const size = static_cast<int>(std::min(bytes.size(), maximum));
	for (;;)
	{
#if defined(_WIN32)
		int const received = recv(ToNative(m_Handle), reinterpret_cast<char*>(bytes.data()), size, 0);
#else
		ssize_t const received = recv(ToNative(m_Handle), bytes.data(), static_cast<std::size_t>(size), 0);
#endif
		if (received > 0)
			return {SocketStatus::Ok, static_cast<std::size_t>(received), {}};
		if (received == 0)
			return {SocketStatus::Closed, 0, "peer closed the bridge connection"};
		int const socketError = LastSocketError();
		if (IsInterrupted(socketError))
			continue;
		if (IsWouldBlock(socketError))
			return {SocketStatus::WouldBlock, 0, {}};
		return {SocketStatus::Error, 0, "cannot receive bridge data: " + DescribeSocketError(socketError)};
	}
}

std::uint16_t TcpSocket::BoundPort(std::string& error) const
{
	if (!IsOpen())
	{
		error = "socket is closed";
		return 0;
	}
	sockaddr_in address{};
#if defined(_WIN32)
	int size = sizeof(address);
#else
	socklen_t size = sizeof(address);
#endif
	if (getsockname(ToNative(m_Handle), reinterpret_cast<sockaddr*>(&address), &size) != 0)
	{
		error = "cannot inspect bridge port: " + DescribeSocketError(LastSocketError());
		return 0;
	}
	error.clear();
	return ntohs(address.sin_port);
}

bool TcpSocket::IsOpen() const
{
	return m_Handle != InvalidHandle;
}

void TcpSocket::Close()
{
	if (!IsOpen())
		return;
	CloseNativeSocket(ToNative(m_Handle));
	m_Handle = InvalidHandle;
}

bool TcpSocket::SetNonBlocking(std::string& error)
{
	if (!IsOpen())
	{
		error = "socket is closed";
		return false;
	}
#if defined(_WIN32)
	u_long enabled = 1;
	if (ioctlsocket(ToNative(m_Handle), FIONBIO, &enabled) != 0)
#else
	int const flags = fcntl(ToNative(m_Handle), F_GETFL, 0);
	if (flags < 0 || fcntl(ToNative(m_Handle), F_SETFL, flags | O_NONBLOCK) != 0)
#endif
	{
		error = "cannot make bridge socket nonblocking: " + DescribeSocketError(LastSocketError());
		Close();
		return false;
	}
#if defined(__APPLE__)
	int noSigPipe = 1;
	if (setsockopt(ToNative(m_Handle), SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe)) != 0)
	{
		error = "cannot suppress SIGPIPE: " + DescribeSocketError(LastSocketError());
		Close();
		return false;
	}
#endif
	error.clear();
	return true;
}
} // namespace rogue::bridge
