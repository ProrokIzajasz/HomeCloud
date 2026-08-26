#include "homecloud/storage.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <system_error>

using namespace std;

namespace homecloud {
namespace {

bool is_within(const std::filesystem::path& root,
               const std::filesystem::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *root_it != *candidate_it) {
            return false;
        }
    }
    return true;
}

std::uintmax_t directory_size(const std::filesystem::path& root) {
    std::uintmax_t total = 0;
    std::error_code error;
    const auto options = std::filesystem::directory_options::skip_permission_denied;

    for (std::filesystem::recursive_directory_iterator it(root, options, error), end;
         it != end; it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (it->is_regular_file(error)) {
            const auto size = it->file_size(error);
            if (!error) {
                total += size;
            }
            error.clear();
        }
    }
    return total;
}

std::string next_trash_id() {
    static std::atomic_uint64_t counter{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    return std::to_string(now) + "-" + std::to_string(counter.fetch_add(1));
}

void copy_path(const std::filesystem::path& source,
               const std::filesystem::path& destination) {
    if (std::filesystem::is_directory(source)) {
        std::filesystem::copy(source, destination,
                              std::filesystem::copy_options::recursive);
    } else {
        std::filesystem::copy_file(source, destination);
    }
}

} // namespace

std::uintmax_t StorageStatus::available_bytes() const noexcept {
    return used_bytes >= quota_bytes ? 0 : quota_bytes - used_bytes;
}

Storage::Storage(std::filesystem::path root, std::uintmax_t quota_bytes)
    : root_(std::filesystem::absolute(std::move(root)).lexically_normal()),
      quota_bytes_(quota_bytes) {
    if (quota_bytes_ == 0) {
        throw std::invalid_argument("Storage quota must be greater than zero");
    }
}

void Storage::initialize() const {
    std::filesystem::create_directories(root_);
    std::filesystem::create_directories(trash_root());
    std::filesystem::create_directories(incoming_root());
}

StorageStatus Storage::status() const {
    if (!std::filesystem::exists(root_)) {
        return {0, quota_bytes_};
    }
    return {directory_size(root_), quota_bytes_};
}

bool Storage::can_store(std::uintmax_t incoming_bytes) const {
    return incoming_bytes <= status().available_bytes();
}

std::filesystem::path Storage::resolve_user_path(std::string_view relative_path) const {
    const std::filesystem::path requested{relative_path};
    if (requested.empty() || requested.is_absolute() || requested.has_root_name() ||
        requested.has_root_directory()) {
        throw std::invalid_argument("A non-empty relative path is required");
    }

    const auto resolved = (root_ / requested).lexically_normal();
    if (!is_within(root_, resolved) || resolved == root_) {
        throw std::invalid_argument("Path escapes the storage root");
    }
    return resolved;
}

std::filesystem::path Storage::available_name(std::string_view relative_path) const {
    const auto requested = resolve_user_path(relative_path);
    if (!std::filesystem::exists(requested)) {
        return requested;
    }

    const auto parent = requested.parent_path();
    const auto stem = requested.stem().wstring();
    const auto extension = requested.extension().wstring();
    for (std::uint64_t index = 2;; ++index) {
        const auto candidate = parent / (stem + L" (" + std::to_wstring(index) + L")" + extension);
        if (!std::filesystem::exists(candidate)) {
            return candidate;
        }
    }
}

std::vector<FileEntry> Storage::list(std::string_view relative_path) const {
    const auto directory = relative_path == "." ? root_ : resolve_user_path(relative_path);
    if (!std::filesystem::is_directory(directory)) {
        throw std::invalid_argument("Requested path is not a directory");
    }

    std::vector<FileEntry> entries;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (directory == root_ &&
            (item.path().filename() == ".homecloud" ||
             item.path().filename() == "cloud-storage.json")) {
            continue;
        }
        const bool directory_entry = item.is_directory();
        entries.push_back({item.path().lexically_relative(root_),
                           directory_entry ? directory_size(item.path()) : item.file_size(),
                           directory_entry});
    }
    std::ranges::sort(entries, [](const FileEntry& left, const FileEntry& right) {
        if (left.is_directory != right.is_directory) {
            return left.is_directory > right.is_directory;
        }
        return left.relative_path.native() < right.relative_path.native();
    });
    return entries;
}

vector<FileEntry> Storage::search(string_view filename_query) const {
    if (filename_query.empty()) {
        throw invalid_argument("Search query cannot be empty");
    }
    string query(filename_query);
    ranges::transform(query, query.begin(), [](unsigned char character) {
        return static_cast<char>(tolower(character));
    });
    vector<FileEntry> matches;
    error_code error;
    for (filesystem::recursive_directory_iterator it(
             root_, filesystem::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (it->path() == root_ / ".homecloud") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->path() == root_ / "cloud-storage.json") continue;
        auto filename = it->path().filename().string();
        ranges::transform(filename, filename.begin(), [](unsigned char character) {
            return static_cast<char>(tolower(character));
        });
        if (filename.find(query) == string::npos) continue;
        const bool directory_entry = it->is_directory(error);
        const auto size = directory_entry ? directory_size(it->path()) : it->file_size(error);
        if (!error) {
            matches.push_back({it->path().lexically_relative(root_), size, directory_entry});
        }
        error.clear();
    }
    ranges::sort(matches, [](const FileEntry& left, const FileEntry& right) {
        return left.relative_path.native() < right.relative_path.native();
    });
    return matches;
}

