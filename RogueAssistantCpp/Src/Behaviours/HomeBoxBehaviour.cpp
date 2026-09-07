#include "Behaviours/HomeBoxBehaviour.h"

#include "Endian.h"
#include "GameConnection.h"
#include "GameData.h"
#include "Log.h"
#include "UserData.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <span>
#include <string>
#include <utility>

void HomeBoxBehaviour::OnAttach(GameConnection&)
{
	m_State = State::First;
	m_Dimensions = {};
	m_LocalBoxCount = 0;
	m_LocalActiveBoxIndices.clear();
	m_RemoteActiveBoxIndices.clear();
	m_ActiveBoxData.clear();
	m_StoredBoxData.clear();
	m_BoxWriteRequests = {};
	m_WaitingForBoxWrite = false;
	m_InitialiseBoxWriteIndex = 0;
	m_WriteFilePath.clear();
	m_HasPendingFileWrite = false;
	m_RequiresReopen = false;
	m_NextSaveAttempt = {};
}

void HomeBoxBehaviour::OnDetach(GameConnection& game)
{
	// The last result can arrive just before disconnect, before OnUpdate removes
	// the completed box. Only completed writes may change the saved box order.
	while (!m_WaitingForBoxWrite && !m_BoxWriteRequests.empty() && m_BoxWriteRequests.front().bytesRemaining == 0)
		m_BoxWriteRequests.pop();
	if (!m_BoxWriteRequests.empty())
	{
		LOG_WARN("Home Box transfer interrupted; kept the last saved storage file and backup");
		game.ReportError("Storage transfer stopped.\nHome Box file was not changed.");
		m_BoxWriteRequests = {};
		m_HasPendingFileWrite = false;
		return;
	}
	HandlePendingFileWrite(game, true);
}

bool HomeBoxBehaviour::ValidateLayout(GameConnection const& game, std::string& error) const
{
	auto const& memory = game.GetObservedGameMemory();
	auto const& header = memory.GetRogueHeader();
	if (header.homeBoxSize != memory.GetHomeBoxStateBlobSize())
	{
		error = "Home Box header size does not match the observed state";
		return false;
	}

	rogue::storage::HomeBoxLayout const layout{
		header.homeBoxSize,			 header.homeLocalBoxCount,			header.homeTotalBoxCount,
		header.homeMinimalBoxOffset, header.homeMinimalBoxSize,			header.homeDestMonOffset,
		header.homeDestMonSize,		 header.homeRemoteIndexOrderOffset, header.homeTrainerIdOffset,
	};
	return rogue::storage::ValidateHomeBoxLayout(layout, error);
}

bool HomeBoxBehaviour::ValidateIndexOrder(std::vector<std::uint8_t> const& indices) const
{
	if (indices.size() != m_Dimensions.remoteBoxCount + m_LocalBoxCount)
		return false;
	std::vector<bool> seen(indices.size(), false);
	for (std::uint8_t index : indices)
	{
		if (index >= seen.size() || seen[index])
			return false;
		seen[index] = true;
	}
	return true;
}

