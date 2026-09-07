#pragma once

#include <filesystem>
#include <fstream>
#include <string>

class UserData
{
public:
	static bool DoesDirectoryExist(std::wstring const& path);
	static bool DoesFileExist(std::wstring const& path);

	static bool TryOpenReadFile(std::wstring const& path, std::fstream& outStream);
	static bool TryOpenWriteFile(std::wstring const& path, std::fstream& outStream, bool createIfMissing = true);
	static bool TryOpenAppendFile(std::wstring const& path, std::fstream& outStream, bool createIfMissing = true);

	static bool Init();
	static void Update();
	static void Shutdown();

	static std::filesystem::path GetDataDirectory();
	static std::filesystem::path GetConfigDirectory();
	static std::filesystem::path GetResourceDirectory();
	static std::filesystem::path GetScriptDirectory();

	static std::string GetSavedString(std::string const& key, std::string const& defaultValue = "");
	static void SetSavedString(std::string const& key, std::string const& value);

	static int GetSavedInt(std::string const& key, int defaultValue = 0);
	static void SetSavedInt(std::string const& key, int value);

};
