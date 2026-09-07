#include "Multiplayer/MultiplayerProtocol.h"

#include "Endian.h"

#include <algorithm>
#include <array>

namespace rogue::multiplayer
{
namespace
{
constexpr std::array<std::byte, 4> Magic{std::byte{'R'}, std::byte{'A'}, std::byte{'M'}, std::byte{'P'}};

void AppendLittle(std::vector<std::byte>& output, std::uint16_t value)
{
	std::size_t const offset = output.size();
	output.resize(offset + sizeof(value));
	(void)endian::WriteLittle(output, offset, value);
}

void AppendLittle(std::vector<std::byte>& output, std::uint32_t value)
{
	std::size_t const offset = output.size();
	output.resize(offset + sizeof(value));
	(void)endian::WriteLittle(output, offset, value);
}

template <typename T> bool ReadLittle(std::span<std::byte const> input, std::size_t& position, T& value)
{
	if (!endian::ReadLittle(input, position, value))
		return false;
	position += sizeof(T);
	return true;
}

bool ValidateHello(Hello const& hello, std::string& error)
{
	if (hello.protocolMajor == 0)
	{
		error = "multiplayer protocol major must not be zero";
		return false;
	}
	if (hello.romAssistantApi != RequiredRomAssistantApi)
	{
		error = "multiplayer requires ROM Assistant API 3";
		return false;
	}
	if (!rom::IsSupportedEdition(hello.edition))
	{
		error = "multiplayer ROM edition is neither Vanilla nor EX";
		return false;
	}
	if (hello.playerCount == 0 || hello.playerCount > 255 || hello.multiplayerStateSize == 0 ||
		hello.handshakeSize == 0 || hello.gameStateSize == 0 || hello.playerProfileSize == 0 ||
		hello.playerStateSize == 0)
	{
		error = "multiplayer player count or structure size is invalid";
		return false;
	}
	return true;
}
} // namespace

bool EncodeHello(Hello const& hello, std::vector<std::byte>& encoded, std::string& error)
{
	encoded.clear();
	error.clear();
	if (!ValidateHello(hello, error))
		return false;

	encoded.reserve(HelloSize);
	encoded.insert(encoded.end(), Magic.begin(), Magic.end());
	AppendLittle(encoded, hello.protocolMajor);
	AppendLittle(encoded, hello.protocolMinor);
	AppendLittle(encoded, hello.romAssistantApi);
	encoded.push_back(static_cast<std::byte>(hello.edition));
	encoded.insert(encoded.end(), 3, std::byte{0});
	AppendLittle(encoded, hello.playerCount);
	AppendLittle(encoded, hello.multiplayerStateSize);
	AppendLittle(encoded, hello.handshakeSize);
	AppendLittle(encoded, hello.gameStateSize);
	AppendLittle(encoded, hello.playerProfileSize);
	AppendLittle(encoded, hello.playerStateSize);
	return encoded.size() == HelloSize;
}

bool DecodeHello(std::span<std::byte const> encoded, Hello& hello, std::string& error)
{
	error.clear();
	if (encoded.size() != HelloSize)
	{
		error = "multiplayer hello has an invalid size";
		return false;
	}
	if (!std::equal(Magic.begin(), Magic.end(), encoded.begin()))
	{
		error = "multiplayer hello has invalid RAMP magic";
		return false;
	}

	Hello decoded;
	std::size_t position = Magic.size();
	std::uint8_t reserved = 0;
	if (!ReadLittle(encoded, position, decoded.protocolMajor) ||
		!ReadLittle(encoded, position, decoded.protocolMinor) ||
		!ReadLittle(encoded, position, decoded.romAssistantApi) || position >= encoded.size())
	{
		error = "multiplayer hello is truncated";
		return false;
	}
	decoded.edition = std::to_integer<std::uint8_t>(encoded[position++]);
	for (int index = 0; index < 3; ++index)
	{
		if (position >= encoded.size())
		{
			error = "multiplayer hello is truncated";
			return false;
		}
		reserved |= std::to_integer<std::uint8_t>(encoded[position++]);
	}
	if (reserved != 0 || !ReadLittle(encoded, position, decoded.playerCount) ||
		!ReadLittle(encoded, position, decoded.multiplayerStateSize) ||
		!ReadLittle(encoded, position, decoded.handshakeSize) ||
		!ReadLittle(encoded, position, decoded.gameStateSize) ||
		!ReadLittle(encoded, position, decoded.playerProfileSize) ||
		!ReadLittle(encoded, position, decoded.playerStateSize) || position != encoded.size())
	{
		error = reserved != 0 ? "multiplayer hello reserved fields are nonzero" : "multiplayer hello is truncated";
		return false;
	}
	if (!ValidateHello(decoded, error))
		return false;
	hello = decoded;
	return true;
}

CompatibilityResult CheckCompatibility(Hello const& local, Hello const& remote)
{
	CompatibilityResult result;
	std::string validationError;
	if (!ValidateHello(local, validationError))
	{
		result.error = "The local multiplayer data is invalid.";
		return result;
	}
	if (!ValidateHello(remote, validationError))
	{
		if (remote.romAssistantApi != RequiredRomAssistantApi)
			result.error = "The other game version\nis not supported.";
		else if (!rom::IsSupportedEdition(remote.edition))
			result.error = "The other player uses a different ROM edition.";
		else
			result.error = "The other player sent invalid multiplayer data.";
		return result;
	}
	if (local.protocolMajor != remote.protocolMajor)
		result.error = "Use the same assistant version\non both computers.";
	else if (local.romAssistantApi != remote.romAssistantApi)
		result.error = "The other game version\nis not supported.";
	else if (local.edition != remote.edition)
		result.error = "The other player uses a different ROM edition.";
	else if (local.playerCount != remote.playerCount)
		result.error = "The games support different numbers of players.";
	else if (local.multiplayerStateSize != remote.multiplayerStateSize || local.handshakeSize != remote.handshakeSize ||
			 local.gameStateSize != remote.gameStateSize || local.playerProfileSize != remote.playerProfileSize ||
			 local.playerStateSize != remote.playerStateSize)
		result.error = "The game versions do not match.";
	else
	{
		result.compatible = true;
		result.negotiatedMinor = std::min(local.protocolMinor, remote.protocolMinor);
	}
	return result;
}
} // namespace rogue::multiplayer
