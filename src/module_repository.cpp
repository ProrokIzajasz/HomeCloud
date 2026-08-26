#include "homecloud/module_repository.hpp"
#include "homecloud/crypto.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <ranges>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace std;

namespace homecloud {
namespace {

struct ModuleInfo {
    string_view id;
    string_view name;
    string_view description;
    string_view category;
    string_view launch_uri;
    bool coming_soon;
};

constexpr ModuleInfo kModules[] {
    {"homecloud", "HomeCloud", "Twoje pliki, zdjęcia i kopie zapasowe w prywatnej chmurze.", "Chmura", "homecloud://open", false},
    {"what-to-eat", "What to Eat", "Pomysły na posiłki, przepisy i wspólne listy zakupów.", "Kuchnia", "whattoeat://open", false},
    {"smart-home", "Smart Home", "Sterowanie urządzeniami i automatyzacjami domu.", "Dom", "", true}
};

const ModuleInfo* module_info(string_view id) {
    const auto found = ranges::find_if(kModules, [id](const auto& module) { return module.id == id; });
    return found == end(kModules) ? nullptr : &*found;
}

bool valid_version(string_view value) {
    return !value.empty() && value.size() <= 32 &&
        ranges::all_of(value, [](unsigned char c) { return isalnum(c) || c == '.' || c == '-'; }) &&
        value.find("..") == string_view::npos;
}

string json_escape(string_view value) {
    string output;
    for (const unsigned char c : value) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else if (c >= 0x20) output += static_cast<char>(c);
    }
    return output;
}

struct ReleaseMeta {
    string filename;
    string sha256;
    uintmax_t size{};
};

optional<ReleaseMeta> read_meta(const filesystem::path& directory) {
    ifstream input(directory / "release.meta", ios::binary);
    ReleaseMeta meta;
    string size;
    if (!getline(input, meta.filename) || !getline(input, meta.sha256) || !getline(input, size)) return nullopt;
    if (meta.filename.empty() || filesystem::path(meta.filename).filename() != meta.filename ||
        meta.sha256.size() != 64 || !ranges::all_of(meta.sha256, [](unsigned char c) { return isxdigit(c); })) return nullopt;
    try { meta.size = stoull(size); } catch (...) { return nullopt; }
    error_code error;
    const auto apk = directory / meta.filename;
    if (!filesystem::is_regular_file(apk, error) || filesystem::file_size(apk, error) != meta.size || error) return nullopt;
    return meta;
}

string iso_timestamp() {
    const auto now = chrono::system_clock::to_time_t(chrono::system_clock::now());
    tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char buffer[32]{};
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

} // namespace

ModuleRepository::ModuleRepository(filesystem::path root)
    : root_(filesystem::absolute(std::move(root)).lexically_normal()) {}

void ModuleRepository::initialize() const { filesystem::create_directories(root_); }

void ModuleRepository::publish_android(string_view module_id, string_view version,
                                       const filesystem::path& apk) {
    const auto* module = module_info(module_id);
    if (!module || module->coming_soon) throw invalid_argument("Unknown or unavailable module id");
    if (!valid_version(version)) throw invalid_argument("Version contains unsupported characters");
    const auto source = filesystem::absolute(apk).lexically_normal();
    if (source.extension() != ".apk" || !filesystem::is_regular_file(source))
        throw invalid_argument("A regular .apk file is required");

    initialize();
    const auto target_directory = root_ / string(module_id) / "android" / string(version);
    if (filesystem::exists(target_directory)) throw invalid_argument("This module version already exists");
    const auto staging = root_ / (".publish-" + crypto::hex_encode(crypto::random_bytes(12)));
    filesystem::create_directories(staging);
    try {
        const string filename = string(module_id) + "-android-v" + string(version) + ".apk";
        const auto target = staging / filename;
        filesystem::copy_file(source, target, filesystem::copy_options::none);
        const auto size = filesystem::file_size(target);
        const auto digest = crypto::hex_encode(crypto::sha256_file(target));
        ofstream metadata(staging / "release.meta", ios::binary | ios::trunc);
        metadata << filename << '\n' << digest << '\n' << size << '\n';
        metadata.flush();
        if (!metadata) throw runtime_error("Could not persist module metadata");
        metadata.close();
        filesystem::create_directories(target_directory.parent_path());
        filesystem::rename(staging, target_directory);
    } catch (...) {
        error_code ignored;
        filesystem::remove_all(staging, ignored);
        throw;
    }
}

string ModuleRepository::catalog_json() const {
    ostringstream json;
    json << "{\"schemaVersion\":1,\"publishedAt\":\"" << iso_timestamp() << "\",\"modules\":[";
    for (size_t module_index = 0; module_index < size(kModules); ++module_index) {
        const auto& module = kModules[module_index];
        if (module_index) json << ',';
        json << "{\"id\":\"" << module.id << "\",\"name\":\"" << json_escape(module.name)
             << "\",\"description\":\"" << json_escape(module.description)
             << "\",\"category\":\"" << module.category
             << "\",\"tags\":[],\"comingSoon\":" << (module.coming_soon ? "true" : "false");
        if (!module.launch_uri.empty()) json << ",\"launchUri\":\"" << module.launch_uri << '"';
        json << ",\"releases\":[";
        bool first_release = true;
        const auto platform_root = root_ / string(module.id) / "android";
        error_code ignored;
        if (filesystem::is_directory(platform_root, ignored)) {
            vector<filesystem::path> versions;
            for (const auto& entry : filesystem::directory_iterator(platform_root, ignored))
                if (entry.is_directory()) versions.push_back(entry.path());
            ranges::sort(versions, greater{}, [](const auto& path) { return path.filename().string(); });
            for (const auto& directory : versions) {
                const auto version = directory.filename().string();
                const auto meta = valid_version(version) ? read_meta(directory) : nullopt;
                if (!meta) continue;
                if (!first_release) json << ',';
                first_release = false;
                json << "{\"platform\":\"android\",\"architecture\":\"any\",\"version\":\""
                     << version << "\",\"downloadUrl\":\"/api/hiphop/modules/" << module.id << '/'
                     << version << "/download?platform=android\",\"sha256\":\"" << meta->sha256
                     << "\",\"sizeBytes\":" << meta->size << ",\"fileName\":\""
                     << json_escape(meta->filename) << "\",\"minimumHipHopVersion\":\"0.1.1\"}";
            }
        }
        json << "]}";
    }
    json << "]}";
    return json.str();
}

optional<filesystem::path> ModuleRepository::resolve_download(string_view module_id,
                                                               string_view version,
                                                               string_view platform) const {
    if (!module_info(module_id) || !valid_version(version) || platform != "android") return nullopt;
    const auto directory = root_ / string(module_id) / "android" / string(version);
    const auto meta = read_meta(directory);
    if (!meta) return nullopt;
    error_code error;
    const auto root_canonical = filesystem::weakly_canonical(root_, error);
    if (error) return nullopt;
    const auto file = filesystem::weakly_canonical(directory / meta->filename, error);
    if (error) return nullopt;
    const auto relative = filesystem::relative(file, root_canonical, error);
    if (error || relative.empty() || *relative.begin() == "..") return nullopt;
    return file;
}

} // namespace homecloud
