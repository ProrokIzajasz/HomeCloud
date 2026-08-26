#include "homecloud/storage.hpp"
#include "homecloud/platform.hpp"

#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>

using namespace std;

namespace {

double gib(std::uintmax_t bytes) {
    return static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const filesystem::path root = argc > 1 ? argv[1] : homecloud::default_storage_path();
        homecloud::Storage storage(root);
        storage.initialize();
        const auto status = storage.status();

        std::cout << "HomeCloud storage is ready\n"
                  << "Path: " << storage.root().string() << '\n'
                  << std::fixed << std::setprecision(2)
                  << "Used: " << gib(status.used_bytes) << " GiB\n"
                  << "Available: " << gib(status.available_bytes()) << " GiB\n"
                  << "Quota: " << gib(status.quota_bytes) << " GiB\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "HomeCloud error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
