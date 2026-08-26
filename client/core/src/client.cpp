#include "homecloud_client/client.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <stdexcept>
#include <utility>

using namespace std;
using json = nlohmann::json;

namespace homecloud_client {
namespace {

string body_text(const HttpResponse& response) {
    return string(response.body.begin(), response.body.end());
}

vector<uint8_t> bytes(string_view text) {
    return vector<uint8_t>(text.begin(), text.end());
}

} // namespace

bool Client::authenticated() const noexcept { return !token_.empty(); }
const string& Client::username() const noexcept { return username_; }
const string& Client::current_path() const noexcept { return current_path_; }

HttpRequest Client::login_request(string_view username, string_view password) const {
    const auto form = "username=" + percent_encode(username) + "&password=" + percent_encode(password);
    HttpRequest request{HttpMethod::post, "/api/v1/login", {}, {}};
    request.headers["Content-Type"] = "application/x-www-form-urlencoded";
    request.body = bytes(form);
    return request;
}

void Client::accept_login(string_view username, const HttpResponse& response) {
    require_success(response);
    const auto document = json::parse(body_text(response));
    token_ = document.at("token").get<string>();
    if (token_.size() != 64) throw runtime_error("Server returned an invalid session token");
    username_ = username;
}

HttpRequest Client::logout_request() const { return authorized(HttpMethod::post, "/api/v1/logout"); }

void Client::clear_session() {
    token_.clear(); username_.clear(); current_path_ = ".";
}

HttpRequest Client::storage_request() const { return authorized(HttpMethod::get, "/api/v1/storage"); }

StorageInfo Client::parse_storage(const HttpResponse& response) const {
    require_success(response);
    const auto value = json::parse(body_text(response));
    return {value.at("usedBytes").get<uint64_t>(),
            value.at("availableBytes").get<uint64_t>(),
            value.at("quotaBytes").get<uint64_t>()};
}

HttpRequest Client::list_request(string_view path) const {
    return authorized(HttpMethod::get, "/api/v1/files?path=" + percent_encode(path));
}

vector<CloudFile> Client::parse_files(const HttpResponse& response) const {
    require_success(response);
    const auto document = json::parse(body_text(response));
    vector<CloudFile> result;
    for (const auto& item : document.at("entries")) {
        result.push_back({item.at("path").get<string>(),
                          item.at("sizeBytes").get<uint64_t>(),
                          item.at("directory").get<bool>()});
    }
    return result;
}

HttpRequest Client::search_request(string_view query) const {
    if (query.empty()) throw invalid_argument("Search query cannot be empty");
    return authorized(HttpMethod::get, "/api/v1/search?query=" + percent_encode(query));
}

HttpRequest Client::create_folder_request(string_view path) const {
    return authorized(HttpMethod::post, "/api/v1/directories?path=" + percent_encode(path));
}

HttpRequest Client::rename_request(string_view source, string_view new_name) const {
    if (new_name.empty()) throw invalid_argument("New name cannot be empty");
    return authorized(HttpMethod::post, "/api/v1/files/rename?source=" + percent_encode(source) +
                                        "&name=" + percent_encode(new_name));
}

HttpRequest Client::trash_request(string_view path) const {
    return authorized(HttpMethod::delete_, "/api/v1/files?path=" + percent_encode(path));
}

HttpRequest Client::list_trash_request() const { return authorized(HttpMethod::get, "/api/v1/trash"); }

vector<TrashItem> Client::parse_trash(const HttpResponse& response) const {
    require_success(response);
    const auto document = json::parse(body_text(response));
    vector<TrashItem> result;
    for (const auto& item : document.at("entries")) {
        result.push_back({item.at("id").get<string>(),
                          item.at("originalPath").get<string>(),
                          item.at("deletedAt").get<int64_t>()});
    }
    return result;
}

HttpRequest Client::restore_request(string_view id) const {
    return authorized(HttpMethod::post, "/api/v1/trash/restore?id=" + percent_encode(id));
}

HttpRequest Client::permanent_delete_request(string_view id) const {
    return authorized(HttpMethod::delete_, "/api/v1/trash?id=" + percent_encode(id));
}

HttpRequest Client::download_request(string_view path) const {
    return authorized(HttpMethod::get, "/api/v1/download?path=" + percent_encode(path));
}

HttpRequest Client::upload_request(string_view relative_path, vector<uint8_t> content) const {
    auto request = authorized(HttpMethod::post,
                              "/api/v1/upload?relativePath=" + percent_encode(relative_path));
    request.headers["Content-Type"] = "application/octet-stream";
    request.body = std::move(content);
    return request;
}

void Client::open_folder(string path) {
    if (path.empty() || path.starts_with('/') || path.find("..") != string::npos)
        throw invalid_argument("Invalid cloud folder path");
    current_path_ = std::move(path);
}

void Client::go_back() {
    if (current_path_ == ".") return;
    const auto separator = current_path_.find_last_of('/');
    current_path_ = separator == string::npos ? "." : current_path_.substr(0, separator);
}

string Client::percent_encode(string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    string result;
    for (const unsigned char character : value) {
        if (isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            result += static_cast<char>(character);
        } else {
            result += '%'; result += digits[character >> 4]; result += digits[character & 15];
        }
    }
    return result;
}

HttpRequest Client::authorized(HttpMethod method, string path) const {
    if (!authenticated()) throw logic_error("HomeCloud client is not authenticated");
    HttpRequest request{method, std::move(path), {}, {}};
    request.headers["Authorization"] = "Bearer " + token_;
    request.headers["Accept"] = "application/json";
    return request;
}

void Client::require_success(const HttpResponse& response) {
    if (response.status < 200 || response.status >= 300)
        throw runtime_error("HomeCloud API returned HTTP " + to_string(response.status));
}

} // namespace homecloud_client
