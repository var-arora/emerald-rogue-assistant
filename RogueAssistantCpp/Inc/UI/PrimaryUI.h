#pragma once
#include "Application/UiModel.h"
#include "Timer.h"
#include "UI/RefreshState.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

struct AssetCollection;

class Window;

class PrimaryUI
{
public:
  explicit PrimaryUI(std::filesystem::path const& resourceDirectory = {});
  ~PrimaryUI();

  using CommandSink = std::function<bool(rogue::app::UiCommand)>;

  bool Render(Window& window, rogue::app::UiSnapshot const& snapshot, CommandSink const& submitCommand);
  void SetToStubTheme();

private:
	void RenderAwaitingPage(Window& window);
	void RenderMultiplayerPage(Window& window, rogue::app::UiSnapshot const& snapshot,
		rogue::app::ConnectionSnapshot const& connection, bool initialLoad, CommandSink const& submitCommand);
	void RenderHomeBoxPage(Window& window, rogue::app::HomeBoxSnapshot const& homeBox, bool initialLoad);
	void RenderBridgeControls(Window& window, rogue::app::UiSnapshot const& snapshot, CommandSink const& submitCommand);

	std::unique_ptr<AssetCollection> m_Assets;

	int m_CurrentConnectionIdx = 0;
	std::uint64_t m_CurrentConnectionId = 0;
	rogue::ui::RefreshState m_Refresh;
	rogue::app::UiPage m_CurrentPage;
	bool m_EditingBridgePort = false;
	std::string m_ActionMessage;
};
