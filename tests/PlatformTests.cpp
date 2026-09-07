#include "Log.h"
#include "Platform/AppPaths.h"
#include "Platform/BridgeScript.h"
#include "Platform/Configuration.h"
#include "Platform/ResourceLocator.h"
#include "Platform/Utf8.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace fs = std::filesystem;
using namespace rogue::platform;

namespace
{
	class TemporaryDirectory
	{
	public:
		TemporaryDirectory()
		{
			auto const unique = std::chrono::steady_clock::now().time_since_epoch().count();
			m_Path = fs::temp_directory_path() / ("rogue-assistant-tests-" + std::to_string(unique));
			fs::create_directories(m_Path);
		}

		~TemporaryDirectory()
		{
			std::error_code ignored;
			fs::remove_all(m_Path, ignored);
		}

		TemporaryDirectory(TemporaryDirectory const&) = delete;
		TemporaryDirectory& operator=(TemporaryDirectory const&) = delete;

		[[nodiscard]] fs::path const& Path() const { return m_Path; }

	private:
		fs::path m_Path;
	};

	void WriteText(fs::path const& path, std::string const& contents)
	{
		fs::create_directories(path.parent_path());
		std::ofstream stream(path, std::ios::binary | std::ios::trunc);
		stream << contents;
		REQUIRE(stream.good());
	}

