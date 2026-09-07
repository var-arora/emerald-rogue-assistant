#include "UI/PrimaryUI.h"
#include "Defines.h"
#include "Log.h"
#include "Platform/BridgeScript.h"
#include "Platform/ResourceLocator.h"
#include "Platform/Utf8.h"
#include "RogueAssistantVersion.h"
#include "UI/TextCache.h"
#include "UI/TextEncoding.h"
#include "UI/Window.h"

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <vector>

// The view dimensions match the Poketch frame asset.
static int const c_ViewWidth = 256;
static int const c_ViewHeight = 192;
static double const c_ViewAspectW = 4;
static double const c_ViewAspectH = 3;

static sf::Vector2f const c_CentreOffset(-16, 0);

static std::string SanitiseConnectionAddress(std::string const& address, bool requestingHost)
{
	if (!requestingHost)
		return address;

	std::string result;
	for (char character : address)
	{
		if (character >= '0' && character <= '9')
			result += character;
	}
	return result;
}

static bool LoadFont(sf::Font& output, std::filesystem::path const& resourceDirectory)
{
	if (resourceDirectory.empty())
		return false;
	rogue::platform::ResourceLocator const resources(resourceDirectory);
	return output.openFromFile(resources.Resolve(rogue::platform::Resource::Font));
}

static bool LoadFrame(sf::Texture& output, std::filesystem::path const& resourceDirectory)
{
	if (resourceDirectory.empty())
		return false;
	rogue::platform::ResourceLocator const resources(resourceDirectory);
	return output.loadFromFile(resources.Resolve(rogue::platform::Resource::Frame));
}

struct AssetCollection
{
	std::string m_LoadingSpinnerAnimText;
	std::string m_CursorPosAnimText;

	sf::Color m_ClearColour;
	sf::Color m_DarkFontColour;
	sf::Color m_LightFontColour;
	sf::Color m_ErrorFontColour;
	sf::Font m_Font;
	sf::Texture m_PoketchOverlay;
	rogue::ui::TextCache m_Text{m_Font};

	explicit AssetCollection(std::filesystem::path const& resourceDirectory)
	{
		m_ClearColour = sf::Color(112, 176, 112);
		m_DarkFontColour = sf::Color(16, 40, 24);
		m_LightFontColour = sf::Color(56, 80, 48);
		m_ErrorFontColour = sf::Color(196, 24, 24);

		bool fontLoaded = LoadFont(m_Font, resourceDirectory);
		if (!fontLoaded)
		{
			throw std::runtime_error("Cannot load the application font.");
		}
		m_Font.setSmooth(true);

		bool frameLoaded = LoadFrame(m_PoketchOverlay, resourceDirectory);
		if (!frameLoaded)
		{
			throw std::runtime_error("Cannot load the application frame image.");
		}
		m_PoketchOverlay.setSmooth(false);
	}

	sf::Text& CreateText(sf::RenderWindow const& gfx, std::string const& msg, int fontSize)
	{
		// Layout uses a 256x192 view. Draw text near the window's pixel size, then
		// scale it back to view units. Limit font sizes to keep the font cache
		// from growing too large while the window is resized.
		sf::Vector2u const framebufferSize = gfx.getSize();
		sf::Vector2f const viewSize = gfx.getView().getSize();
		float rasterScale = 1.0F;
		if (viewSize.x > 0.0F && viewSize.y > 0.0F)
		{
			rasterScale = std::clamp(
				std::ceil(std::max(framebufferSize.x / viewSize.x, framebufferSize.y / viewSize.y)), 1.0F, 8.0F);
		}

		unsigned int const rasterSize = static_cast<unsigned int>(std::ceil(fontSize * rasterScale));
		sf::Text& text = m_Text.Get(rogue::ui::DecodeUtf8(msg), rasterSize);
		text.setScale(sf::Vector2f(1.0F / rasterScale, 1.0F / rasterScale));
		return text;
	}

	void DrawCenteredText(sf::RenderWindow& gfx, std::string const& msg, sf::Vector2f pos, int fontSize,
						  sf::Color const& colour)
	{
		sf::Text& text = CreateText(gfx, msg, fontSize);
		text.setFillColor(colour);
		text.setOrigin(sf::Vector2f(text.getLocalBounds().size.x / 2, 0));
		text.setPosition(pos);
		gfx.draw(text);
	}