void HomeBoxBehaviour::LoadOfflineData(GameConnection& game, std::uint32_t trainerId)
{
	auto const& header = game.GetObservedGameMemory().GetRogueHeader();
	m_LocalBoxCount = header.homeLocalBoxCount;
	m_Dimensions = {
		header.rogueAssistantCompatVersion,
		header.rogueVersion,
		trainerId,
		header.homeTotalBoxCount - header.homeLocalBoxCount,
		header.homeMinimalBoxSize,
		header.homeDestMonSize,
	};
	m_WriteFilePath = UserData::GetDataDirectory() / std::to_string(m_Dimensions.edition) /
					  std::to_string(m_Dimensions.trainerId) / "boxes.dat";

	m_StoredBoxData.assign(m_Dimensions.remoteBoxCount,
						   BoxData{std::vector<std::uint8_t>(m_Dimensions.metadataSize, 0),
								   std::vector<std::uint8_t>(m_Dimensions.pokemonDataSize, 0)});
	auto const loaded = rogue::storage::LoadHomeBoxFile(m_WriteFilePath, m_Dimensions);
	if (loaded.Succeeded())
	{
		for (auto const& record : loaded.data->records)
		{
			m_StoredBoxData[record.remoteBoxIndex] = BoxData{record.metadata, record.pokemonData};
		}
		LOG_INFO("Loaded Home Box data from %s",
				 loaded.source == rogue::storage::HomeBoxLoadSource::Primary ? "primary" : "backup");
	}
	else if (!loaded.NotFound())
	{
		LOG_ERROR("Home Box load failed: %s", loaded.error.c_str());
		game.ReportError("Cannot load Home Box.\nBack up its files before recovery.");
		game.Disconnect();
		return;
	}

	if (!loaded.warning.empty())
	{
		LOG_WARN("Home Box load warning: %s", loaded.warning.c_str());
		if (loaded.primaryInvalid)
		{
			// Saving will not replace a damaged main file. Do not enable transfers
			// that could remove Pokémon from the game without saving them to disk.
			game.ReportError("Home Box needs recovery.\nBack up its files before continuing.");
			game.Disconnect();
			return;
		}
		else
			game.ReportError("Loaded Home Box with a warning.\nSee RogueAssistant.log for details.");
	}

	m_ActiveBoxData.clear();
	m_ActiveBoxData.reserve(m_LocalBoxCount + m_Dimensions.remoteBoxCount);
	m_InitialiseBoxWriteIndex = m_LocalBoxCount;
	m_State = State::InitialiseBoxData;
}

