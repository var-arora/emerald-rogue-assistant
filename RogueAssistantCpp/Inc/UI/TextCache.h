#pragma once

#include <SFML/Graphics/Text.hpp>

#include <cstddef>
#include <deque>

namespace rogue::ui
{
// Reuse one text object per draw position, not one per message value.
class TextCache
{
  public:
	explicit TextCache(sf::Font const& font) : m_Font(font)
	{
	}

	void BeginFrame()
	{
		m_NextText = 0;
	}

	sf::Text& Get(sf::String const& message, unsigned int characterSize)
	{
		if (m_NextText == m_Text.size())
			m_Text.emplace_back(m_Font);
		sf::Text& text = m_Text[m_NextText++];
		text.setString(message);
		text.setCharacterSize(characterSize);
		return text;
	}

  private:
	sf::Font const& m_Font;
	std::deque<sf::Text> m_Text;
	std::size_t m_NextText = 0;
};
} // namespace rogue::ui
