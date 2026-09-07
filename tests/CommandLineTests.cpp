#include "Application/CommandLine.h"
#include "RogueAssistantVersion.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

using namespace rogue::app;

TEST_CASE("desktop command line accepts both bridge port forms", "[command-line]")
{
	std::array<std::string_view, 1> const inlineForm{"--bridge-port=41234"};
	auto parsed = ParseDesktopOptions(inlineForm);
	REQUIRE(parsed.error.empty());
	REQUIRE(parsed.bridgePort == 41234);

	std::array<std::string_view, 2> const splitForm{"--bridge-port", "51234"};
	parsed = ParseDesktopOptions(splitForm);
	REQUIRE(parsed.error.empty());
	REQUIRE(parsed.bridgePort == 51234);
}

TEST_CASE("desktop command line rejects missing, invalid, duplicate, and unknown options", "[command-line]")
{
	std::array<std::string_view, 1> const missing{"--bridge-port"};
	REQUIRE_FALSE(ParseDesktopOptions(missing).error.empty());

	std::array<std::string_view, 1> const invalid{"--bridge-port=0"};
	REQUIRE_FALSE(ParseDesktopOptions(invalid).error.empty());

	std::array<std::string_view, 2> const duplicate{"--bridge-port=30125", "--bridge-port=30126"};
	REQUIRE_FALSE(ParseDesktopOptions(duplicate).error.empty());

	std::array<std::string_view, 1> const unknown{"--listen-publicly"};
	REQUIRE_FALSE(ParseDesktopOptions(unknown).error.empty());
}

TEST_CASE("current-run bridge port override takes precedence without replacing configuration",
		  "[command-line][configuration]")
{
	REQUIRE(SelectBridgePort(std::nullopt, std::uint16_t{30125}) == std::uint16_t{30125});
	REQUIRE(SelectBridgePort(std::uint16_t{40125}, std::uint16_t{30125}) == std::uint16_t{40125});
}

TEST_CASE("desktop help and version switches are recognized", "[command-line]")
{
	std::array<std::string_view, 2> const arguments{"--version", "-h"};
	auto const parsed = ParseDesktopOptions(arguments);
	REQUIRE(parsed.error.empty());
	REQUIRE(parsed.showVersion);
	REQUIRE(parsed.showHelp);
	REQUIRE(DesktopUsage().starts_with(ROGUE_ASSISTANT_DISPLAY_NAME " " ROGUE_ASSISTANT_VERSION_STRING "\n"));
}
