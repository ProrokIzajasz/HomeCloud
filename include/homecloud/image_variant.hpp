#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace homecloud {

struct ImageVariant {
    std::vector<std::uint8_t> bytes;
    std::uint32_t width{};
    std::uint32_t height{};
};

[[nodiscard]] std::optional<ImageVariant> create_jpeg_variant(
    const std::filesystem::path& source,
    std::uint32_t maximum_width,
    std::uint32_t maximum_height,
    float quality);

} // namespace homecloud
