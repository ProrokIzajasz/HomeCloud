#pragma once

#include <cstdint>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace homecloud {

inline constexpr std::uintmax_t kDefaultQuotaBytes = 200ULL * 1024ULL * 1024ULL * 1024ULL;

struct StorageStatus {
    std::uintmax_t used_bytes{};
    std::uintmax_t quota_bytes{};

    [[nodiscard]] std::uintmax_t available_bytes() const noexcept;
};

struct FileEntry {
    std::filesystem::path relative_path;
    std::uintmax_t size_bytes{};
    bool is_directory{};
};

struct TrashEntry {
    std::string id;
    std::filesystem::path original_path;
    std::int64_t deleted_at_epoch_seconds{};
};

class Storage {
public:
    explicit Storage(std::filesystem::path root,
                     std::uintmax_t quota_bytes = kDefaultQuotaBytes);

    void initialize() const;
    [[nodiscard]] StorageStatus status() const;
    [[nodiscard]] bool can_store(std::uintmax_t incoming_bytes) const;
    [[nodiscard]] std::filesystem::path resolve_user_path(
        std::string_view relative_path) const;
    [[nodiscard]] std::filesystem::path available_name(
        std::string_view relative_path) const;
    [[nodiscard]] std::vector<FileEntry> list(std::string_view relative_path = ".") const;
    [[nodiscard]] std::vector<FileEntry> list_recursive(
        std::string_view relative_path = ".") const;
    [[nodiscard]] std::vector<FileEntry> search(std::string_view filename_query) const;
    void create_directory(std::string_view relative_path) const;
    [[nodiscard]] std::filesystem::path import(
        const std::filesystem::path& source,
        std::string_view destination_directory) const;
    [[nodiscard]] std::filesystem::path copy(
        std::string_view source,
        std::string_view destination_directory) const;
    [[nodiscard]] std::filesystem::path move(
        std::string_view source,
        std::string_view destination_directory) const;
    [[nodiscard]] std::filesystem::path rename(
        std::string_view source,
        std::string_view new_name) const;
    [[nodiscard]] std::string move_to_trash(std::string_view relative_path) const;
    [[nodiscard]] std::vector<TrashEntry> list_trash() const;
    [[nodiscard]] std::filesystem::path restore_from_trash(std::string_view id) const;
    void permanently_delete_from_trash(std::string_view id) const;
    void empty_trash() const;
    [[nodiscard]] std::size_t purge_expired_trash(
        std::chrono::hours maximum_age = std::chrono::hours(24 * 30)) const;
    [[nodiscard]] std::filesystem::path create_upload_staging_path(
        std::string_view upload_id) const;
    [[nodiscard]] std::filesystem::path commit_staged_upload(
        const std::filesystem::path& staged_file,
        std::string_view filename,
        std::string_view destination_directory) const;
    [[nodiscard]] const std::filesystem::path& root() const noexcept;

private:
    std::filesystem::path root_;
    std::uintmax_t quota_bytes_;

    [[nodiscard]] std::filesystem::path trash_root() const;
    [[nodiscard]] std::filesystem::path incoming_root() const;
};

} // namespace homecloud
