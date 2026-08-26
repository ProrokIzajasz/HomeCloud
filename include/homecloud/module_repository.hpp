#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace homecloud {

class ModuleRepository {
public:
    explicit ModuleRepository(std::filesystem::path root);

    void initialize() const;
    void publish_android(std::string_view module_id,
                         std::string_view version,
                         const std::filesystem::path& apk);
    [[nodiscard]] std::string catalog_json() const;
    [[nodiscard]] std::optional<std::filesystem::path> resolve_download(
        std::string_view module_id, std::string_view version,
        std::string_view platform) const;
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return root_; }

private:
    std::filesystem::path root_;
};

} // namespace homecloud