vector<FileEntry> Storage::list_recursive(string_view relative_path) const {
    const auto directory = relative_path == "." ? root_ : resolve_user_path(relative_path);
    if (!filesystem::is_directory(directory)) {
        throw invalid_argument("Requested path is not a directory");
    }
    vector<FileEntry> entries;
    error_code error;
    for (filesystem::recursive_directory_iterator it(
             directory, filesystem::directory_options::skip_permission_denied, error), end;
         it != end; it.increment(error)) {
        if (error) {
            error.clear();
            continue;
        }
        if (it->path() == root_ / ".homecloud") {
            it.disable_recursion_pending();
            continue;
        }
        if (it->path() == root_ / "cloud-storage.json") continue;
        const bool directory_entry = it->is_directory(error);
        const auto size = directory_entry ? 0 : it->file_size(error);
        if (!error) {
            entries.push_back({it->path().lexically_relative(root_), size, directory_entry});
        }
        error.clear();
    }
    ranges::sort(entries, [](const FileEntry& left, const FileEntry& right) {
        return left.relative_path.native() < right.relative_path.native();
    });
    return entries;
}

void Storage::create_directory(std::string_view relative_path) const {
    const auto destination = resolve_user_path(relative_path);
    if (!std::filesystem::create_directories(destination) &&
        !std::filesystem::is_directory(destination)) {
        throw std::runtime_error("Could not create directory");
    }
}

std::filesystem::path Storage::import(const std::filesystem::path& source,
                                      std::string_view destination_directory) const {
    if (!std::filesystem::exists(source)) {
        throw std::invalid_argument("Import source does not exist");
    }
    const auto destination_root = destination_directory == "."
        ? root_ : resolve_user_path(destination_directory);
    if (!std::filesystem::is_directory(destination_root)) {
        throw std::invalid_argument("Import destination is not a directory");
    }
    const auto incoming_size = std::filesystem::is_directory(source)
        ? directory_size(source) : std::filesystem::file_size(source);
    if (!can_store(incoming_size)) {
        throw std::runtime_error("Storage quota exceeded");
    }
    const auto requested = (destination_root / source.filename()).lexically_relative(root_);
    const auto destination = available_name(requested.generic_string());
    if (std::filesystem::is_directory(source) && is_within(source, destination)) {
        throw std::invalid_argument("Cannot copy a directory into itself");
    }
    copy_path(source, destination);
    return destination.lexically_relative(root_);
}

std::filesystem::path Storage::copy(std::string_view source,
                                    std::string_view destination_directory) const {
    const auto source_path = resolve_user_path(source);
    return import(source_path, destination_directory);
}

std::filesystem::path Storage::move(std::string_view source,
                                    std::string_view destination_directory) const {
    const auto source_path = resolve_user_path(source);
    const auto destination_root = destination_directory == "."
        ? root_ : resolve_user_path(destination_directory);
    if (!std::filesystem::exists(source_path) || !std::filesystem::is_directory(destination_root)) {
        throw std::invalid_argument("Move source or destination is invalid");
    }
    const auto requested = (destination_root / source_path.filename()).lexically_relative(root_);
    const auto destination = available_name(requested.generic_string());
    if (std::filesystem::is_directory(source_path) && is_within(source_path, destination)) {
        throw std::invalid_argument("Cannot move a directory into itself");
    }
    std::filesystem::rename(source_path, destination);
    return destination.lexically_relative(root_);
}

std::filesystem::path Storage::rename(std::string_view source,
                                      std::string_view new_name) const {
    const std::filesystem::path name{new_name};
    if (name.empty() || name.has_parent_path() || name == "." || name == "..") {
        throw std::invalid_argument("New name must contain one valid path component");
    }
    const auto source_path = resolve_user_path(source);
    if (!std::filesystem::exists(source_path)) {
        throw std::invalid_argument("Rename source does not exist");
    }
    const auto requested = (source_path.parent_path() / name).lexically_relative(root_);
    const auto destination = available_name(requested.generic_string());
    std::filesystem::rename(source_path, destination);
    return destination.lexically_relative(root_);
}

std::string Storage::move_to_trash(std::string_view relative_path) const {
    const auto source = resolve_user_path(relative_path);
    if (!std::filesystem::exists(source)) {
        throw std::invalid_argument("Trash source does not exist");
    }
    const auto id = next_trash_id();
    const auto entry = trash_root() / id;
    std::filesystem::create_directories(entry);
    std::ofstream metadata(entry / "original-path.txt", std::ios::binary);
    metadata << source.lexically_relative(root_).generic_string();
    metadata.close();
    if (!metadata) {
        std::filesystem::remove_all(entry);
        throw std::runtime_error("Could not write trash metadata");
    }
    ofstream deleted_at(entry / "deleted-at.txt", ios::binary);
    deleted_at << chrono::duration_cast<chrono::seconds>(
        chrono::system_clock::now().time_since_epoch()).count();
    deleted_at.close();
    if (!deleted_at) {
        filesystem::remove_all(entry);
        throw runtime_error("Could not write trash timestamp");
    }
    std::filesystem::rename(source, entry / "payload");
    return id;
}

