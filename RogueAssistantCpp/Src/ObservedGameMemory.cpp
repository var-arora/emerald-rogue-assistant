#include "ObservedGameMemory.h"
#include "Bridge/BridgeProtocol.h"
#include "Endian.h"
#include "GameConnection.h"
#include "Log.h"

namespace
{
constexpr std::uint32_t MaximumObservedReadSize =
	rogue::bridge::MaximumFrameBodyLength - rogue::bridge::FrameBodyHeaderSize;
}

enum class ObservedMemoryID : u16
{
	GFHeader,
	RogueHeader,
	AssitantState,
	MultiplayerStatePtr,
	MultiplayerState,
	HomeBoxStatePtr,
	HomeBoxState,
	GamePokemonStorageData,
};

inline static GameMessageID CreateMessageId(GameMessageChannel channel, ObservedMemoryID param)
{
	return ::CreateMessageId(channel, static_cast<u16>(param));
}

ObservedBlob::ObservedBlob(size_t size) : m_IsValid(false)
{
	Resize(size);
}

void ObservedBlob::Resize(size_t size)
{
	if (m_Data.size() != size)
		m_IsValid = false;
	m_Data.resize(size);
}

bool ObservedBlob::SetData(u8 const* data, size_t size)
{
	ASSERT_MSG(size == GetSize() && (size == 0 || data != nullptr), "Unexpected observed data");
	if (size == GetSize() && (size == 0 || data != nullptr))
	{
		if (size != 0)
			std::memcpy(m_Data.data(), data, GetSize());
		m_IsValid = true;
		return true;
	}

	return false;
}

void ObservedBlob::Clear()
{
	m_IsValid = false;
}

ObservedGameMemory::ObservedGameMemory(GameConnection& game) : m_Game(game)
{
}

void ObservedGameMemory::Update()
{
	if (!m_GFRomHeader.IsValid())
	{
		GameMessageID messageId = CreateMessageId(GameMessageChannel::CommonRead, ObservedMemoryID::GFHeader);
		m_Game.ReadRequest(messageId, GameAddresses::c_GFHeaderAddress, m_GFRomHeader.GetSize());
	}
	else if (!m_RogueHeader.IsValid())
	{
		GameMessageID messageId = CreateMessageId(GameMessageChannel::CommonRead, ObservedMemoryID::RogueHeader);
		m_Game.ReadRequest(messageId, m_GFRomHeader->rogueAssistantHeader, m_RogueHeader.GetSize());
	}
	else
	{
		// Both headers are valid. Read the dynamic state.
		GameMessageID messageId;

		// Read the multiplayer state when the ROM provides it.
		messageId = CreateMessageId(GameMessageChannel::CommonRead, ObservedMemoryID::MultiplayerStatePtr);
		m_Game.ReadRequest(messageId, m_RogueHeader->multiplayerPtr, m_MultiplayerStatePtr.GetSize());

		if (m_MultiplayerStatePtr.IsValid() && m_MultiplayerStatePtr.Get() != 0)
		{
			if (m_RogueHeader->netMultiplayerSize == 0 || m_RogueHeader->netMultiplayerSize > MaximumObservedReadSize)
			{
				LOG_ERROR("Multiplayer state size must be from 1 through %zu bytes", MaximumObservedReadSize);
				m_Game.ReportError("Cannot start multiplayer.\nThe ROM multiplayer data is invalid.");
				m_Game.Disconnect();
				return;
			}
			if (m_MultiplayerState.GetSize() != m_RogueHeader->netMultiplayerSize)
				m_MultiplayerState.Resize(m_RogueHeader->netMultiplayerSize);

			messageId = CreateMessageId(GameMessageChannel::CommonRead, ObservedMemoryID::MultiplayerState);
			m_Game.ReadRequest(messageId, m_MultiplayerStatePtr.Get(), m_MultiplayerState.GetSize());
		}
		else
		{
			// A null pointer means that multiplayer is inactive.
			m_MultiplayerState.Clear();
		}

		// Read the Home Box state when the ROM provides it.
		messageId = CreateMessageId(GameMessageChannel::CommonRead, ObservedMemoryID::HomeBoxStatePtr);
		m_Game.ReadRequest(messageId, m_RogueHeader->homeBoxPtr, m_HomeBoxStatePtr.GetSize());

		if (m_HomeBoxStatePtr.IsValid() && m_HomeBoxStatePtr.Get() != 0)
		{
			if (m_RogueHeader->homeBoxSize == 0 || m_RogueHeader->homeBoxSize > MaximumObservedReadSize)
			{
				LOG_ERROR("Home Box state size must be from 1 through %zu bytes", MaximumObservedReadSize);
				m_Game.ReportError("Cannot use Home Box.\nThe ROM Home Box data is invalid.");
				m_Game.Disconnect();
				return;
			}
			if (m_HomeBoxState.GetSize() != m_RogueHeader->homeBoxSize)
				m_HomeBoxState.Resize(m_RogueHeader->homeBoxSize);

			messageId = CreateMessageId(GameMessageChannel::CommonRead, ObservedMemoryID::HomeBoxState);
			m_Game.ReadRequest(messageId, m_HomeBoxStatePtr.Get(), m_HomeBoxState.GetSize());
		}
		else
		{
			// A null pointer means that Home Box is inactive.
			m_HomeBoxState.Clear();
			m_PokemonStorageData.Clear();
		}
	}
}