	void DrawLeftAlignedText(sf::RenderWindow& gfx, std::string const& msg, sf::Vector2f pos, int fontSize,
							 sf::Color const& colour)
	{
		sf::Text& text = CreateText(gfx, msg, fontSize);
		text.setFillColor(colour);
		text.setOrigin(sf::Vector2f(0, 0));
		text.setPosition(pos);
		gfx.draw(text);
	}

	void DrawRightAlignedText(sf::RenderWindow& gfx, std::string const& msg, sf::Vector2f pos, int fontSize,
							  sf::Color const& colour)
	{
		sf::Text& text = CreateText(gfx, msg, fontSize);
		text.setFillColor(colour);
		text.setOrigin(sf::Vector2f(text.getLocalBounds().size.x, 0));
		text.setPosition(pos);
		gfx.draw(text);
	}
};

PrimaryUI::PrimaryUI(std::filesystem::path const& resourceDirectory)
	: m_Assets(std::make_unique<AssetCollection>(resourceDirectory)), m_CurrentPage(rogue::app::UiPage::Awaiting)
{
}

PrimaryUI::~PrimaryUI() = default;

void PrimaryUI::SetToStubTheme()
{
	m_Assets->m_ClearColour = sf::Color(112, 112, 176);
	m_Assets->m_DarkFontColour = sf::Color(16, 24, 40);
	m_Assets->m_LightFontColour = sf::Color(56, 48, 80);
	m_Assets->m_ErrorFontColour = sf::Color(196, 24, 24);
}

