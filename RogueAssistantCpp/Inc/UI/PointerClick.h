#pragma once

#include <SFML/Graphics/Rect.hpp>
#include <optional>

namespace rogue::ui
{
class PointerClick
{
public:
	void BeginFrame() { m_Released.reset(); }
	void Press(sf::Vector2i position) { m_Pressed = position; }
	void Release(sf::Vector2i position)
	{
		if (m_Pressed)
			m_Released = Click{*m_Pressed, position};
		m_Pressed.reset();
	}
	void Cancel()
	{
		m_Pressed.reset();
		m_Released.reset();
	}

	template <typename MapPosition>
	bool Within(sf::FloatRect const& bounds, MapPosition mapPosition) const
	{
		return m_Released && bounds.contains(mapPosition(m_Released->press)) &&
			bounds.contains(mapPosition(m_Released->release));
	}

private:
	struct Click { sf::Vector2i press; sf::Vector2i release; };
	std::optional<sf::Vector2i> m_Pressed;
	std::optional<Click> m_Released;
};
} // namespace rogue::ui
