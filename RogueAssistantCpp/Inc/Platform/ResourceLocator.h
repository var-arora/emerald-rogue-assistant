#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace rogue::platform
{
	enum class Resource : std::uint8_t
	{
		Font,
		Frame,
		Icon,
		BridgeScript,
	};

	[[nodiscard]] std::string_view ResourceFileName(Resource resource);

	class ResourceLocator
	{
	public:
		explicit ResourceLocator(std::filesystem::path resourceDirectory);

		[[nodiscard]] std::filesystem::path Resolve(Resource resource) const;
		[[nodiscard]] bool Exists(Resource resource) const;
		[[nodiscard]] std::filesystem::path const& Directory() const { return m_ResourceDirectory; }

	private:
		std::filesystem::path m_ResourceDirectory;
	};
}
