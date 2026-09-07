#include "Platform/AppPaths.h"
#include "Platform/Utf8.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace fs = std::filesystem;

namespace rogue::platform
{
	namespace
	{
		fs::path SelectPosixAbsoluteOrFallback(fs::path const& preferred, fs::path const& fallback)
		{
			auto const generic = preferred.generic_string();
			return !generic.empty() && generic.front() == '/' ? preferred : fallback;
		}

		fs::path FindResourceDirectory(HostPlatform platform, fs::path const& executablePath)
		{
			fs::path const executableDirectory = executablePath.parent_path();
			if (platform == HostPlatform::MacOS && executableDirectory.filename() == "MacOS"
				&& executableDirectory.parent_path().filename() == "Contents")
			{
				return executableDirectory.parent_path() / "Resources";
			}
			return executableDirectory / "resources";
		}


#if !defined(_WIN32)
		fs::path GetEnvironmentPath(char const* name)
		{
			char const* value = std::getenv(name);
			return value != nullptr && *value != '\0' ? PathFromUtf8(value) : fs::path{};
		}
#endif

#if defined(_WIN32)
		fs::path GetWideEnvironmentPath(wchar_t const* name)
		{
			std::size_t required = 0;
			_wgetenv_s(&required, nullptr, 0, name);
			if (required == 0)
				return {};

			std::vector<wchar_t> value(required);
			if (_wgetenv_s(&required, value.data(), value.size(), name) != 0 || required == 0)
				return {};
			return fs::path(value.data());
		}
#endif

		fs::path DiscoverExecutablePath(std::string& error)
		{
#if defined(_WIN32)
			std::vector<wchar_t> buffer(512);
			for (;;)
			{
				DWORD const size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
				if (size == 0)
				{
					error = "GetModuleFileNameW failed";
					return {};
				}
				if (static_cast<std::size_t>(size) < buffer.size())
					return fs::path(std::wstring_view(buffer.data(), static_cast<std::size_t>(size)));
				buffer.resize(buffer.size() * 2);
			}
#elif defined(__APPLE__)
			std::uint32_t required = 0;
			_NSGetExecutablePath(nullptr, &required);
			std::vector<char> buffer(required);
			if (_NSGetExecutablePath(buffer.data(), &required) != 0)
			{
				error = "_NSGetExecutablePath failed";
				return {};
			}
			std::error_code ec;
			fs::path path = fs::weakly_canonical(PathFromUtf8(buffer.data()), ec);
			if (ec)
			{
				error = "cannot resolve the executable path: " + ec.message();
				return {};
			}
			return path;
#else
			std::error_code ec;
			fs::path path = fs::read_symlink("/proc/self/exe", ec);
			if (ec)
			{
				error = "cannot resolve /proc/self/exe: " + ec.message();
				return {};
			}
			return path;
#endif
		}

		bool CopyLegacyTree(fs::path const& source, fs::path const& destination, std::string& error)
		{
			std::error_code ec;
			fs::create_directories(destination, ec);
			if (ec)
			{
				error = "cannot create data directory: " + ec.message();
				return false;
			}

			fs::recursive_directory_iterator it(source, fs::directory_options::skip_permission_denied, ec);
			fs::recursive_directory_iterator const end;
			if (ec)
			{
				error = "cannot list files in the old data folder: " + ec.message();
				return false;
			}
			for (; it != end; it.increment(ec))
			{
				if (ec)
				{
					error = "cannot list files in the old data folder: " + ec.message();
					return false;
				}

				fs::path const relative = it->path().lexically_relative(source);
				if (relative.empty())
				{
					error = "cannot resolve a legacy data path";
					return false;
				}
				fs::path const target = destination / relative;

				ec.clear();
				if (it->is_symlink(ec))
				{
					error = "legacy data contains a symbolic link and was not imported";
					return false;
				}
				if (ec)
				{
					error = "cannot inspect legacy data: " + ec.message();
					return false;
				}
				if (it->is_directory(ec))
				{
					fs::create_directories(target, ec);
				}
				else if (it->is_regular_file(ec))
				{
					fs::create_directories(target.parent_path(), ec);
					if (!ec)
						fs::copy_file(it->path(), target, fs::copy_options::none, ec);
				}
				else
				{
					error = "legacy data contains an unsupported file type";
					return false;
				}

				if (ec)
				{
					error = "cannot import legacy data: " + ec.message();
					return false;
				}
			}
			return true;
		}
	}

