#include "UI/RefreshState.h"
#include "UI/TextCache.h"
#include "UI/TextEncoding.h"

#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Text.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

TEST_CASE("UI text decodes UTF-8 before rendering", "[ui][utf8]")
{
	sf::String const decoded = rogue::ui::DecodeUtf8("Pok\xC3\xA9mon");
	REQUIRE(decoded.getSize() == 7);
	REQUIRE(decoded[3] == U'\u00E9');

	std::filesystem::path const fontPath = std::filesystem::path(ROGUE_TEST_RESOURCE_DIR) / "pokemon-emerald-pro.ttf";
	sf::Font font;
	REQUIRE(font.openFromFile(fontPath));
	REQUIRE(font.hasGlyph(decoded[3]));

	sf::Text const text(font, decoded, 16);
	REQUIRE(text.getString() == decoded);
}

TEST_CASE("UI text objects are reused across frames and changing messages", "[ui][text-cache]")
{
	sf::Font font;
	REQUIRE(font.openFromFile(std::filesystem::path(ROGUE_TEST_RESOURCE_DIR) / "pokemon-emerald-pro.ttf"));
	rogue::ui::TextCache cache(font);
	sf::Text* const title = &cache.Get("Emerald Rogue Assistant", 20);
	sf::Text* const status = &cache.Get("Waiting", 14);
	REQUIRE(title != status);

	for (unsigned int frame = 0; frame < 1000; ++frame)
	{
		cache.BeginFrame();
		REQUIRE(&cache.Get("Emerald Rogue Assistant", 20) == title);
		sf::String const message = rogue::ui::DecodeUtf8("Pok\xC3\xA9mon " + std::to_string(frame));
		unsigned int const size = frame % 2 == 0 ? 14 : 28;
		REQUIRE(&cache.Get(message, size) == status);
		REQUIRE(status->getString() == message);
		REQUIRE(status->getCharacterSize() == size);
		REQUIRE(&status->getFont() == &font);
	}
}

TEST_CASE("unchanged UI snapshots do not trigger redraws", "[ui][refresh]")
{
	rogue::ui::RefreshState refresh;
	rogue::app::UiSnapshot snapshot;
	snapshot.connections.emplace_back();
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	for (unsigned int tick = 1; tick < 1000; ++tick)
	{
		snapshot.revision = tick;
		REQUIRE_FALSE(refresh.ShouldDraw(snapshot, false, tick % 4, 0, false));
	}
	REQUIRE(refresh.ShouldDraw(snapshot, true, 0, 0, false));
	REQUIRE_FALSE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	snapshot.error = "Connection lost";
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	snapshot.connections.emplace_back();
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 1, false));
	snapshot.connections.clear();
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, true));
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
}

TEST_CASE("UI redraws only visible animation changes", "[ui][refresh]")
{
	rogue::ui::RefreshState refresh;
	rogue::app::UiSnapshot snapshot;
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	REQUIRE_FALSE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	REQUIRE(refresh.ShouldDraw(snapshot, false, 1, 0, false));
	REQUIRE_FALSE(refresh.ShouldDraw(snapshot, false, 1, 0, false));

	snapshot.connections.emplace_back();
	auto& connection = snapshot.connections.back();
	connection.page = rogue::app::UiPage::HomeBox;
	connection.homeBox.loading = true;
	REQUIRE(refresh.ShouldDraw(snapshot, false, 1, 0, false));
	REQUIRE(refresh.ShouldDraw(snapshot, false, 2, 0, false));
	connection.homeBox.loading = false;
	REQUIRE(refresh.ShouldDraw(snapshot, false, 2, 0, false));
	REQUIRE_FALSE(refresh.ShouldDraw(snapshot, false, 3, 0, false));
	connection.homeBox.saving = true;
	REQUIRE(refresh.ShouldDraw(snapshot, false, 3, 0, false));
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	connection.homeBox.requiresReopen = true;
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
	REQUIRE_FALSE(refresh.ShouldDraw(snapshot, false, 1, 0, false));

	connection.page = rogue::app::UiPage::Multiplayer;
	connection.multiplayer.connected = true;
	REQUIRE(refresh.ShouldDraw(snapshot, false, 1, 0, false));
	REQUIRE_FALSE(refresh.ShouldDraw(snapshot, false, 2, 0, false));
	connection.multiplayer.awaitingAddress = true;
	REQUIRE(refresh.ShouldDraw(snapshot, false, 2, 0, false));
	REQUIRE(refresh.ShouldDraw(snapshot, false, 3, 0, false));
	connection.multiplayer.awaitingAddress = false;
	connection.multiplayer.connected = false;
	REQUIRE(refresh.ShouldDraw(snapshot, false, 3, 0, false));
	REQUIRE(refresh.ShouldDraw(snapshot, false, 0, 0, false));
}
