#include "Application/CommandLine.h"

#include "Platform/Configuration.h"
#include "RogueAssistantVersion.h"

namespace rogue::app
{
DesktopOptions ParseDesktopOptions(std::span<std::string_view const> arguments)
{
	DesktopOptions options;
	bool bridgePortSeen = false;
	for (std::size_t index = 0; index < arguments.size(); ++index)
	{
		std::string_view const argument = arguments[index];
		if (argument == "--help" || argument == "-h")
		{
			options.showHelp = true;
			continue;
		}
		if (argument == "--version")
		{
			options.showVersion = true;
			continue;
		}

		std::string_view value;
		if (argument == "--bridge-port")
		{
			if (++index >= arguments.size())
			{
				options.error = "Enter a value after --bridge-port.";
				return options;
			}
			value = arguments[index];
		}
		else if (argument.starts_with("--bridge-port="))
		{
			value = argument.substr(std::string_view("--bridge-port=").size());
		}
		else
		{
			options.error = "Unknown option: " + std::string(argument) + ".";
			return options;
		}

		if (bridgePortSeen)
		{
			options.error = "Use --bridge-port only once.";
			return options;
		}
		bridgePortSeen = true;
		platform::Settings candidate;
		if (!platform::TrySetSetting(candidate, platform::BridgePortKey, value, options.error))
		{
			options.error = "Set --bridge-port to a number from 1 to 65535.";
			return options;
		}
		options.bridgePort = candidate.bridgePort;
	}
	return options;
}

std::uint16_t SelectBridgePort(std::optional<std::uint16_t> currentRunOverride, std::uint16_t configuredPort)
{
	return currentRunOverride.value_or(configuredPort);
}

std::string DesktopUsage()
{
	return ROGUE_ASSISTANT_DISPLAY_NAME " " ROGUE_ASSISTANT_VERSION_STRING
		   "\nUsage: RogueAssistant [--bridge-port PORT] [--version] [--help]\n";
}
} // namespace rogue::app
