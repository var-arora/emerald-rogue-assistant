#include "UI/Window.h"
#include "Defines.h"
#include "Log.h"
#include "Platform/ResourceLocator.h"

#include <SFML/Graphics.hpp>
#include <SFML/Window.hpp>
#include <SFML/Window/Clipboard.hpp>

#include <algorithm>
#include <cstddef>

namespace
{
bool LoadWindowIcon(sf::Image& icon, std::filesystem::path const& resourceDirectory)
{
	if (!resourceDirectory.empty())
	{
		rogue::platform::ResourceLocator const resources(resourceDirectory);
		return icon.loadFromFile(resources.Resolve(rogue::platform::Resource::Icon));
	}
	return false;
}
} // namespace

Window::Window(WindowConfig const& config) : m_Config(config)
{
}

Window::~Window()
{
	Destroy();
}

bool Window::Create()
{
	LOG_INFO("Creating window");
	unsigned int style = sf::Style::Titlebar | sf::Style::Close;
	if (m_Config.resizable)
		style |= sf::Style::Resize;

	m_WindowHandle = std::make_unique<sf::RenderWindow>();
	m_WindowHandle->create(
		sf::VideoMode({static_cast<unsigned int>(m_Config.width), static_cast<unsigned int>(m_Config.height)}),
		m_Config.title, style);
	m_WindowHandle->setVerticalSyncEnabled(true);

	sf::Image icon;
	if (LoadWindowIcon(icon, m_Config.resourceDirectory))
		m_WindowHandle->setIcon(icon);
	else
		LOG_WARN("Cannot load the Emerald Rogue Assistant window icon");
	return m_WindowHandle->isOpen();
}

bool Window::Destroy()
{
	if (m_WindowHandle)
	{
		m_WindowHandle->close();
		m_WindowHandle.reset();
	}
	return true;
}

void Window::EnterMainLoop(WindowCallback const& callback, void* userData)
{
	if (!m_WindowHandle)
	{
		ASSERT_FAIL("Window not created yet");
		return;
	}

	bool continueLoop = true;
	bool firstFrame = true;
	while (m_WindowHandle->isOpen() && continueLoop)
	{
		m_HadVisualEvent = firstFrame;
		firstFrame = false;
		m_PreviousKeyStates = m_CurrentKeyStates;

		while (auto const event = m_WindowHandle->pollEvent())
		{
			if (event->is<sf::Event::Resized>() || event->is<sf::Event::FocusGained>() ||
				event->is<sf::Event::KeyPressed>() || event->is<sf::Event::KeyReleased>() ||
				event->is<sf::Event::TextEntered>())
			{
				m_HadVisualEvent = true;
			}
			if (auto const* keyPressed = event->getIf<sf::Event::KeyPressed>();
				keyPressed != nullptr && keyPressed->code != sf::Keyboard::Key::Unknown)
			{
				m_CurrentKeyStates.set(static_cast<std::size_t>(keyPressed->code), true);
			}
			if (auto const* keyReleased = event->getIf<sf::Event::KeyReleased>();
				keyReleased != nullptr && keyReleased->code != sf::Keyboard::Key::Unknown)
			{
				m_CurrentKeyStates.set(static_cast<std::size_t>(keyReleased->code), false);
			}

			if (auto const* textEntered = event->getIf<sf::Event::TextEntered>();
				textEntered != nullptr && textEntered->unicode < 128)
			{
				if (textEntered->unicode == 8)
				{
					if (!m_TextEntered.empty())
						m_TextEntered.pop_back();
				}
				else if (textEntered->unicode == 22)
				{
					std::string const clipboard = sf::Clipboard::getString().toAnsiString();
					std::size_t const available = 256U - std::min<std::size_t>(m_TextEntered.size(), 256U);
					m_TextEntered.append(clipboard, 0, std::min(clipboard.size(), available));
				}
				else if (textEntered->unicode == 1)
				{
					m_TextEntered.clear();
				}
				else if (textEntered->unicode >= 0x20)
				{
					m_TextEntered += static_cast<char>(textEntered->unicode);
				}
			}

			if (event->is<sf::Event::Closed>() && m_Config.canBeDestroyed)
				continueLoop = false;
		}

		if (!continueLoop)
			break;
		switch (callback(this, userData))
		{
		case WindowFrame::Rendered:
			m_WindowHandle->display();
			break;
		case WindowFrame::Idle:
			// Keep input responsive without submitting unchanged frames to OpenGL.
			sf::sleep(sf::milliseconds(8));
			break;
		case WindowFrame::Close:
			continueLoop = false;
			break;
		}
	}

	// The caller stops the session worker before this Window leaves scope.
}