void HomeBoxBehaviour::OnUpdate(GameConnection& game)
{
	if (!game.GetObservedGameMemory().IsHomeBoxStateValid())
		return;

	std::string layoutError;
	if (!ValidateLayout(game, layoutError))
	{
		LOG_ERROR("%s", layoutError.c_str());
		game.ReportError("Cannot use Home Box.\nThe ROM layout is invalid.");
		game.Disconnect();
		return;
	}
	if (m_RequiresReopen)
		return;

	HandlePendingFileWrite(game);
	auto const& header = game.GetObservedGameMemory().GetRogueHeader();
	std::uint8_t const* homeBoxState = game.GetObservedGameMemory().GetHomeBoxStateBlob();
	GameAddress const writeAddress = game.GetObservedGameMemory().GetHomeBoxStatePtr();

	switch (m_State)
	{
	case State::OpenOfflineFile: {
		// The ROM initializes the final order entry to 255 when Extended Storage
		// opens. A different value before this behaviour has written anything
		// means another assistant session already set up this screen. After a
		// disconnect, the game stays disconnected until the player leaves this
		// screen and opens it again. Do not change storage before that happens.
		if (homeBoxState[header.homeRemoteIndexOrderOffset + header.homeTotalBoxCount - 1] != 255)
		{
			LOG_WARN("Extended Storage was already initialized when this assistant session connected");
			m_RequiresReopen = true;
			return;
		}

		std::uint32_t trainerId = 0;
		std::span<std::uint8_t const> const state(homeBoxState, game.GetObservedGameMemory().GetHomeBoxStateBlobSize());
		if (!rogue::endian::ReadLittle(state, header.homeTrainerIdOffset, trainerId))
		{
			LOG_ERROR("Home Box trainer ID is outside the observed state");
			game.ReportError("Cannot use Home Box.\nThe trainer data is invalid.");
			game.Disconnect();
			return;
		}
		LoadOfflineData(game, trainerId);
		break;
	}

	case State::InitialiseBoxData:
		if (m_ActiveBoxData.size() < m_LocalBoxCount)
		{
			if (game.GetObservedGameMemory().RequestPokemonStorageData(
					static_cast<std::uint32_t>(m_ActiveBoxData.size())))
			{
				m_ActiveBoxData.emplace_back();
				m_State = State::WaitingForBoxData;
			}
			break;
		}

		for (BoxData const& stored : m_StoredBoxData)
			m_ActiveBoxData.push_back(stored);
		if (m_ActiveBoxData.size() != header.homeTotalBoxCount)
		{
			game.ReportError("Cannot use Home Box.\nThe number of boxes changed.");
			game.Disconnect();
			return;
		}
		m_State = State::SendGameDataInit;
		break;

	case State::WaitingForBoxData:
		if (game.GetObservedGameMemory().IsPokemonStorageBlobReady())
		{
			InitialiseLocalBoxData(game, static_cast<std::uint32_t>(m_ActiveBoxData.size() - 1));
			m_State = State::InitialiseBoxData;
		}
		break;

	case State::SendGameDataInit:
		if (m_InitialiseBoxWriteIndex < header.homeTotalBoxCount)
		{
			if (!WriteMinimalBox(game, m_InitialiseBoxWriteIndex,
							 m_ActiveBoxData[m_InitialiseBoxWriteIndex].minimalData.data()))
			{
				break;
			}
			++m_InitialiseBoxWriteIndex;
			if (m_InitialiseBoxWriteIndex < header.homeTotalBoxCount)
				break;
		}

		m_LocalActiveBoxIndices.resize(header.homeTotalBoxCount);
		m_RemoteActiveBoxIndices.resize(header.homeTotalBoxCount);
		for (std::uint32_t index = 0; index < header.homeTotalBoxCount; ++index)
		{
			m_LocalActiveBoxIndices[index] = static_cast<std::uint8_t>(index);
			m_RemoteActiveBoxIndices[index] = static_cast<std::uint8_t>(index);
		}
		if (!game.WriteRequest(CreateAnonymousMessageId(), writeAddress + header.homeRemoteIndexOrderOffset,
						   m_RemoteActiveBoxIndices.data(), m_RemoteActiveBoxIndices.size()))
		{
			break;
		}
		m_State = State::WaitForInit;
		break;

	case State::WaitForInit:
		if (homeBoxState[header.homeRemoteIndexOrderOffset + header.homeTotalBoxCount - 1] != 255)
			m_State = State::Update;
		break;

	case State::Update:
		if (PumpWriteMonBox(game))
			break;

		std::memcpy(m_RemoteActiveBoxIndices.data(), homeBoxState + header.homeRemoteIndexOrderOffset,
					m_RemoteActiveBoxIndices.size());
		if (!ValidateIndexOrder(m_RemoteActiveBoxIndices))
		{
			game.ReportError("Cannot use Home Box.\nThe box order is invalid.");
			game.Disconnect();
			return;
		}

		for (std::uint32_t index = 0; index < header.homeTotalBoxCount; ++index)
		{
			if (m_RemoteActiveBoxIndices[index] == m_LocalActiveBoxIndices[index])
				continue;
			if (index < header.homeLocalBoxCount)
			{
				BeginWriteMonBox(game, index, m_ActiveBoxData[m_RemoteActiveBoxIndices[index]].pokemonData.data());
			}
			m_LocalActiveBoxIndices[index] = m_RemoteActiveBoxIndices[index];
			m_HasPendingFileWrite = true;
		}
		break;
	}
}

void HomeBoxBehaviour::InitialiseLocalBoxData(GameConnection& game, std::uint32_t boxId)
{
	auto const& header = game.GetObservedGameMemory().GetRogueHeader();
	BoxData& target = m_ActiveBoxData[boxId];
	std::uint8_t const* const minimalBox = GetMinimalBoxPtr(game, boxId);
	target.minimalData.assign(minimalBox, minimalBox + header.homeMinimalBoxSize);
	target.pokemonData.assign(game.GetObservedGameMemory().GetPokemonStorageBlob(),
							  game.GetObservedGameMemory().GetPokemonStorageBlob() + header.homeDestMonSize);
}

bool HomeBoxBehaviour::WriteMinimalBox(GameConnection& game, std::uint32_t boxId, std::uint8_t const* data)
{
	auto const& header = game.GetObservedGameMemory().GetRogueHeader();
	GameAddress const writeAddress = game.GetObservedGameMemory().GetHomeBoxStatePtr();
	return game.WriteRequest(CreateAnonymousMessageId(),
						 writeAddress + header.homeMinimalBoxOffset + header.homeMinimalBoxSize * boxId, data,
						 header.homeMinimalBoxSize);
}