bool PrimaryUI::Render(Window& window, rogue::app::UiSnapshot const& snapshot, CommandSink const& submitCommand)
{
	unsigned int const animationFrame = static_cast<unsigned int>((UpdateTimer::GetCurrentClock() / 250000000) % 4);
	if (!m_Refresh.ShouldDraw(snapshot, window.HadVisualEvent(), animationFrame,
							static_cast<std::size_t>(m_CurrentConnectionIdx), m_EditingBridgePort))
	{
		return false;
	}
	m_Assets->m_Text.BeginFrame();
	m_Assets->m_LoadingSpinnerAnimText.assign(animationFrame, '.');
	m_Assets->m_CursorPosAnimText = animationFrame % 2 == 0 ? "" : "|";

	sf::RenderWindow& gfx = *window.GetHandle();

	// Keep the window's width-to-height ratio fixed.
	{
		sf::Vector2u currentWindowSize = gfx.getSize();

		// Use the width as the source dimension while the user resizes the window.
		sf::Vector2u snappedWindowSize(
			currentWindowSize.x, (u32)std::max(1.0, std::round(currentWindowSize.x / c_ViewAspectW) * c_ViewAspectH)
		);

		if (currentWindowSize != snappedWindowSize)
		{
			gfx.setSize(snappedWindowSize);
		}
	}

	// Match the logical view to the Poketch frame asset.
	sf::View view(
		sf::FloatRect(sf::Vector2f(-c_ViewWidth / 2, -c_ViewHeight / 2), sf::Vector2f(c_ViewWidth, c_ViewHeight)));
	gfx.setView(view);

	gfx.clear(m_Assets->m_ClearColour);

	// Draw the title or the latest error.
	std::string const& errorStr = snapshot.error;

	if (errorStr.empty())
	{
		m_Assets->DrawCenteredText(gfx, ROGUE_ASSISTANT_DISPLAY_NAME, c_CentreOffset + sf::Vector2f(0, -86), 20,
								   m_Assets->m_DarkFontColour);
	}
	else
	{
		m_Assets->DrawCenteredText(gfx, errorStr, c_CentreOffset + sf::Vector2f(0, -86), 16,
								   m_Assets->m_ErrorFontColour);
	}
	m_Assets->DrawRightAlignedText(gfx, "v" ROGUE_ASSISTANT_VERSION_STRING, c_CentreOffset + sf::Vector2f(101, -90), 10,
								   m_Assets->m_LightFontColour);

	// Draw the connection instructions.
	if (snapshot.connections.empty())
	{
		m_Assets->DrawLeftAlignedText(gfx, "Waiting for Emerald Rogue" + m_Assets->m_LoadingSpinnerAnimText,
									  c_CentreOffset + sf::Vector2f(-74, -55), 14, m_Assets->m_LightFontColour);

		m_Assets->DrawLeftAlignedText(gfx,
									  "Connect to mGBA 0.10.5 or later:\n"
									  "1. Open Emerald Rogue in mGBA\n"
									  "2. Select Tools > Scripting...\n"
									  "3. Select File > Load Script...\n"
									  "4. Open RogueAssistant_mGBA.lua",
									  c_CentreOffset + sf::Vector2f(-90, -38), 11, m_Assets->m_LightFontColour);
	}
	else
	{
		// Draw the active connection status.
		int const connectionCount = static_cast<int>(snapshot.connections.size());
		int prevConnIdx = m_CurrentConnectionIdx;

		if (window.ButtonJustReleased(sf::Keyboard::Key::Tab))
		{
			m_CurrentConnectionIdx++;
		}

		m_CurrentConnectionIdx %= connectionCount;
		rogue::app::ConnectionSnapshot const& connection = snapshot.connections[m_CurrentConnectionIdx];
		bool const hasSwappedConnection =
			prevConnIdx != m_CurrentConnectionIdx || m_CurrentConnectionId != connection.id;
		m_CurrentConnectionId = connection.id;

		std::string connectionText = "Connected to mGBA";

		if (connectionCount > 1)
		{
			connectionText = "Connected: " + std::to_string(m_CurrentConnectionIdx + 1) + " of " +
							 std::to_string(connectionCount) + " [Tab]";
		}

		m_Assets->DrawCenteredText(gfx, connectionText, c_CentreOffset + sf::Vector2f(0, 52), 14, sf::Color::Green);

		// Select the page for the active connection.

		rogue::app::UiPage const newPage = connection.page;
		bool initialLoad = false;

		if (m_CurrentPage != newPage || hasSwappedConnection)
		{
			initialLoad = true;
			window.ClearInputText();
		}
		m_CurrentPage = newPage;

		// Draw the selected page.
		switch (m_CurrentPage)
		{
		case rogue::app::UiPage::Multiplayer:
			RenderMultiplayerPage(window, snapshot, connection, initialLoad, submitCommand);
			break;

		case rogue::app::UiPage::HomeBox:
			RenderHomeBoxPage(window, connection.homeBox, initialLoad);
			break;

		default:
			RenderAwaitingPage(window);
			break;
		}
	}

	RenderBridgeControls(window, snapshot, submitCommand);

	// Draw the Poketch overlay last.
	sf::Sprite sprite(m_Assets->m_PoketchOverlay);
	sprite.setOrigin(sf::Vector2f(c_ViewWidth / 2, c_ViewHeight / 2));
	gfx.draw(sprite);

	// Restore the default view.
	gfx.setView(gfx.getDefaultView());

	return true;
}

