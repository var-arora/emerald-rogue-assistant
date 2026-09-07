#include "Behaviours/CommonBehaviour.h"
#include "Behaviours/HomeBoxBehaviour.h"
#include "Behaviours/MultiplayerBehaviour.h"
#include "GameConnection.h"
#include "GameData.h"
#include "Log.h"
#include "RomCompatibility.h"

#include <limits>

void CommonBehaviour::OnAttach(GameConnection&)
{
}

void CommonBehaviour::OnDetach(GameConnection&)
{
}

void CommonBehaviour::OnUpdate(GameConnection& game)
{
	if (!game.IsMemoryReadable())
		return;

	GameStructures::RogueAssistantHeader const& rogueHeader = game.GetObservedGameMemory().GetRogueHeader();

	if (rogueHeader.rogueAssistantCompatVersion != rogue::rom::RequiredAssistantApi)
	{
		LOG_WARN("Unsupported ROM Assistant API: %u", rogueHeader.rogueAssistantCompatVersion);
		game.ReportError("This game version is not supported.\nCheck for an assistant update.");
		game.Disconnect();
		return;
	}
	if (!rogue::rom::IsSupportedEdition(rogueHeader.rogueVersion))
	{
		game.ReportError("This ROM is not compatible.\nUse the Vanilla or EX edition.");
		game.Disconnect();
		return;
	}

	// Notify the game that the assistant is connected by repeatedly writing zero
	// to the ROM-provided confirmation field. API 3 uses one integer. Check the
	// ROM's size and address before making a request, so invalid values cannot
	// cause a read past this local value or an address overflow.
	if (rogueHeader.assistantConfirmSize == 0 || rogueHeader.assistantConfirmSize > sizeof(u32) ||
		rogueHeader.assistantState > std::numeric_limits<GameAddress>::max() - rogueHeader.assistantConfirmOffset)
	{
		game.ReportError("Cannot connect.\nThe ROM connection data is invalid.");
		game.Disconnect();
		return;
	}

	u32 value = 0;
	game.WriteRequest(CreateAnonymousMessageId(), rogueHeader.assistantState + rogueHeader.assistantConfirmOffset,
					  &value, rogueHeader.assistantConfirmSize);

	// Attach or remove multiplayer behavior when the ROM changes its request.
	if (game.GetObservedGameMemory().IsMultiplayerStateValid())
	{
		if (m_MultiplayerBehaviour.expired())
		{
			u8 const* multiplayerBlob = game.GetObservedGameMemory().GetMultiplayerStateBlob();
			if (rogueHeader.netRequestStateOffset >=
				game.GetObservedGameMemory().GetMultiplayerStateBlobSize())
			{
				game.ReportError("Cannot start multiplayer.\nThe ROM multiplayer data is invalid.");
				game.Disconnect();
				return;
			}

			u8 requestFlags = multiplayerBlob[rogueHeader.netRequestStateOffset];

			if (requestFlags != 0)
			{
				m_MultiplayerBehaviour = game.AddBehaviour<MultiplayerBehaviour>();
			}
		}
	}
	else
	{
		auto multiplayer = m_MultiplayerBehaviour.lock();

		if (multiplayer != nullptr)
		{
			game.RemoveBehaviour(multiplayer.get());
			m_MultiplayerBehaviour.reset();
		}
	}

	// Attach or remove Home Box behavior when the ROM changes its request.
	if (game.GetObservedGameMemory().IsHomeBoxStateValid())
	{
		if (m_HomeBoxBehaviour.expired())
		{
			m_HomeBoxBehaviour = game.AddBehaviour<HomeBoxBehaviour>();
		}
	}
	else
	{
		auto homeBox = m_HomeBoxBehaviour.lock();

		if (homeBox != nullptr)
		{
			game.RemoveBehaviour(homeBox.get());
			m_HomeBoxBehaviour.reset();
		}
	}
}
