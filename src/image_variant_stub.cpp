#include "homecloud/image_variant.hpp"

namespace homecloud {

std::optional<ImageVariant> create_jpeg_variant(const std::filesystem::path&,
                                                std::uint32_t,
                                                std::uint32_t,
                                                float) {
    return std::nullopt;
}

} // namespace homecloud
