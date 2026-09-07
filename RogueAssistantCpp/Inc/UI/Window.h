#pragma once
#include <SFML/Window.hpp>
#include <bitset>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

class Window;

namespace sf
{
class RenderWindow;
}

enum class WindowFrame
{
	Rendered,
	Idle,
	Close,
};

using WindowCallback = std::function<WindowFrame(Window*, void*)>;

struct WindowConfig
{
	std::string title;
	int width = 640;
	int height = 480;
	bool resizable = true;
	bool canBeDestroyed = true;
	bool imGuiEnabled = false;
	std::filesystem::path resourceDirectory;
};

class Window
{
  public:
	Window(WindowConfig const&);
	~Window();

	Window(Window const&) = delete;
	Window& operator=(Window const&) = delete;

	bool Create();
	bool Destroy();

	void EnterMainLoop(WindowCallback const& callback, void* userData = nullptr);
	bool HadVisualEvent() const
	{
		return m_HadVisualEvent;
	}

	inline sf::RenderWindow* GetHandle()
	{
		return m_WindowHandle.get();
	}
	inline sf::RenderWindow const* GetHandle() const
	{
		return m_WindowHandle.get();
	}

	inline bool IsButtonHeld(sf::Keyboard::Key key) const
	{
		return m_CurrentKeyStates.test(static_cast<std::size_t>(key));
	}
	inline bool ButtonJustPressed(sf::Keyboard::Key key) const
	{
		auto const index = static_cast<std::size_t>(key);
		return m_CurrentKeyStates.test(index) && !m_PreviousKeyStates.test(index);
	}
	inline bool ButtonJustReleased(sf::Keyboard::Key key) const
	{
		auto const index = static_cast<std::size_t>(key);
		return !m_CurrentKeyStates.test(index) && m_PreviousKeyStates.test(index);
	}

	inline void ClearInputText()
	{
		m_TextEntered = "";
	}
	inline void SetInputText(std::string const& text)
	{
		m_TextEntered = text;
	}
	inline std::string const& GetInputText() const
	{
		return m_TextEntered;
	}

  private:
	WindowConfig m_Config;
	std::unique_ptr<sf::RenderWindow> m_WindowHandle;

	std::string m_TextEntered;
	std::bitset<sf::Keyboard::KeyCount> m_CurrentKeyStates;
	std::bitset<sf::Keyboard::KeyCount> m_PreviousKeyStates;
	bool m_HadVisualEvent = true;
};