void PrimaryUI::RenderBridgeControls(Window& window, rogue::app::UiSnapshot const& snapshot,
									 CommandSink const& submitCommand)
{
	if (!snapshot.connections.empty())
		return;

	sf::RenderWindow& gfx = *window.GetHandle();
	std::string bridgeState;
	switch (snapshot.transportState)
	{
	case TransportState::Stopped:
		bridgeState = "mGBA connection stopped";
		break;
	case TransportState::Disconnected:
		bridgeState = "mGBA is not connected";
		break;
	case TransportState::Listening:
		bridgeState = "Waiting for mGBA on port " + std::to_string(snapshot.bridgePort);
		break;
	case TransportState::Connected:
		bridgeState = "Connected to mGBA on port " + std::to_string(snapshot.bridgePort);
		break;
	}
	if (!m_EditingBridgePort && window.ButtonJustReleased(sf::Keyboard::Key::P))
	{
		m_EditingBridgePort = true;
		window.SetInputText(std::to_string(snapshot.bridgePort));
	}
	if (m_EditingBridgePort)
	{
		window.SetInputText(SanitiseConnectionAddress(window.GetInputText(), true));
		m_Assets->DrawCenteredText(gfx, bridgeState, c_CentreOffset + sf::Vector2f(0, 44), 9,
								   m_Assets->m_DarkFontColour);
		m_Assets->DrawCenteredText(gfx, "Port: " + window.GetInputText() + m_Assets->m_CursorPosAnimText,
								   c_CentreOffset + sf::Vector2f(0, 57), 9, m_Assets->m_DarkFontColour);
		m_Assets->DrawCenteredText(gfx, "[Enter] Save  [Esc] Cancel", c_CentreOffset + sf::Vector2f(0, 69), 8,
								   m_Assets->m_LightFontColour);
		if (window.ButtonJustReleased(sf::Keyboard::Key::Enter))
		{
			rogue::app::UiCommand command;
			command.type = rogue::app::UiCommand::Type::SetBridgePort;
			command.value = window.GetInputText();
			(void)submitCommand(std::move(command));
			m_EditingBridgePort = false;
			window.ClearInputText();
		}
		else if (window.ButtonJustReleased(sf::Keyboard::Key::Escape))
		{
			m_EditingBridgePort = false;
			window.ClearInputText();
		}
		return;
	}

	if (window.ButtonJustReleased(sf::Keyboard::Key::E))
	{
		rogue::app::UiCommand command;
		command.type = rogue::app::UiCommand::Type::ExportBridgeScript;
		if (submitCommand(std::move(command)))
			m_ActionMessage.clear();
		else
			m_ActionMessage = "The app is busy. Try again.";
	}
	if (window.ButtonJustReleased(sf::Keyboard::Key::C))
	{
		if (snapshot.bridgeScriptPath.empty())
		{
			m_ActionMessage = "Export the mGBA script first.";
		}
		else
		{
			auto const begin = snapshot.bridgeScriptPath.begin();
			sf::Clipboard::setString(sf::String::fromUtf8(begin, snapshot.bridgeScriptPath.end()));
			m_ActionMessage = "Script path copied.";
		}
	}
	if (window.ButtonJustReleased(sf::Keyboard::Key::R))
	{
		if (snapshot.bridgeScriptPath.empty())
		{
			m_ActionMessage = "Export the mGBA script first.";
		}
		else
		{
			std::string error;
			if (rogue::platform::RevealDirectory(rogue::platform::PathFromUtf8(snapshot.bridgeScriptPath).parent_path(),
												 error))
			{
				m_ActionMessage = "Script folder opened.";
			}
			else
			{
				m_ActionMessage = "Cannot open the script folder.";
				LOG_WARN("Cannot open bridge script folder: %s", error.c_str());
			}
		}
	}

	std::string const& action = m_ActionMessage.empty() ? snapshot.bridgeMessage : m_ActionMessage;
	m_Assets->DrawCenteredText(gfx, bridgeState, c_CentreOffset + sf::Vector2f(0, 37), 9,
								   m_Assets->m_DarkFontColour);
	if (!action.empty())
		m_Assets->DrawCenteredText(gfx, action, c_CentreOffset + sf::Vector2f(0, 49), 8,
								   m_Assets->m_DarkFontColour);
	m_Assets->DrawCenteredText(gfx, "[P] Change port  [E] Export script", c_CentreOffset + sf::Vector2f(0, 59), 8,
							   m_Assets->m_LightFontColour);
	m_Assets->DrawCenteredText(gfx, "[C] Copy path  [R] Open folder", c_CentreOffset + sf::Vector2f(0, 69), 8,
							   m_Assets->m_LightFontColour);
}

void PrimaryUI::RenderAwaitingPage(Window& window)
{
	sf::RenderWindow& gfx = *window.GetHandle();

	// Draw the idle state.
	m_Assets->DrawCenteredText(gfx, "Ready", c_CentreOffset + sf::Vector2f(0, -55), 16, m_Assets->m_LightFontColour);
}

