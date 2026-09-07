#pragma once

#include "GameConnectionBehaviour.h"
#include "Storage/HomeBoxFile.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <queue>
#include <string>
#include <vector>

class HomeBoxBehaviour : public IGameConnectionBehaviour
{
  public:
	void OnAttach(GameConnection& game) override;
	void OnDetach(GameConnection& game) override;
	void OnUpdate(GameConnection& game) override;

	[[nodiscard]] bool IsLoading() const
	{
		return !m_RequiresReopen && m_State < State::Update;
	}
	[[nodiscard]] bool IsSaving() const
	{
		return m_HasPendingFileWrite;
	}
	[[nodiscard]] bool RequiresReopen() const
	{
		return m_RequiresReopen;
	}

  private:
	enum class State
	{
		OpenOfflineFile,
		InitialiseBoxData,
		WaitingForBoxData,
		SendGameDataInit,
		WaitForInit,
		Update,

		First = OpenOfflineFile,
	};

	struct BoxData
	{
		std::vector<std::uint8_t> minimalData;
		std::vector<std::uint8_t> pokemonData;
	};

	struct BoxWriteRequest
	{
		std::uint32_t boxId = 0;
		std::uint8_t const* data = nullptr;
		std::size_t offset = 0;
		std::size_t bytesRemaining = 0;
	};

	[[nodiscard]] bool ValidateLayout(GameConnection const& game, std::string& error) const;
	[[nodiscard]] bool ValidateIndexOrder(std::vector<std::uint8_t> const& indices) const;
	void LoadOfflineData(GameConnection& game, std::uint32_t trainerId);
	void InitialiseLocalBoxData(GameConnection& game, std::uint32_t boxId);
	void HandlePendingFileWrite(GameConnection& game, bool force = false);

	[[nodiscard]] bool WriteMinimalBox(GameConnection& game, std::uint32_t boxId, std::uint8_t const* data);
	[[nodiscard]] std::uint8_t const* GetMinimalBoxPtr(GameConnection& game, std::uint32_t boxId);

	void BeginWriteMonBox(GameConnection& game, std::uint32_t boxId, std::uint8_t const* data);
	[[nodiscard]] bool PumpWriteMonBox(GameConnection& game);

	State m_State = State::First;
	rogue::storage::HomeBoxDimensions m_Dimensions;
	std::uint32_t m_LocalBoxCount = 0;
	std::vector<std::uint8_t> m_LocalActiveBoxIndices;
	std::vector<std::uint8_t> m_RemoteActiveBoxIndices;
	std::vector<BoxData> m_ActiveBoxData;
	std::vector<BoxData> m_StoredBoxData;
	std::queue<BoxWriteRequest> m_BoxWriteRequests;
	bool m_WaitingForBoxWrite = false;
	std::uint32_t m_InitialiseBoxWriteIndex = 0;

	std::filesystem::path m_WriteFilePath;
	bool m_HasPendingFileWrite = false;
	bool m_RequiresReopen = false;
	std::chrono::steady_clock::time_point m_NextSaveAttempt{};
};