void ObservedGameMemory::OnRecieveMessage(GameMessageID messageId, u8 const* data, size_t size)
{
	ObservedMemoryID memoryId = static_cast<ObservedMemoryID>(messageId.GetParam16());

	switch (memoryId)
	{
	case ObservedMemoryID::GFHeader:
		if (m_GFRomHeader.SetData(data, size))
		{
			// Validate both handshake values before following the assistant header pointer.
			if (m_GFRomHeader->rogueAssistantHandshake1 != 20012 || m_GFRomHeader->rogueAssistantHandshake2 != 30035)
			{
				LOG_WARN("Invalid Game Freak ROM header handshake");
				m_Game.Disconnect();
				return;
			}
		}
		else
		{
			LOG_WARN("Invalid Game Freak ROM header size");
			m_Game.Disconnect();
		}
		break;

	case ObservedMemoryID::RogueHeader:
		if (!m_RogueHeader.SetData(data, size))
		{
			LOG_WARN("Invalid ROM Assistant header size");
			m_Game.Disconnect();
		}
		break;

	case ObservedMemoryID::AssitantState:
		m_AssistantState.SetData(data, size);
		break;

	case ObservedMemoryID::MultiplayerStatePtr:
		m_MultiplayerStatePtr.SetData(data, size);
		break;

	case ObservedMemoryID::MultiplayerState:
		m_MultiplayerState.SetData(data, size);
		break;

	case ObservedMemoryID::HomeBoxStatePtr:
		m_HomeBoxStatePtr.SetData(data, size);
		break;

	case ObservedMemoryID::HomeBoxState:
		m_HomeBoxState.SetData(data, size);
		break;

	case ObservedMemoryID::GamePokemonStorageData:
		m_PokemonStorageData.SetData(data, size);
		break;
	}
}

bool ObservedGameMemory::AreHeadersValid() const
{
	return m_GFRomHeader.IsValid() && m_RogueHeader.IsValid();
}

bool ObservedGameMemory::IsMultiplayerStateValid() const
{
	return m_MultiplayerStatePtr.IsValid() && m_MultiplayerStatePtr.Get() != 0 && m_MultiplayerState.IsValid();
}

bool ObservedGameMemory::IsHomeBoxStateValid() const
{
	return m_HomeBoxStatePtr.IsValid() && m_HomeBoxStatePtr.Get() != 0 && m_HomeBoxState.IsValid();
}

GameAddress ObservedGameMemory::GetPokemonStoragePtr() const
{
	if (IsHomeBoxStateValid())
	{
		GameAddress storagePtrAddr = 0;
		std::span<u8 const> const state(m_HomeBoxState.GetData(), m_HomeBoxState.GetSize());
		if (rogue::endian::ReadLittle(state, m_RogueHeader->homeDestMonOffset, storagePtrAddr))
			return storagePtrAddr;
		LOG_WARN("Home Box destination pointer is outside the observed state");
	}

	return 0;
}

bool ObservedGameMemory::RequestPokemonStorageData(u32 boxId)
{
	m_PokemonStorageData.Clear();

	if (IsHomeBoxStateValid())
	{
		GameStructures::RogueAssistantHeader const& rogueHeader = m_Game.GetObservedGameMemory().GetRogueHeader();

		GameMessageID messageId =
			CreateMessageId(GameMessageChannel::CommonRead, ObservedMemoryID::GamePokemonStorageData);

		if (!m_Game.ReadRequest(messageId, GetPokemonStoragePtr() + rogueHeader.homeDestMonSize * boxId,
						 rogueHeader.homeDestMonSize))
		{
			return false;
		}
		m_PokemonStorageData.Resize(rogueHeader.homeDestMonSize);
		return true;
	}

	return false;
}
