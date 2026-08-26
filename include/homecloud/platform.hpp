#pragma once

#include <filesystem>

namespace homecloud {

[[nodiscard]] inline std::filesystem::path default_storage_path() {
#ifdef _WIN32
    return "D:\\PrivateCloud";
#else
    return "/srv/homecloud/data";
#endif
}

} // namespace homecloud

