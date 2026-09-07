#include "UserData.h"

#include "Log.h"
#include "Platform/AppPaths.h"
#include "Platform/Configuration.h"
#include "Platform/FileSystem.h"
#include "Platform/Utf8.h"

#include <optional>
#include <utility>

namespace fs = std::filesystem;

namespace
{
	std::optional<rogue::platform::AppPaths> Paths;
	rogue::platform::Settings SavedSettings;
	bool PendingSettingsChange = false;
	bool CanWriteSettings = true;

	fs::path ResolveDataPath(std::wstring const& path)
	{
		fs::path resolved(path);
		if (resolved.is_absolute())
			return resolved.lexically_normal();
		if (!Paths)
			return resolved.lexically_normal();
		return (Paths->dataDirectory / resolved).lexically_normal();
	}

	bool EnsureParentDirectory(fs::path const& path)
	{
		std::string error;
		if (rogue::platform::EnsureDirectory(path.parent_path(), error))
			return true;
		LOG_ERROR("Cannot prepare %s: %s", rogue::platform::PathToUtf8(path).c_str(), error.c_str());
		return false;
	}
}

bool UserData::DoesDirectoryExist(std::wstring const& path)
{
	std::error_code ec;
	return fs::is_directory(ResolveDataPath(path), ec);
}

bool UserData::DoesFileExist(std::wstring const& path)
{
	std::error_code ec;
	return fs::is_regular_file(ResolveDataPath(path), ec);
}

bool UserData::TryOpenReadFile(std::wstring const& path, std::fstream& outStream)
{
	fs::path const resolved = ResolveDataPath(path);
	std::error_code ec;
	if (!fs::is_regular_file(resolved, ec))
		return false;

	LOG_INFO("UserData::OpenRead %s", rogue::platform::PathToUtf8(resolved).c_str());
	outStream.close();
	outStream.open(resolved, std::ios::binary | std::ios::in);
	return outStream.is_open();
}

bool UserData::TryOpenWriteFile(std::wstring const& path, std::fstream& outStream, bool createIfMissing)
{
	fs::path const resolved = ResolveDataPath(path);
	std::error_code ec;
	if (!createIfMissing && !fs::is_regular_file(resolved, ec))
		return false;
	if (!EnsureParentDirectory(resolved))
		return false;

	LOG_INFO("UserData::OpenWrite %s", rogue::platform::PathToUtf8(resolved).c_str());
	outStream.close();
	outStream.open(resolved, std::ios::binary | std::ios::out | std::ios::trunc);
	return outStream.is_open();
}

bool UserData::TryOpenAppendFile(std::wstring const& path, std::fstream& outStream, bool createIfMissing)
{
	fs::path const resolved = ResolveDataPath(path);
	std::error_code ec;
	if (!createIfMissing && !fs::is_regular_file(resolved, ec))
		return false;
	if (!EnsureParentDirectory(resolved))
		return false;

	LOG_INFO("UserData::OpenAppend %s", rogue::platform::PathToUtf8(resolved).c_str());
	outStream.close();
	outStream.open(resolved, std::ios::binary | std::ios::out | std::ios::app);
	return outStream.is_open();
}

bool UserData::Init()
{
	std::string error;
	Paths = rogue::platform::DiscoverAppPaths(error);
	if (!Paths)
	{
		LOG_ERROR("Cannot initialize application paths: %s", error.c_str());
		return false;
	}

	auto const migration = rogue::platform::ImportLegacyWindowsData(*Paths);
	RogueLog_Initialize(Paths->logFile);
	for (std::string const& diagnostic : migration.diagnostics)
		LOG_ERROR("Legacy data import: %s", diagnostic.c_str());
	if (migration.importedData)
		LOG_INFO("Imported data from the original Rogue Assistant");
	if (migration.importedSettings)
		LOG_INFO("Imported legacy settings.ini");

	if (!rogue::platform::EnsureDirectory(Paths->dataDirectory, error)
		|| !rogue::platform::EnsureDirectory(Paths->configDirectory, error)
		|| !rogue::platform::EnsureDirectory(Paths->scriptDirectory, error))
	{
		LOG_ERROR("Cannot initialize application directories: %s", error.c_str());
		return false;
	}

	auto const loaded = rogue::platform::LoadSettings(Paths->settingsFile);
	CanWriteSettings = loaded.Succeeded();
	if (loaded.Succeeded())
	{
		SavedSettings = loaded.settings;
		PendingSettingsChange = !loaded.fileFound || loaded.needsRewrite;
	}
	else
	{
		SavedSettings = {};
		PendingSettingsChange = false;
		for (std::string const& diagnostic : loaded.diagnostics)
			LOG_ERROR("Settings: %s", diagnostic.c_str());
		LOG_ERROR("The invalid settings file was left unchanged; defaults are active for this run");
	}

	return true;
}

void UserData::Update()
{
	if (!Paths || !PendingSettingsChange || !CanWriteSettings)
		return;

	std::string error;
	if (!rogue::platform::SaveSettings(Paths->settingsFile, SavedSettings, error))
	{
		LOG_ERROR("Cannot save settings: %s", error.c_str());
		return;
	}
	PendingSettingsChange = false;
}

void UserData::Shutdown()
{
	Update();
	RogueLog_Shutdown();
}

std::filesystem::path UserData::GetDataDirectory()
{
	return Paths ? Paths->dataDirectory : fs::path{};
}

std::filesystem::path UserData::GetConfigDirectory()
{
	return Paths ? Paths->configDirectory : fs::path{};
}

std::filesystem::path UserData::GetResourceDirectory()
{
	return Paths ? Paths->resourceDirectory : fs::path{};
}

std::filesystem::path UserData::GetScriptDirectory()
{
	return Paths ? Paths->scriptDirectory : fs::path{};
}

std::string UserData::GetSavedString(std::string const& key, std::string const& defaultValue)
{
	std::string const value = rogue::platform::GetSetting(SavedSettings, key);
	return value.empty() && key != rogue::platform::MultiplayerJoinIpKey ? defaultValue : value;
}

void UserData::SetSavedString(std::string const& key, std::string const& value)
{
	auto candidate = SavedSettings;
	std::string error;
	if (!rogue::platform::TrySetSetting(candidate, key, value, error))
	{
		LOG_WARN("Ignoring invalid setting %s: %s", key.c_str(), error.c_str());
		return;
	}
	SavedSettings = std::move(candidate);
	PendingSettingsChange = true;
}

int UserData::GetSavedInt(std::string const& key, int defaultValue)
{
	std::string const value = GetSavedString(key, std::to_string(defaultValue));
	try
	{
		std::size_t parsed = 0;
		int const result = std::stoi(value, &parsed, 10);
		return parsed == value.size() ? result : defaultValue;
	}
	catch (std::exception const&)
	{
		return defaultValue;
	}
}

void UserData::SetSavedInt(std::string const& key, int value)
{
	SetSavedString(key, std::to_string(value));
}