std::uint8_t const* HomeBoxBehaviour::GetMinimalBoxPtr(GameConnection& game, std::uint32_t boxId)
{
	auto const& header = game.GetObservedGameMemory().GetRogueHeader();
	return game.GetObservedGameMemory().GetHomeBoxStateBlob() + header.homeMinimalBoxOffset +
		   header.homeMinimalBoxSize * boxId;
}

void HomeBoxBehaviour::BeginWriteMonBox(GameConnection& game, std::uint32_t boxId, std::uint8_t const* data)
{
	auto const& header = game.GetObservedGameMemory().GetRogueHeader();
	m_BoxWriteRequests.push(BoxWriteRequest{boxId, data, 0, header.homeDestMonSize});
}

bool HomeBoxBehaviour::PumpWriteMonBox(GameConnection& game)
{
	if (m_BoxWriteRequests.empty())
		return false;
	if (m_WaitingForBoxWrite)
		return true;

	BoxWriteRequest& request = m_BoxWriteRequests.front();
	if (request.bytesRemaining == 0)
	{
		m_BoxWriteRequests.pop();
		return true;
	}
	auto const& header = game.GetObservedGameMemory().GetRogueHeader();
	GameAddress const writeAddress = game.GetObservedGameMemory().GetPokemonStoragePtr();
	std::size_t const writeSize = std::min<std::size_t>(request.bytesRemaining, 1024);
	std::weak_ptr<HomeBoxBehaviour> const weakSelf =
		std::static_pointer_cast<HomeBoxBehaviour>(shared_from_this());
	m_WaitingForBoxWrite = true;
	if (!game.WriteRequest(CreateAnonymousMessageId(),
					   writeAddress + header.homeDestMonSize * request.boxId + static_cast<GameAddress>(request.offset),
					   request.data + request.offset, writeSize, [weakSelf] {
						   if (auto self = weakSelf.lock())
							   self->m_WaitingForBoxWrite = false;
					   }))
	{
		m_WaitingForBoxWrite = false;
		return true;
	}
	request.bytesRemaining -= writeSize;
	request.offset += writeSize;
	return true;
}

void HomeBoxBehaviour::HandlePendingFileWrite(GameConnection& game, bool force)
{
	if (!m_HasPendingFileWrite || m_WriteFilePath.empty() || m_ActiveBoxData.empty() ||
		m_LocalActiveBoxIndices.empty() || !m_BoxWriteRequests.empty())
	{
		return;
	}
	if (!force && std::chrono::steady_clock::now() < m_NextSaveAttempt)
		return;

	rogue::storage::HomeBoxData data;
	data.dimensions = m_Dimensions;
	data.records.reserve(m_Dimensions.remoteBoxCount);
	for (std::uint32_t remoteIndex = 0; remoteIndex < m_Dimensions.remoteBoxCount; ++remoteIndex)
	{
		std::size_t const slot = m_LocalBoxCount + remoteIndex;
		if (slot >= m_LocalActiveBoxIndices.size() || m_LocalActiveBoxIndices[slot] >= m_ActiveBoxData.size())
		{
			game.ReportError("Cannot save Home Box.\nThe box order is invalid.");
			m_NextSaveAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
			return;
		}
		BoxData const& box = m_ActiveBoxData[m_LocalActiveBoxIndices[slot]];
		data.records.push_back(rogue::storage::HomeBoxRecord{remoteIndex, box.minimalData, box.pokemonData});
	}

	std::string error;
	LOG_INFO("Attempting to save Home Box data");
	if (!rogue::storage::SaveHomeBoxFile(m_WriteFilePath, data, error))
	{
		LOG_ERROR("Home Box save failed: %s", error.c_str());
		game.ReportError("Could not save Home Box.\nThe main file was not changed.");
		m_NextSaveAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		return;
	}
	m_HasPendingFileWrite = false;
	m_NextSaveAttempt = {};
	LOG_INFO("Saved Home Box data");
}