void PrimaryUI::RenderMultiplayerPage(Window& window, rogue::app::UiSnapshot const& snapshot,
									  rogue::app::ConnectionSnapshot const& connection, bool initialLoad,
									  CommandSink const& submitCommand)
{
	sf::RenderWindow& gfx = *window.GetHandle();
	rogue::app::MultiplayerSnapshot const& multiplayer = connection.multiplayer;

	// Draw the page title.
	m_Assets->DrawCenteredText(gfx, "Multiplayer", c_CentreOffset + sf::Vector2f(0, -55), 16,
							   m_Assets->m_LightFontColour);

	if (initialLoad)
	{
		if (multiplayer.requestingHost)
		{
			window.SetInputText(snapshot.multiplayerHostPort);
		}
		else
		{
			window.SetInputText(snapshot.multiplayerJoinAddress);
		}
	}

	if (multiplayer.awaitingAddress)
	{
		window.SetInputText(SanitiseConnectionAddress(window.GetInputText(), multiplayer.requestingHost));

		if (multiplayer.requestingHost)
		{
			m_Assets->DrawLeftAlignedText(gfx,
										  "Enter the host port:\n" + window.GetInputText() +
											  m_Assets->m_CursorPosAnimText + "\n\nPress [Enter] to continue",
										  c_CentreOffset + sf::Vector2f(-90, -30), 16, m_Assets->m_LightFontColour);
		}
		else
		{
			m_Assets->DrawLeftAlignedText(gfx,
										  "Enter the host IP address:\n" + window.GetInputText() +
											  m_Assets->m_CursorPosAnimText + "\n\nPress [Enter] to continue",
										  c_CentreOffset + sf::Vector2f(-90, -30), 16, m_Assets->m_LightFontColour);
		}

		if (window.ButtonJustReleased(sf::Keyboard::Key::Enter))
		{
			rogue::app::UiCommand command;
			command.type = rogue::app::UiCommand::Type::ProvideMultiplayerAddress;
			command.connectionId = connection.id;
			command.value = window.GetInputText();
			if (submitCommand(std::move(command)))
				window.ClearInputText();
		}
	}
	else
	{
		if (multiplayer.requestingHost)
		{
			m_Assets->DrawLeftAlignedText(gfx, "Hosting on port " + std::to_string(multiplayer.port),
										  c_CentreOffset + sf::Vector2f(-90, -40), 16, m_Assets->m_LightFontColour);
		}
		else
		{
			if (multiplayer.connected)
			{
				m_Assets->DrawLeftAlignedText(gfx, "Connected to host", c_CentreOffset + sf::Vector2f(-90, -40), 16,
											  m_Assets->m_LightFontColour);
			}
		}

		if (!multiplayer.connected)
		{
			m_Assets->DrawLeftAlignedText(gfx, "Connecting" + m_Assets->m_LoadingSpinnerAnimText,
										  c_CentreOffset + sf::Vector2f(-90, -30), 16, m_Assets->m_LightFontColour);
		}
	}
}

void PrimaryUI::RenderHomeBoxPage(Window& window, rogue::app::HomeBoxSnapshot const& homeBox, bool /*initialLoad*/)
{
	sf::RenderWindow& gfx = *window.GetHandle();
	if (homeBox.requiresReopen)
	{
		m_Assets->DrawCenteredText(gfx, "Storage transfer stopped", c_CentreOffset + sf::Vector2f(0, -55), 14,
								   m_Assets->m_ErrorFontColour);
		m_Assets->DrawCenteredText(gfx, "Press B. Reopen Extended Storage.", c_CentreOffset + sf::Vector2f(0, -37), 11,
								   m_Assets->m_DarkFontColour);
		return;
	}

	// Draw the transfer state.
	m_Assets->DrawCenteredText(gfx, "Transferring Pokémon boxes", c_CentreOffset + sf::Vector2f(0, -55), 16,
							   m_Assets->m_LightFontColour);

	if (homeBox.loading)
	{
		m_Assets->DrawCenteredText(gfx, "Loading" + m_Assets->m_LoadingSpinnerAnimText,
								   c_CentreOffset + sf::Vector2f(0, -40), 16, m_Assets->m_LightFontColour);
	}
	else if (homeBox.saving)
	{
		m_Assets->DrawCenteredText(gfx, "Saving" + m_Assets->m_LoadingSpinnerAnimText,
								   c_CentreOffset + sf::Vector2f(0, -40), 16, m_Assets->m_DarkFontColour);
	}
}
