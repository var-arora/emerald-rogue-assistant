#pragma once
#include "Bridge/GameMemoryTransport.h"
#include "Bridge/GameSession.h"
#include "Defines.h"
#include "GameConnectionBehaviour.h"
#include "GameConnectionMessage.h"
#include "GameData.h"
#include "ObservedGameMemory.h"
#include "Timer.h"

#include <functional>
#include <memory>

class GameConnectionManager;
class IGameConnectionTask;

enum class GameConnectionState
{
	AwaitingFirstHandshake,
	AwaitingSecondHandshake,
	Connected,
	Disconnected
};

class GameConnection : public std::enable_shared_from_this<GameConnection>
{
  public:
	explicit GameConnection(GameConnectionManager& manager, TimeDurationNS updateInterval = UpdateTimer::c_10UPS);
	GameConnection(GameConnectionManager& manager, std::shared_ptr<IGameMemoryTransport> transport,
				   TimeDurationNS updateInterval = UpdateTimer::c_10UPS);
	~GameConnection();

	void Update();
	void Disconnect();

	void AddBehaviour(IGameConnectionBehaviour* behaviour);
	void RemoveBehaviour(IGameConnectionBehaviour* behaviour);

	template <typename T> std::shared_ptr<T> AddBehaviour();

	template <typename T> std::shared_ptr<T> FindBehaviour();

	inline bool IsReady() const
	{
		return m_State == GameConnectionState::Connected;
	}
	inline bool HasDisconnected() const
	{
		return m_State == GameConnectionState::Disconnected;
	}

	inline bool IsMemoryReadable() const
	{
		return IsReady() && GetObservedGameMemory().AreHeadersValid();
	}

	bool WriteRequest(GameMessageID messageId, GameAddress addr, void const* data, size_t size,
					  std::function<void()> onSuccess = {});
	bool ReadRequest(GameMessageID messageId, GameAddress addr, size_t size);
	void ReportError(std::string error);

	ObservedGameMemory& GetObservedGameMemory();
	ObservedGameMemory const& GetObservedGameMemory() const;

  private:
	void AddDefaultBehaviours();
	bool RemoveBehaviourInternal(IGameConnectionBehaviour* behaviour);

	void OnRecieveMessage(GameMessageID messageId, u8 const* data, size_t size);
	void OnMemoryResult(GameMessageID messageId, MemoryResult result);

	GameConnectionManager& m_Manager;
	GameConnectionState m_State;
	UpdateTimer m_UpdateTimer;

	std::unique_ptr<GameSession> m_GameSession;
	std::unique_ptr<ObservedGameMemory> m_ObservedGameMemory;

	std::vector<GameConnectionBehaviourRef> m_Behaviours;
	std::vector<GameConnectionBehaviourRef> m_BehavioursToRemove;
};

// Templates
//
template <typename T> std::shared_ptr<T> GameConnection::AddBehaviour()
{
	std::shared_ptr<T> ptr = std::make_shared<T>();
	AddBehaviour(ptr.get());
	return ptr;
}

template <typename T> std::shared_ptr<T> GameConnection::FindBehaviour()
{
	for (auto& ref : m_Behaviours)
	{
		std::shared_ptr<T> result = std::dynamic_pointer_cast<T>(ref);
		if (result != nullptr)
			return result;
	}

	return nullptr;
}
