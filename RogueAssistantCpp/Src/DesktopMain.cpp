#include "Application/Application.h"
#include "Application/CommandLine.h"
#include "GameConnectionManager.h"
#include "Platform/AppPaths.h"
#include "RogueAssistantVersion.h"
#include "UI/PrimaryUI.h"
#include "UI/Window.h"

#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
void PrintHelp()
{
	std::cout << rogue::app::DesktopUsage();
}

struct DesktopLoop
{
	rogue::app::Application& application;
	PrimaryUI& ui;
};

WindowFrame RenderDesktopFrame(Window* window, void* userData)
{
	auto& loop = *static_cast<DesktopLoop*>(userData);
	auto const snapshot = loop.application.Snapshot();
	bool const rendered = loop.ui.Render(
		*window, snapshot, [&loop](rogue::app::UiCommand command) { return loop.application.Submit(std::move(command)); });
	return rendered ? WindowFrame::Rendered : WindowFrame::Idle;
}
} // namespace

int main(int argc, char** argv)
{
	std::vector<std::string_view> arguments;
	arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
	for (int index = 1; index < argc; ++index)
		arguments.emplace_back(argv[index]);
	rogue::app::DesktopOptions const options = rogue::app::ParseDesktopOptions(arguments);
	if (!options.error.empty())
	{
		std::cerr << ROGUE_ASSISTANT_DISPLAY_NAME ": " << options.error << '\n';
		PrintHelp();
		return 2;
	}
	if (options.showHelp)
	{
		PrintHelp();
		return 0;
	}
	if (options.showVersion)
	{
		std::cout << ROGUE_ASSISTANT_DISPLAY_NAME " " ROGUE_ASSISTANT_VERSION_STRING << '\n';
		return 0;
	}

	try
	{
		std::string pathError;
		auto const paths = rogue::platform::DiscoverAppPaths(pathError);
		if (!paths)
			throw std::runtime_error("Cannot find the application folders. " + pathError);
		std::filesystem::path const resourceDirectory = paths->resourceDirectory;

		WindowConfig windowConfig;
		windowConfig.title = ROGUE_ASSISTANT_DISPLAY_NAME;
		windowConfig.width = 768;
		windowConfig.height = 576;
		windowConfig.canBeDestroyed = true;
		windowConfig.resourceDirectory = resourceDirectory;

		Window window(windowConfig);
		PrimaryUI ui(resourceDirectory);
		if (!window.Create())
		{
			std::cerr << ROGUE_ASSISTANT_DISPLAY_NAME ": Cannot open the application window.\n";
			return 1;
		}

		rogue::app::Application application(std::make_unique<GameConnectionManager>(options.bridgePort));
		DesktopLoop loop{application, ui};
		window.EnterMainLoop(RenderDesktopFrame, &loop);
		application.Stop();
		bool const failed = application.Snapshot().workerState == rogue::app::WorkerState::Failed;
		return failed ? 1 : 0;
	}
	catch (std::exception const& exception)
	{
		std::cerr << ROGUE_ASSISTANT_DISPLAY_NAME ": " << exception.what() << '\n';
		return 1;
	}
}
