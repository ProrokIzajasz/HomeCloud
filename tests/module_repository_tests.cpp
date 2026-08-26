#include "homecloud/module_repository.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace std;

void run_module_repository_tests() {
    const auto root = filesystem::temp_directory_path() / "homecloud-module-tests";
    filesystem::remove_all(root);
    filesystem::create_directories(root);
    const auto apk = root / "source.apk";
    { ofstream output(apk, ios::binary); output << "test apk bytes"; }

    homecloud::ModuleRepository modules(root / "repository");
    modules.publish_android("what-to-eat", "0.1.9", apk);
    const auto catalog = modules.catalog_json();
    if (catalog.find("\"id\":\"what-to-eat\"") == string::npos ||
        catalog.find("\"version\":\"0.1.9\"") == string::npos ||
        catalog.find("\"sha256\":\"") == string::npos)
        throw runtime_error("Published Android release is missing from catalog");
    const auto download = modules.resolve_download("what-to-eat", "0.1.9", "android");
    if (!download || !filesystem::is_regular_file(*download))
        throw runtime_error("Published Android release cannot be resolved");
    if (modules.resolve_download("../escape", "0.1.9", "android") ||
        modules.resolve_download("what-to-eat", "../escape", "android") ||
        modules.resolve_download("what-to-eat", "0.1.9", "windows"))
        throw runtime_error("Unsafe module download path was accepted");
    bool duplicate_rejected = false;
    try { modules.publish_android("what-to-eat", "0.1.9", apk); }
    catch (const invalid_argument&) { duplicate_rejected = true; }
    if (!duplicate_rejected) throw runtime_error("Duplicate release was overwritten");
    filesystem::remove_all(root);
}
