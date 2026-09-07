#pragma once

#include "Application/UiModel.h"

#include <cstddef>
#include <optional>
#include <utility>

namespace rogue::ui
{
class RefreshState
{
  public:
	bool ShouldDraw(app::UiSnapshot snapshot, bool windowChanged, unsigned int animationFrame,
					std::size_t connectionIndex, bool editingPort)
	{
		// Worker revisions change every tick, even when the displayed values do not.
		snapshot.revision = 0;
		bool const animate = HasAnimation(snapshot, connectionIndex);
		if (!windowChanged && m_LastSnapshot == snapshot && connectionIndex == m_ConnectionIndex &&
			editingPort == m_EditingPort && (!animate || animationFrame == m_AnimationFrame))
		{
			return false;
		}
		m_LastSnapshot = std::move(snapshot);
		m_AnimationFrame = animationFrame;
		m_ConnectionIndex = connectionIndex;
		m_EditingPort = editingPort;
		return true;
	}

  private:
	static bool HasAnimation(app::UiSnapshot const& snapshot, std::size_t connectionIndex)
	{
		if (snapshot.connections.empty())
			return true;
		auto const& connection = snapshot.connections[connectionIndex % snapshot.connections.size()];
		switch (connection.page)
		{
		case app::UiPage::Multiplayer:
			return connection.multiplayer.awaitingAddress || !connection.multiplayer.connected;
		case app::UiPage::HomeBox:
			return !connection.homeBox.requiresReopen && (connection.homeBox.loading || connection.homeBox.saving);
		case app::UiPage::Awaiting:
			return false;
		}
		return false;
	}

	std::optional<app::UiSnapshot> m_LastSnapshot;
	unsigned int m_AnimationFrame = 0;
	std::size_t m_ConnectionIndex = 0;
	bool m_EditingPort = false;
};
} // namespace rogue::ui