std::vector<TrashEntry> Storage::list_trash() const {
    std::vector<TrashEntry> entries;
    if (!std::filesystem::exists(trash_root())) {
        return entries;
    }
    for (const auto& item : std::filesystem::directory_iterator(trash_root())) {
        if (!item.is_directory()) {
            continue;
        }
        std::ifstream metadata(item.path() / "original-path.txt", std::ios::binary);
        std::string original((std::istreambuf_iterator<char>(metadata)),
                             std::istreambuf_iterator<char>());
        if (!original.empty() && std::filesystem::exists(item.path() / "payload")) {
            ifstream deleted_at(item.path() / "deleted-at.txt", ios::binary);
            int64_t epoch_seconds = 0;
            deleted_at >> epoch_seconds;
            entries.push_back({item.path().filename().string(), original, epoch_seconds});
        }
    }
    return entries;
}

std::filesystem::path Storage::restore_from_trash(std::string_view id) const {
    const std::filesystem::path safe_id{id};
    if (safe_id.empty() || safe_id.has_parent_path()) {
        throw std::invalid_argument("Invalid trash identifier");
    }
    const auto entry = trash_root() / safe_id;
    std::ifstream metadata(entry / "original-path.txt", std::ios::binary);
    std::string original((std::istreambuf_iterator<char>(metadata)),
                         std::istreambuf_iterator<char>());
    metadata.close();
    if (original.empty() || !std::filesystem::exists(entry / "payload")) {
        throw std::invalid_argument("Trash entry does not exist");
    }
    const auto requested = resolve_user_path(original);
    std::filesystem::create_directories(requested.parent_path());
    const auto destination = available_name(
        requested.lexically_relative(root_).generic_string());
    std::filesystem::rename(entry / "payload", destination);
    std::filesystem::remove_all(entry);
    return destination.lexically_relative(root_);
}

void Storage::permanently_delete_from_trash(std::string_view id) const {
    const std::filesystem::path safe_id{id};
    if (safe_id.empty() || safe_id.has_parent_path()) {
        throw std::invalid_argument("Invalid trash identifier");
    }
    const auto entry = trash_root() / safe_id;
    if (!std::filesystem::exists(entry)) {
        throw std::invalid_argument("Trash entry does not exist");
    }
    std::filesystem::remove_all(entry);
}

void Storage::empty_trash() const {
    if (!filesystem::exists(trash_root())) return;
    for (const auto& entry : filesystem::directory_iterator(trash_root())) {
        filesystem::remove_all(entry.path());
    }
}

size_t Storage::purge_expired_trash(chrono::hours maximum_age) const {
    const auto cutoff = chrono::system_clock::now() - maximum_age;
    size_t removed = 0;
    for (const auto& entry : list_trash()) {
        if (entry.deleted_at_epoch_seconds == 0) continue;
        const auto deleted_at = chrono::system_clock::time_point(
            chrono::seconds(entry.deleted_at_epoch_seconds));
        if (deleted_at <= cutoff) {
            permanently_delete_from_trash(entry.id);
            ++removed;
        }
    }
    return removed;
}

filesystem::path Storage::create_upload_staging_path(string_view upload_id) const {
    const filesystem::path safe_id{upload_id};
    if (safe_id.empty() || safe_id.has_parent_path()) {
        throw invalid_argument("Invalid upload identifier");
    }
    filesystem::create_directories(incoming_root());
    return incoming_root() / safe_id;
}

filesystem::path Storage::commit_staged_upload(const filesystem::path& staged_file,
                                               string_view filename,
                                               string_view destination_directory) const {
    const auto normalized_staged = filesystem::absolute(staged_file).lexically_normal();
    if (normalized_staged.parent_path() != incoming_root() ||
        !filesystem::is_regular_file(normalized_staged)) {
        throw invalid_argument("Invalid staged upload");
    }
    const filesystem::path safe_name{filename};
    if (safe_name.empty() || safe_name.has_parent_path() || safe_name == "." ||
        safe_name == "..") {
        throw invalid_argument("Filename must contain one valid path component");
    }
    const auto destination_root = destination_directory == "."
        ? root_ : resolve_user_path(destination_directory);
    if (!filesystem::is_directory(destination_root)) {
        throw invalid_argument("Upload destination is not a directory");
    }
    const auto requested = (destination_root / safe_name).lexically_relative(root_);
    const auto destination = available_name(requested.generic_string());
    filesystem::rename(normalized_staged, destination);
    return destination.lexically_relative(root_);
}

std::filesystem::path Storage::trash_root() const {
    return root_ / ".homecloud" / "trash";
}

filesystem::path Storage::incoming_root() const {
    return root_ / ".homecloud" / "incoming";
}

const std::filesystem::path& Storage::root() const noexcept {
    return root_;
}

} // namespace homecloud