	std::string ReadText(fs::path const& path)
	{
		std::ifstream stream(path, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}
}

TEST_CASE("application paths follow each platform convention", "[platform][paths]")
{
	PathEnvironment windows;
	windows.platform = HostPlatform::Windows;
	windows.roamingAppData = "C:/Users/Misty/AppData/Roaming";
	windows.currentWorkingDirectory = "C:/Games/RogueAssistant";
	windows.executablePath = "C:/Program Files/Emerald Rogue Assistant/RogueAssistant.exe";
	auto const windowsPaths = BuildAppPaths(windows);
	REQUIRE(windowsPaths.dataDirectory == fs::path("C:/Users/Misty/AppData/Roaming/Emerald Rogue Assistant"));
	REQUIRE(windowsPaths.configDirectory == windowsPaths.dataDirectory);
	REQUIRE(windowsPaths.settingsFile == windowsPaths.dataDirectory / "settings.ini");
	REQUIRE(windowsPaths.resourceDirectory == fs::path("C:/Program Files/Emerald Rogue Assistant/resources"));
	REQUIRE(windowsPaths.legacyDataDirectory
		== fs::path("C:/Users/Misty/AppData/Roaming/.pokabbie/rogue_assistant"));

	PathEnvironment mac;
	mac.platform = HostPlatform::MacOS;
	mac.homeDirectory = "/Users/misty";
	mac.executablePath = "/Applications/RogueAssistant.app/Contents/MacOS/RogueAssistant";
	auto const macPaths = BuildAppPaths(mac);
	REQUIRE(macPaths.dataDirectory
		== fs::path("/Users/misty/Library/Application Support/assistant.emerald.rogue"));
	REQUIRE(macPaths.configDirectory == macPaths.dataDirectory);
	REQUIRE(macPaths.resourceDirectory == fs::path("/Applications/RogueAssistant.app/Contents/Resources"));

	PathEnvironment linuxEnvironment;
	linuxEnvironment.platform = HostPlatform::Linux;
	linuxEnvironment.homeDirectory = "/home/misty";
	linuxEnvironment.xdgDataHome = "/srv/misty/data";
	linuxEnvironment.xdgConfigHome = "/srv/misty/config";
	linuxEnvironment.executablePath = "/opt/rogue-assistant/RogueAssistant";
	auto const linuxPaths = BuildAppPaths(linuxEnvironment);
	REQUIRE(linuxPaths.dataDirectory == fs::path("/srv/misty/data/emerald-rogue-assistant"));
	REQUIRE(linuxPaths.configDirectory == fs::path("/srv/misty/config/emerald-rogue-assistant"));

	linuxEnvironment.xdgDataHome = "relative/data";
	linuxEnvironment.xdgConfigHome.clear();
	auto const fallbackPaths = BuildAppPaths(linuxEnvironment);
	REQUIRE(fallbackPaths.dataDirectory == fs::path("/home/misty/.local/share/emerald-rogue-assistant"));
	REQUIRE(fallbackPaths.configDirectory == fs::path("/home/misty/.config/emerald-rogue-assistant"));
}

TEST_CASE("native path discovery resolves the running test executable", "[platform][paths]")
{
	std::string error;
	auto const paths = DiscoverAppPaths(error);
	REQUIRE(paths.has_value());
	REQUIRE(error.empty());
	REQUIRE(paths->settingsFile.filename() == "settings.ini");
	REQUIRE(paths->scriptDirectory.filename() == "scripts");
	REQUIRE_FALSE(paths->resourceDirectory.empty());
}

TEST_CASE("settings parse strictly and save atomically", "[platform][settings]")
{
	TemporaryDirectory temporary;
	fs::path const settingsPath = temporary.Path() / "config" / "settings.ini";

	Settings settings;
	settings.multiplayerHostPort = 31000;
	settings.multiplayerJoinIp = "example.test";
	settings.bridgePort = 30126;
	std::string error;
	REQUIRE(SaveSettings(settingsPath, settings, error));
	REQUIRE(error.empty());

	auto const loaded = LoadSettings(settingsPath);
	REQUIRE(loaded.Succeeded());
	REQUIRE(loaded.fileFound);
	REQUIRE_FALSE(loaded.needsRewrite);
	REQUIRE(loaded.settings.multiplayerHostPort == 31000);
	REQUIRE(loaded.settings.multiplayerJoinIp == "example.test");
	REQUIRE(loaded.settings.bridgePort == 30126);
	REQUIRE(ReadText(settingsPath)
		== "Multiplayer.HostPort=31000\nMultiplayer.JoinIP=example.test\nBridge.Port=30126\n");
	settings.bridgePort = 30127;
	REQUIRE(SaveSettings(settingsPath, settings, error));
	REQUIRE(LoadSettings(settingsPath).settings.bridgePort == 30127);

	for (auto const& entry : fs::directory_iterator(settingsPath.parent_path()))
		REQUIRE(entry.path().filename().string().find(".tmp.") == std::string::npos);

	WriteText(settingsPath, "Multiplayer.HostPort=30025\nMultiplayer.HostPort=30026\nBridge.Port=0\nUnknown=x\n");
	auto const invalid = LoadSettings(settingsPath);
	REQUIRE_FALSE(invalid.Succeeded());
	REQUIRE(invalid.diagnostics.size() == 3);
	REQUIRE(invalid.settings.multiplayerHostPort == 30025);
	REQUIRE(invalid.settings.bridgePort == 30125);

	WriteText(settingsPath, "Multiplayer.HostPort=30025\nMultiplayer.JoinIP=127.0.0.1\n");
	auto const legacy = LoadSettings(settingsPath);
	REQUIRE(legacy.Succeeded());
	REQUIRE(legacy.needsRewrite);
	REQUIRE(legacy.settings.bridgePort == 30125);

	Settings invalidForSave;
	invalidForSave.bridgePort = 0;
	REQUIRE_FALSE(SaveSettings(settingsPath, invalidForSave, error));
}

TEST_CASE("legacy Windows data imports only into an absent destination", "[platform][migration]")
{
	TemporaryDirectory temporary;
	PathEnvironment environment;
	environment.platform = HostPlatform::Windows;
	environment.roamingAppData = temporary.Path() / "AppData";
	environment.currentWorkingDirectory = temporary.Path() / "working";
	environment.executablePath = temporary.Path() / "RogueAssistant.exe";
	auto const paths = BuildAppPaths(environment);

	fs::path const legacyBox = *paths.legacyDataDirectory / "1" / "42" / "boxes.dat";
	WriteText(legacyBox, "legacy-box-data");
	WriteText(*paths.legacySettingsFile, "Multiplayer.HostPort=30025\nMultiplayer.JoinIP=127.0.0.1\n");

	auto const imported = ImportLegacyWindowsData(paths);
	REQUIRE(imported.Succeeded());
	REQUIRE(imported.attempted);
	REQUIRE(imported.importedData);
	REQUIRE(imported.importedSettings);
	REQUIRE(ReadText(paths.dataDirectory / "1" / "42" / "boxes.dat") == "legacy-box-data");
	REQUIRE(ReadText(paths.settingsFile) == ReadText(*paths.legacySettingsFile));

	WriteText(legacyBox, "changed-legacy-data");
	auto const secondAttempt = ImportLegacyWindowsData(paths);
	REQUIRE_FALSE(secondAttempt.attempted);
	REQUIRE(ReadText(paths.dataDirectory / "1" / "42" / "boxes.dat") == "legacy-box-data");
}

TEST_CASE("UTF-8 validation rejects malformed and non-canonical encodings", "[platform][utf8]")
{
	REQUIRE(IsValidUtf8("Pikachu"));
	REQUIRE(IsValidUtf8("Pok\xC3\xA9mon"));
	REQUIRE(IsValidUtf8("\xF0\x9F\x8E\xAE"));
	REQUIRE_FALSE(IsValidUtf8("\xC0\xAF"));
	REQUIRE_FALSE(IsValidUtf8("\xED\xA0\x80"));
	REQUIRE_FALSE(IsValidUtf8("\xF4\x90\x80\x80"));
	REQUIRE_FALSE(IsValidUtf8("\xE2\x82"));

	std::string const pathText = "Pok\xC3\xA9mon/boxes.dat";
	REQUIRE(PathToUtf8(PathFromUtf8(pathText)) == pathText);
}

TEST_CASE("resource lookup maps logical assets into the installed resource directory", "[platform][resources]")
{
	ResourceLocator const resources("/opt/rogue-assistant/resources");
	REQUIRE(resources.Resolve(Resource::Font)
		== fs::path("/opt/rogue-assistant/resources/pokemon-emerald-pro.ttf"));
	REQUIRE(resources.Resolve(Resource::Frame)
		== fs::path("/opt/rogue-assistant/resources/poketch_frame.png"));
	REQUIRE(resources.Resolve(Resource::Icon)
		== fs::path("/opt/rogue-assistant/resources/WobbuffetImage.png"));
	REQUIRE(resources.Resolve(Resource::BridgeScript)
		== fs::path("/opt/rogue-assistant/resources/RogueAssistant_mGBA.lua"));
}

TEST_CASE("bridge script export substitutes only the configured port and writes atomically", "[platform][resources]")
{
	TemporaryDirectory temporary;
	fs::path const source = temporary.Path() / "resources" / "RogueAssistant_mGBA.lua";
	WriteText(source, "local BRIDGE_PORT = 30125 -- ROGUE_ASSISTANT_BRIDGE_PORT\n"
					  "local payload = '30125 stays unchanged'\n");

	auto exported = ExportBridgeScript(source, temporary.Path() / "data" / "scripts", 41234);
	REQUIRE(exported.Succeeded());
	REQUIRE(exported.path == temporary.Path() / "data" / "scripts" / "RogueAssistant_mGBA.lua");
	REQUIRE(ReadText(exported.path) == "local BRIDGE_PORT = 41234 -- ROGUE_ASSISTANT_BRIDGE_PORT\n"
									   "local payload = '30125 stays unchanged'\n");

	exported = ExportBridgeScript(source, temporary.Path() / "data" / "scripts", 0);
	REQUIRE_FALSE(exported.Succeeded());
	REQUIRE(ReadText(temporary.Path() / "data" / "scripts" / "RogueAssistant_mGBA.lua").find("41234") !=
			std::string::npos);
}

TEST_CASE("UTF-8 paths preserve bytes and respect string view bounds", "[platform][paths][utf8]")
{
	REQUIRE(PathFromUtf8({}).empty());
	std::string const text = "Pok\xC3\xA9mon/\xE6\x97\xA5\xE6\x9C\xAC/\xF0\x9F\x8E\xAE.dat";
	REQUIRE(PathToUtf8(PathFromUtf8(text)) == text);

	std::string const padded = "prefix" + text + "suffix";
	std::string_view const slice(padded.data() + 6, text.size());
	REQUIRE(PathToUtf8(PathFromUtf8(slice)) == text);
}

TEST_CASE("portable logging writes UTF-8 diagnostics to the configured data path", "[platform][logging]")
{
	TemporaryDirectory temporary;
	fs::path const logPath = temporary.Path() / "logs" / "RogueAssistant.log";
	RogueLog_Initialize(logPath);
	RogueLog_Write("INFO", "Connected to %s", "Pok\xC3\xA9mon Emerald Rogue");
	RogueLog_Shutdown();

	std::string const contents = ReadText(logPath);
	REQUIRE(contents.find("[INFO]") != std::string::npos);
	REQUIRE(contents.find("Connected to Pok\xC3\xA9mon Emerald Rogue") != std::string::npos);
}
