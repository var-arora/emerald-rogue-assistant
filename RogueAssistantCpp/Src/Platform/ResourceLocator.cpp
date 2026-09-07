#include "Platform/ResourceLocator.h"

#include <utility>

namespace rogue::platform
{
	std::string_view ResourceFileName(Resource resource)
	{
		switch (resource)
		{
		case Resource::Font:
			return "pokemon-emerald-pro.ttf";
		case Resource::Frame:
			return "poketch_frame.png";
		case Resource::Icon:
			return "WobbuffetImage.png";
		case Resource::BridgeScript:
			return "RogueAssistant_mGBA.lua";
		}
		return {};
	}

	ResourceLocator::ResourceLocator(std::filesystem::path resourceDirectory)
		: m_ResourceDirectory(std::move(resourceDirectory))
	{
	}

	std::filesystem::path ResourceLocator::Resolve(Resource resource) const
	{
		return m_ResourceDirectory / ResourceFileName(resource);
	}

	bool ResourceLocator::Exists(Resource resource) const
	{
		std::error_code ec;
		return std::filesystem::is_regular_file(Resolve(resource), ec);
	}
}
