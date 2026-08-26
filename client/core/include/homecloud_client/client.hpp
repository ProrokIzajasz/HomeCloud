#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace homecloud_client {

enum class HttpMethod { get, post, delete_ };

struct HttpRequest {
    HttpMethod method{HttpMethod::get};
    std::string path;
    std::map<std::string, std::string> headers;
    std::vector<std::uint8_t> body;
};

struct HttpResponse {
    int status{};
    std::map<std::string, std::string> headers;
    std::vector<std::uint8_t> body;
};

struct CloudFile {
    std::string path;
    std::uint64_t size_bytes{};
    bool directory{};
};

struct StorageInfo {
    std::uint64_t used_bytes{};
    std::uint64_t available_bytes{};
    std::uint64_t quota_bytes{};
};

struct TrashItem {
    std::string id;
    std::string original_path;
    std::int64_t deleted_at_epoch_seconds{};
};

class Client {
public:
    [[nodiscard]] bool authenticated() const noexcept;
    [[nodiscard]] const std::string& username() const noexcept;
    [[nodiscard]] const std::string& current_path() const noexcept;

    [[nodiscard]] HttpRequest login_request(std::string_view username,
                                            std::string_view password) const;
    void accept_login(std::string_view username, const HttpResponse& response);
    [[nodiscard]] HttpRequest logout_request() const;
    void clear_session();

    [[nodiscard]] HttpRequest storage_request() const;
    [[nodiscard]] StorageInfo parse_storage(const HttpResponse& response) const;
    [[nodiscard]] HttpRequest list_request(std::string_view path = ".") const;
    [[nodiscard]] std::vector<CloudFile> parse_files(const HttpResponse& response) const;
    [[nodiscard]] HttpRequest search_request(std::string_view query) const;
    [[nodiscard]] HttpRequest create_folder_request(std::string_view path) const;
    [[nodiscard]] HttpRequest rename_request(std::string_view source,
                                             std::string_view new_name) const;
    [[nodiscard]] HttpRequest trash_request(std::string_view path) const;
    [[nodiscard]] HttpRequest list_trash_request() const;
    [[nodiscard]] std::vector<TrashItem> parse_trash(const HttpResponse& response) const;
    [[nodiscard]] HttpRequest restore_request(std::string_view id) const;
    [[nodiscard]] HttpRequest permanent_delete_request(std::string_view id) const;
    [[nodiscard]] HttpRequest download_request(std::string_view path) const;
    [[nodiscard]] HttpRequest upload_request(std::string_view relative_path,
                                             std::vector<std::uint8_t> bytes) const;

    void open_folder(std::string path);
    void go_back();

    [[nodiscard]] static std::string percent_encode(std::string_view value);

private:
    [[nodiscard]] HttpRequest authorized(HttpMethod method, std::string path) const;
    static void require_success(const HttpResponse& response);

    std::string token_;
    std::string username_;
    std::string current_path_{"."};
};

} // namespace homecloud_client
