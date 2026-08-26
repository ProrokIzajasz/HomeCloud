#include "homecloud/storage.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace std;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_bytes(const std::filesystem::path& path, std::size_t count) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << std::string(count, 'x');
}

} // namespace

void run_auth_tests();
void run_module_repository_tests();

int main() {
    const auto test_root = std::filesystem::temp_directory_path() / "homecloud-storage-tests";
    std::filesystem::remove_all(test_root);

    try {
        homecloud::Storage storage(test_root, 100);
        storage.initialize();
        require(std::filesystem::is_directory(test_root), "Storage root was not created");

        write_bytes(test_root / "folder" / "file.txt", 40);
        const auto status = storage.status();
        require(status.used_bytes == 40, "Used space is incorrect");
        require(status.available_bytes() == 60, "Available space is incorrect");
        require(storage.can_store(60), "Exact remaining size should be accepted");
        require(!storage.can_store(61), "Upload exceeding quota should be rejected");

        bool traversal_rejected = false;
        try {
            static_cast<void>(storage.resolve_user_path("../outside.txt"));
        } catch (const std::invalid_argument&) {
            traversal_rejected = true;
        }
        require(traversal_rejected, "Parent traversal was not rejected");

        write_bytes(test_root / "photo.jpg", 1);
        write_bytes(test_root / "photo (2).jpg", 1);
        require(storage.available_name("photo.jpg").filename() == "photo (3).jpg",
                "Duplicate filename was not incremented");

        storage.create_directory("documents");
        const auto imported = storage.import(test_root / "photo.jpg", "documents");
        require(imported == std::filesystem::path("documents/photo.jpg"),
                "File was not imported into the requested directory");

        const auto copied = storage.copy("documents/photo.jpg", ".");
        require(copied == std::filesystem::path("photo (3).jpg"),
                "Copied file did not receive a conflict-free name");

        storage.create_directory("archive");
        const auto moved = storage.move("documents/photo.jpg", "archive");
        require(moved == std::filesystem::path("archive/photo.jpg"),
                "File was not moved to the requested directory");

        const auto renamed = storage.rename("archive/photo.jpg", "memory.jpg");
        require(renamed == std::filesystem::path("archive/memory.jpg"),
                "File was not renamed");

        const auto search_results = storage.search("MEMORY");
        require(search_results.size() == 1 &&
                    search_results.front().relative_path == std::filesystem::path("archive/memory.jpg"),
                "Case-insensitive filename search failed");

        const auto manifest = storage.list_recursive("archive");
        require(manifest.size() == 1 &&
                    manifest.front().relative_path == std::filesystem::path("archive/memory.jpg"),
                "Recursive folder manifest is incorrect");

        const auto trash_id = storage.move_to_trash("archive/memory.jpg");
        require(!std::filesystem::exists(test_root / "archive" / "memory.jpg"),
                "Trashed file is still visible in its original location");
        const auto trash_entries = storage.list_trash();
        require(trash_entries.size() == 1, "Trash entry was not listed");
        require(trash_entries.front().deleted_at_epoch_seconds > 0,
                "Trash deletion timestamp was not stored");
        const auto restored = storage.restore_from_trash(trash_id);
        require(restored == std::filesystem::path("archive/memory.jpg"),
                "Trash entry was not restored to its original location");

        const auto second_trash_id = storage.move_to_trash("archive/memory.jpg");
        storage.permanently_delete_from_trash(second_trash_id);
        require(storage.list_trash().empty(), "Trash entry was not permanently deleted");

        const auto root_entries = storage.list();
        const auto internal_visible = std::ranges::any_of(root_entries, [](const auto& entry) {
            return entry.relative_path.filename() == ".homecloud";
        });
        require(!internal_visible, "Internal metadata directory is visible to users");

        run_auth_tests();
        run_module_repository_tests();

        std::filesystem::remove_all(test_root);
        std::cout << "All HomeCloud storage tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::filesystem::remove_all(test_root);
        std::cerr << "Test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