	AppPaths BuildAppPaths(PathEnvironment const& environment)
	{
		AppPaths paths;
		paths.platform = environment.platform;

		switch (environment.platform)
		{
		case HostPlatform::Windows:
			paths.dataDirectory = environment.roamingAppData / "Emerald Rogue Assistant";
			paths.configDirectory = paths.dataDirectory;
			paths.legacyDataDirectory = environment.roamingAppData / ".pokabbie" / "rogue_assistant";
			paths.legacySettingsFile = environment.currentWorkingDirectory / "settings.ini";
			break;
		case HostPlatform::MacOS:
			paths.dataDirectory =
				environment.homeDirectory / "Library" / "Application Support" / "assistant.emerald.rogue";
			paths.configDirectory = paths.dataDirectory;
			break;
		case HostPlatform::Linux:
			paths.dataDirectory = SelectPosixAbsoluteOrFallback(
				environment.xdgDataHome, environment.homeDirectory / ".local" / "share")
				/ "emerald-rogue-assistant";
			paths.configDirectory = SelectPosixAbsoluteOrFallback(
				environment.xdgConfigHome, environment.homeDirectory / ".config")
				/ "emerald-rogue-assistant";
			break;
		}

		paths.settingsFile = paths.configDirectory / "settings.ini";
		paths.logFile = paths.dataDirectory / "logs" / "RogueAssistant.log";
		paths.scriptDirectory = paths.dataDirectory / "scripts";
		paths.resourceDirectory = FindResourceDirectory(environment.platform, environment.executablePath);
		return paths;
	}

	std::optional<AppPaths> DiscoverAppPaths(std::string& error)
	{
		error.clear();
		PathEnvironment environment;
#if defined(_WIN32)
		environment.platform = HostPlatform::Windows;
		environment.roamingAppData = GetWideEnvironmentPath(L"APPDATA");
		environment.homeDirectory = GetWideEnvironmentPath(L"USERPROFILE");
		if (environment.roamingAppData.empty())
		{
			error = "APPDATA is not available";
			return std::nullopt;
		}
#elif defined(__APPLE__)
		environment.platform = HostPlatform::MacOS;
		environment.homeDirectory = GetEnvironmentPath("HOME");
#else
		environment.platform = HostPlatform::Linux;
		environment.homeDirectory = GetEnvironmentPath("HOME");
		environment.xdgDataHome = GetEnvironmentPath("XDG_DATA_HOME");
		environment.xdgConfigHome = GetEnvironmentPath("XDG_CONFIG_HOME");
#endif

		if (environment.homeDirectory.empty() && environment.platform != HostPlatform::Windows)
		{
			error = "HOME is not available";
			return std::nullopt;
		}

		std::error_code ec;
		environment.currentWorkingDirectory = fs::current_path(ec);
		if (ec)
		{
			error = "cannot determine the current directory: " + ec.message();
			return std::nullopt;
		}
		environment.executablePath = DiscoverExecutablePath(error);
		if (environment.executablePath.empty())
			return std::nullopt;

		return BuildAppPaths(environment);
	}

	MigrationReport ImportLegacyWindowsData(AppPaths const& paths)
	{
		MigrationReport report;
		if (paths.platform != HostPlatform::Windows || !paths.legacyDataDirectory || !paths.legacySettingsFile)
			return report;

		std::error_code ec;
		if (fs::exists(paths.dataDirectory, ec))
			return report;
		if (ec)
		{
			report.diagnostics.push_back("cannot inspect the new data directory: " + ec.message());
			return report;
		}

		report.attempted = true;
		bool const hasLegacyData = fs::is_directory(*paths.legacyDataDirectory, ec);
		if (ec)
		{
			report.diagnostics.push_back("cannot inspect legacy data: " + ec.message());
			return report;
		}
		if (hasLegacyData)
		{
			fs::path temporary = paths.dataDirectory;
			temporary += ".importing.";
			temporary += std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
			std::string error;
			if (!CopyLegacyTree(*paths.legacyDataDirectory, temporary, error))
			{
				std::error_code ignored;
				fs::remove_all(temporary, ignored);
				report.diagnostics.push_back(std::move(error));
				return report;
			}

			fs::create_directories(paths.dataDirectory.parent_path(), ec);
			if (!ec)
				fs::rename(temporary, paths.dataDirectory, ec);
			if (ec)
			{
				std::error_code ignored;
				fs::remove_all(temporary, ignored);
				report.diagnostics.push_back("cannot finish legacy data import: " + ec.message());
				return report;
			}
			report.importedData = true;
		}

		if (!fs::exists(paths.settingsFile, ec) && fs::is_regular_file(*paths.legacySettingsFile, ec))
		{
			fs::create_directories(paths.settingsFile.parent_path(), ec);
			if (!ec)
				fs::copy_file(*paths.legacySettingsFile, paths.settingsFile, fs::copy_options::none, ec);
			if (ec)
				report.diagnostics.push_back("cannot import legacy settings: " + ec.message());
			else
				report.importedSettings = true;
		}

		return report;
	}
}
