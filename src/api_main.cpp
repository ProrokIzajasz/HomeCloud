#include "homecloud/auth.hpp"
#include "homecloud/crypto.hpp"
#include "homecloud/image_variant.hpp"
#include "homecloud/platform.hpp"
#include "homecloud/storage.hpp"
#include "httplib.h"

#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cctype>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <unordered_map>

using namespace std;

namespace {

class LoginRateLimiter {
public:
    bool allow(const string& address) {
        scoped_lock lock(mutex_);
        auto& attempts = attempts_[address];
        const auto cutoff = chrono::steady_clock::now() - chrono::minutes(5);
        while (!attempts.empty() && attempts.front() < cutoff) attempts.pop_front();
        return attempts.size() < 5;
    }

    void failure(const string& address) {
        scoped_lock lock(mutex_);
        attempts_[address].push_back(chrono::steady_clock::now());
    }

    void success(const string& address) {
        scoped_lock lock(mutex_);
        attempts_.erase(address);
    }

private:
    mutex mutex_;
    unordered_map<string, deque<chrono::steady_clock::time_point>> attempts_;
};

struct DownloadTicket {
    filesystem::path file;
    chrono::steady_clock::time_point expires_at;
};

std::string json_escape(std::string_view value) {
    std::string output;
    for (const unsigned char character : value) {
        switch (character) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20) {
                constexpr char digits[] = "0123456789abcdef";
                output += "\\u00";
                output += digits[character >> 4];
                output += digits[character & 0x0f];
            } else {
                output += static_cast<char>(character);
            }
        }
    }
    return output;
}

void json(httplib::Response& response, int status, std::string body) {
    response.status = status;
    response.set_content(std::move(body), "application/json; charset=utf-8");
    response.set_header("Cache-Control", "no-store");
    response.set_header("X-Content-Type-Options", "nosniff");
}

std::optional<std::string> bearer(const httplib::Request& request) {
    const auto header = request.get_header_value("Authorization");
    constexpr std::string_view prefix = "Bearer ";
    if (!header.starts_with(prefix)) return std::nullopt;
    return header.substr(prefix.size());
}

std::optional<std::string> authorized_user(const httplib::Request& request,
                                           homecloud::AuthService& auth) {
    const auto token = bearer(request);
    return token ? auth.authenticate(*token) : std::nullopt;
}

string entries_json(const vector<homecloud::FileEntry>& entries) {
    ostringstream body;
    body << "{\"entries\":[";
    for (size_t index = 0; index < entries.size(); ++index) {
        if (index != 0) body << ',';
        body << "{\"path\":\"" << json_escape(entries[index].relative_path.generic_string())
             << "\",\"sizeBytes\":" << entries[index].size_bytes
             << ",\"directory\":" << (entries[index].is_directory ? "true" : "false")
             << '}';
    }
    body << "]}";
    return body.str();
}

optional<string> preview_content_type(const filesystem::path& path) {
    auto extension = path.extension().string();
    ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(tolower(character));
    });
    static const unordered_map<string, string> types{
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".png", "image/png"},
        {".gif", "image/gif"}, {".webp", "image/webp"}, {".bmp", "image/bmp"},
        {".pdf", "application/pdf"}, {".mp4", "video/mp4"},
        {".webm", "video/webm"}, {".mov", "video/quicktime"}
    };
    const auto found = types.find(extension);
    return found == types.end() ? nullopt : optional<string>(found->second);
}

string download_content_type(const filesystem::path& path) {
    auto extension = path.extension().string();
    ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(tolower(character));
    });
    static const unordered_map<string, string> types{
        {".zip", "application/zip"}, {".7z", "application/x-7z-compressed"},
        {".rar", "application/vnd.rar"}, {".pdf", "application/pdf"},
        {".txt", "text/plain"}, {".csv", "text/csv"},
        {".jpg", "image/jpeg"}, {".jpeg", "image/jpeg"}, {".png", "image/png"},
        {".gif", "image/gif"}, {".webp", "image/webp"},
        {".mp4", "video/mp4"}, {".webm", "video/webm"}, {".mov", "video/quicktime"},
        {".doc", "application/msword"},
        {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
        {".xls", "application/vnd.ms-excel"},
        {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
        {".ppt", "application/vnd.ms-powerpoint"},
        {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"}
    };
    const auto found = types.find(extension);
    return found == types.end() ? "application/octet-stream" : found->second;
}

string percent_encode_header(string_view value) {
    constexpr char digits[] = "0123456789ABCDEF";
    string encoded;
    for (const unsigned char character : value) {
        if (isalnum(character) || character == '.' || character == '-' || character == '_') {
            encoded += static_cast<char>(character);
        } else {
            encoded += '%';
            encoded += digits[character >> 4];
            encoded += digits[character & 0x0f];
        }
    }
    return encoded;
}

bool scalable_image(const filesystem::path& path) {
    auto extension = path.extension().string();
    ranges::transform(extension, extension.begin(), [](unsigned char character) {
        return static_cast<char>(tolower(character));
    });
    return extension == ".jpg" || extension == ".jpeg" || extension == ".png" ||
           extension == ".bmp" || extension == ".gif";
}

uint64_t fnv1a(string_view value) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char character : value) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

optional<filesystem::path> cached_variant(const filesystem::path& source,
                                          const filesystem::path& cache_root,
                                          string_view kind,
                                          uint32_t width,
                                          uint32_t height,
                                          float quality) {
    error_code ignored;
    const auto size = filesystem::file_size(source, ignored);
    if (ignored) return nullopt;
    const auto modified = filesystem::last_write_time(source, ignored);
    if (ignored) return nullopt;
    const string identity = source.generic_string() + '|' + to_string(size) + '|' +
                            to_string(modified.time_since_epoch().count()) + '|' +
                            string(kind) + '|' + to_string(width) + 'x' + to_string(height);
    ostringstream name;
    name << hex << fnv1a(identity) << ".jpg";
    const auto target = cache_root / string(kind) / name.str();
    if (filesystem::is_regular_file(target, ignored)) return target;
    filesystem::create_directories(target.parent_path());
    const auto image = homecloud::create_jpeg_variant(source, width, height, quality);
    if (!image || image->bytes.empty()) return nullopt;
    const auto temporary = target.string() + ".tmp";
    {
        ofstream output(temporary, ios::binary | ios::trunc);
        output.write(reinterpret_cast<const char*>(image->bytes.data()),
                     static_cast<streamsize>(image->bytes.size()));
        if (!output) return nullopt;
    }
    filesystem::rename(temporary, target, ignored);
    if (ignored && !filesystem::is_regular_file(target)) {
        filesystem::remove(temporary);
        return nullopt;
    }
    return target;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const filesystem::path root = argc > 1 ? argv[1] : homecloud::default_storage_path();
        const filesystem::path web_root = argc > 2
            ? filesystem::absolute(argv[2]).lexically_normal()
            : (filesystem::current_path() / "web").lexically_normal();
        homecloud::Storage storage(root);
        storage.initialize();
        const auto preview_cache = root / ".homecloud" / "previews";
        const auto expired_trash_removed = storage.purge_expired_trash();
        homecloud::AuthService auth(root / ".homecloud" / "users.db");
        LoginRateLimiter login_limiter;
        mutex upload_mutex;
        mutex preview_mutex;
        mutex download_ticket_mutex;
        unordered_map<string, DownloadTicket> download_tickets;

        if (expired_trash_removed != 0) {
            cout << "Removed " << expired_trash_removed << " expired trash entries\n";
        }

        if (const char* username = std::getenv("HOMECLOUD_BOOTSTRAP_USER")) {
            if (const char* password = std::getenv("HOMECLOUD_BOOTSTRAP_PASSWORD");
                password && auth.user_count() == 0) {
                auth.create_user(username, password);
                std::cout << "Initial HomeCloud user created\n";
            }
        }

        httplib::Server server;
        server.set_payload_max_length(homecloud::kDefaultQuotaBytes);

        if (!filesystem::is_regular_file(web_root / "index.html") ||
            !server.set_mount_point("/assets", web_root.string())) {
            throw runtime_error("HomeCloud web assets were not found at " + web_root.string());
        }

        server.Get("/", [web_root](const auto&, auto& response) {
            response.set_file_content((web_root / "index.html").string(),
                                      "text/html; charset=utf-8");
            response.set_header("Cache-Control", "no-store");
            response.set_header("X-Content-Type-Options", "nosniff");
            response.set_header("Content-Security-Policy",
                                "default-src 'self'; img-src 'self' blob: data:; "
                                "media-src 'self' blob:; connect-src 'self'; "
                                "style-src 'self'; script-src 'self'; base-uri 'none'; "
                                "frame-ancestors 'none'");
        });

        server.Get("/api/v1/health", [](const auto&, auto& response) {
            json(response, 200, R"({"status":"ok"})");
        });

        server.Post("/api/v1/login", [&](const auto& request, auto& response) {
            if (!login_limiter.allow(request.remote_addr)) {
                response.set_header("Retry-After", "300");
                json(response, 429, R"({"error":"too_many_login_attempts"})");
                return;
            }
            if (!request.has_param("username") || !request.has_param("password")) {
                json(response, 400, R"({"error":"username_and_password_required"})");
                return;
            }
            const auto token = auth.login(request.get_param_value("username"),
                                          request.get_param_value("password"));
            if (!token) {
                login_limiter.failure(request.remote_addr);
                json(response, 401, R"({"error":"invalid_credentials"})");
                return;
            }
            login_limiter.success(request.remote_addr);
            json(response, 200, "{\"token\":\"" + *token + "\"}");
        });

        server.Post("/api/v1/logout", [&](const auto& request, auto& response) {
            const auto token = bearer(request);
            if (!token || !auth.authenticate(*token)) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            auth.logout(*token);
            json(response, 200, R"({"status":"logged_out"})");
        });

        server.Get("/api/v1/storage", [&](const auto& request, auto& response) {
            const auto user = authorized_user(request, auth);
            if (!user) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            const auto status = storage.status();
            json(response, 200,
                 "{\"usedBytes\":" + std::to_string(status.used_bytes) +
                 ",\"availableBytes\":" + std::to_string(status.available_bytes()) +
                 ",\"quotaBytes\":" + std::to_string(status.quota_bytes) + "}");
        });

        server.Get("/api/v1/files", [&](const auto& request, auto& response) {
            const auto user = authorized_user(request, auth);
            if (!user) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            try {
                const auto path = request.has_param("path")
                    ? request.get_param_value("path") : ".";
                const auto entries = storage.list(path);
                json(response, 200, entries_json(entries));
            } catch (const std::exception& error) {
                json(response, 400, "{\"error\":\"invalid_path\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Get("/api/v1/files/manifest", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            try {
                const auto path = request.has_param("path")
                    ? request.get_param_value("path") : ".";
                json(response, 200, entries_json(storage.list_recursive(path)));
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"manifest_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Post("/api/v1/directories", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            if (!request.has_param("path")) {
                json(response, 400, R"({"error":"path_required"})");
                return;
            }
            try {
                storage.create_directory(request.get_param_value("path"));
                json(response, 201, R"({"status":"created"})");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"directory_creation_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Get("/api/v1/download", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            if (!request.has_param("path")) {
                json(response, 400, R"({"error":"path_required"})");
                return;
            }
            try {
                const auto file = storage.resolve_user_path(request.get_param_value("path"));
                if (!filesystem::is_regular_file(file)) {
                    json(response, 404, R"({"error":"file_not_found"})");
                    return;
                }
                response.set_header("Cache-Control", "no-store");
                response.set_header("X-Content-Type-Options", "nosniff");
                response.set_file_content(file.string(), "application/octet-stream");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"invalid_path\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Post("/api/v1/download-ticket", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            if (!request.has_param("path")) {
                json(response, 400, R"({"error":"path_required"})");
                return;
            }
            try {
                const auto file = storage.resolve_user_path(request.get_param_value("path"));
                if (!filesystem::is_regular_file(file)) {
                    json(response, 404, R"({"error":"file_not_found"})");
                    return;
                }
                const auto ticket = homecloud::crypto::hex_encode(
                    homecloud::crypto::random_bytes(24));
                {
                    scoped_lock lock(download_ticket_mutex);
                    const auto now = chrono::steady_clock::now();
                    erase_if(download_tickets, [&](const auto& item) {
                        return item.second.expires_at <= now;
                    });
                    download_tickets.emplace(ticket, DownloadTicket{
                        file, now + chrono::minutes(5)});
                }
                json(response, 201, "{\"url\":\"/api/v1/download-file?ticket=" +
                                    ticket + "\"}");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"download_ticket_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Get("/api/v1/download-file", [&](const auto& request, auto& response) {
            if (!request.has_param("ticket")) {
                json(response, 400, R"({"error":"ticket_required"})");
                return;
            }
            optional<filesystem::path> file;
            {
                scoped_lock lock(download_ticket_mutex);
                const auto found = download_tickets.find(request.get_param_value("ticket"));
                if (found != download_tickets.end() &&
                    found->second.expires_at > chrono::steady_clock::now()) {
                    file = found->second.file;
                }
            }
            if (!file || !filesystem::is_regular_file(*file)) {
                json(response, 404, R"({"error":"download_expired"})");
                return;
            }
            response.set_header("Cache-Control", "private, no-store");
            response.set_header("X-Content-Type-Options", "nosniff");
            const string filename = file->filename().generic_string();
            string fallback = "HomeCloud" + file->extension().generic_string();
            if (ranges::all_of(filename, [](unsigned char value) {
                    return value >= 32 && value < 127 && value != '"' && value != '\\';
                })) {
                fallback = filename;
            }
            response.set_header("Content-Disposition",
                                "attachment; filename=\"" + fallback +
                                "\"; filename*=UTF-8''" + percent_encode_header(filename));
            response.set_file_content(file->string(), download_content_type(*file));
        });

        server.Get("/api/v1/thumbnail", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("path")) {
                json(response, 400, R"({"error":"path_required"})"); return;
            }
            try {
                const auto file = storage.resolve_user_path(request.get_param_value("path"));
                if (!filesystem::is_regular_file(file)) {
                    json(response, 404, R"({"error":"file_not_found"})"); return;
                }
                if (!scalable_image(file)) {
                    json(response, 415, R"({"error":"thumbnail_not_supported"})"); return;
                }
                optional<filesystem::path> thumbnail;
                {
                    scoped_lock lock(preview_mutex);
                    thumbnail = cached_variant(file, preview_cache, "thumb", 320, 320, 0.74F);
                }
                if (!thumbnail) {
                    json(response, 500, R"({"error":"thumbnail_generation_failed"})"); return;
                }
                response.set_header("Cache-Control", "private, max-age=1800");
                response.set_header("X-Content-Type-Options", "nosniff");
                response.set_file_content(thumbnail->string(), "image/jpeg");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"thumbnail_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Get("/api/v1/preview", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("path")) {
                json(response, 400, R"({"error":"path_required"})"); return;
            }
            try {
                const auto file = storage.resolve_user_path(request.get_param_value("path"));
                if (!filesystem::is_regular_file(file)) {
                    json(response, 404, R"({"error":"file_not_found"})"); return;
                }
                const auto content_type = preview_content_type(file);
                if (!content_type) {
                    json(response, 415, R"({"error":"preview_not_supported"})"); return;
                }
                if (scalable_image(file)) {
                    optional<filesystem::path> preview;
                    {
                        scoped_lock lock(preview_mutex);
                        preview = cached_variant(file, preview_cache, "screen", 1920, 1920, 0.86F);
                    }
                    if (preview) {
                        response.set_header("Cache-Control", "private, max-age=1800");
                        response.set_header("X-Content-Type-Options", "nosniff");
                        response.set_header("Content-Disposition", "inline");
                        response.set_file_content(preview->string(), "image/jpeg");
                        return;
                    }
                }
                response.set_header("Cache-Control", "private, max-age=300");
                response.set_header("X-Content-Type-Options", "nosniff");
                response.set_header("Content-Disposition", "inline");
                response.set_file_content(file.string(), *content_type);
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"preview_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Post("/api/v1/upload", [&](const auto& request, auto& response,
                                           const httplib::ContentReader& reader) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            if (!request.has_param("filename") && !request.has_param("relativePath")) {
                json(response, 400, R"({"error":"filename_or_relative_path_required"})");
                return;
            }
            if (!request.has_header("Content-Length")) {
                json(response, 411, R"({"error":"content_length_required"})");
                return;
            }

            filesystem::path staged;
            try {
                const auto expected = stoull(request.get_header_value("Content-Length"));
                if (!storage.can_store(expected)) {
                    json(response, 413, R"({"error":"storage_quota_exceeded"})");
                    return;
                }
                const auto upload_id = homecloud::crypto::hex_encode(
                    homecloud::crypto::random_bytes(16));
                staged = storage.create_upload_staging_path(upload_id);
                ofstream output(staged, ios::binary | ios::trunc);
                uintmax_t received = 0;
                const bool read_ok = reader([&](const char* data, size_t length) {
                    if (received > expected || length > expected - received) return false;
                    output.write(data, static_cast<streamsize>(length));
                    received += length;
                    return static_cast<bool>(output);
                });
                output.close();
                if (!read_ok || received != expected || !output) {
                    filesystem::remove(staged);
                    json(response, 400, R"({"error":"incomplete_upload"})");
                    return;
                }
                string filename;
                string destination;
                if (request.has_param("relativePath")) {
                    const auto target = storage.resolve_user_path(
                        request.get_param_value("relativePath"));
                    filename = target.filename().generic_string();
                    const auto parent = target.parent_path().lexically_relative(storage.root());
                    destination = parent.empty() ? "." : parent.generic_string();
                    if (destination != ".") storage.create_directory(destination);
                } else {
                    filename = request.get_param_value("filename");
                    destination = request.has_param("destination")
                        ? request.get_param_value("destination") : ".";
                }
                filesystem::path stored;
                {
                    scoped_lock upload_lock(upload_mutex);
                    stored = storage.commit_staged_upload(staged, filename, destination);
                }
                json(response, 201, "{\"path\":\"" +
                                    json_escape(stored.generic_string()) + "\"}");
            } catch (const exception& error) {
                if (!staged.empty()) {
                    error_code ignored;
                    filesystem::remove(staged, ignored);
                }
                json(response, 400, "{\"error\":\"upload_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        // Resumable uploads use small requests so clients can retry only the
        // interrupted part. Different files may be streamed concurrently;
        // only the final rename is serialized by upload_mutex.
        server.Get("/api/v1/uploads/status", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            if (!request.has_param("uploadId")) {
                json(response, 400, R"({"error":"upload_id_required"})");
                return;
            }
            try {
                const auto upload_id = request.get_param_value("uploadId");
                const auto staged = storage.create_upload_staging_path(upload_id);
                const auto completed = storage.create_upload_staging_path(upload_id + ".done");
                uintmax_t offset = 0;
                if (filesystem::is_regular_file(completed)) {
                    ifstream marker(completed);
                    marker >> offset;
                } else if (filesystem::is_regular_file(staged)) {
                    offset = filesystem::file_size(staged);
                }
                json(response, 200, "{\"offset\":" + to_string(offset) + "}");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"invalid_upload\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Post("/api/v1/uploads/chunk", [&](const auto& request, auto& response,
                                                  const httplib::ContentReader& reader) {
            constexpr uintmax_t maximum_chunk_size = 8U * 1024U * 1024U;
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})");
                return;
            }
            if (!request.has_param("uploadId") || !request.has_param("offset") ||
                !request.has_param("total") || !request.has_param("relativePath") ||
                !request.has_header("Content-Length")) {
                json(response, 400, R"({"error":"incomplete_chunk_metadata"})");
                return;
            }

            filesystem::path staged;
            try {
                const auto upload_id = request.get_param_value("uploadId");
                const auto offset = stoull(request.get_param_value("offset"));
                const auto total = stoull(request.get_param_value("total"));
                const auto chunk_size = stoull(request.get_header_value("Content-Length"));
                if (total == 0 || offset > total || chunk_size > maximum_chunk_size ||
                    chunk_size > total - offset) {
                    json(response, 400, R"({"error":"invalid_chunk_range"})");
                    return;
                }

                staged = storage.create_upload_staging_path(upload_id);
                const auto completed = storage.create_upload_staging_path(upload_id + ".done");
                if (filesystem::is_regular_file(completed)) {
                    uintmax_t completed_size = 0;
                    ifstream marker(completed);
                    marker >> completed_size;
                    json(response, 200, "{\"offset\":" + to_string(completed_size) + "}");
                    return;
                }
                const auto stored_offset = filesystem::is_regular_file(staged)
                    ? filesystem::file_size(staged) : uintmax_t{0};
                if (stored_offset != offset) {
                    json(response, 409, "{\"error\":\"offset_mismatch\",\"offset\":" +
                                        to_string(stored_offset) + "}");
                    return;
                }
                if (offset == 0 && !storage.can_store(total)) {
                    json(response, 413, R"({"error":"storage_quota_exceeded"})");
                    return;
                }

                ofstream output(staged, ios::binary | ios::app);
                uintmax_t received = 0;
                const bool read_ok = reader([&](const char* data, size_t length) {
                    if (received > chunk_size || length > chunk_size - received) return false;
                    output.write(data, static_cast<streamsize>(length));
                    received += length;
                    return static_cast<bool>(output);
                });
                output.close();
                if (!read_ok || received != chunk_size || !output) {
                    error_code ignored;
                    filesystem::resize_file(staged, offset, ignored);
                    json(response, 400, R"({"error":"incomplete_chunk"})");
                    return;
                }

                const auto next_offset = offset + received;
                if (next_offset < total) {
                    json(response, 200, "{\"offset\":" + to_string(next_offset) + "}");
                    return;
                }

                const auto target = storage.resolve_user_path(
                    request.get_param_value("relativePath"));
                const auto filename = target.filename().generic_string();
                const auto parent = target.parent_path().lexically_relative(storage.root());
                const auto destination = parent.empty() ? string{"."} : parent.generic_string();
                filesystem::path stored;
                {
                    scoped_lock upload_lock(upload_mutex);
                    if (destination != ".") storage.create_directory(destination);
                    stored = storage.commit_staged_upload(staged, filename, destination);
                }
                {
                    ofstream marker(completed, ios::trunc);
                    marker << next_offset;
                }
                json(response, 201, "{\"offset\":" + to_string(next_offset) +
                                    ",\"path\":\"" +
                                    json_escape(stored.generic_string()) + "\"}");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"chunk_upload_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Get("/api/v1/search", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("query")) {
                json(response, 400, R"({"error":"query_required"})"); return;
            }
            try {
                json(response, 200, entries_json(storage.search(request.get_param_value("query"))));
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"search_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Post("/api/v1/files/copy", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("source") || !request.has_param("destination")) {
                json(response, 400, R"({"error":"source_and_destination_required"})"); return;
            }
            try {
                const auto path = storage.copy(request.get_param_value("source"),
                                               request.get_param_value("destination"));
                json(response, 201, "{\"path\":\"" + json_escape(path.generic_string()) + "\"}");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"copy_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Post("/api/v1/files/move", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("source") || !request.has_param("destination")) {
                json(response, 400, R"({"error":"source_and_destination_required"})"); return;
            }
            try {
                const auto path = storage.move(request.get_param_value("source"),
                                               request.get_param_value("destination"));
                json(response, 200, "{\"path\":\"" + json_escape(path.generic_string()) + "\"}");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"move_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Post("/api/v1/files/rename", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("source") || !request.has_param("name")) {
                json(response, 400, R"({"error":"source_and_name_required"})"); return;
            }
            try {
                const auto path = storage.rename(request.get_param_value("source"),
                                                 request.get_param_value("name"));
                json(response, 200, "{\"path\":\"" + json_escape(path.generic_string()) + "\"}");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"rename_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Delete("/api/v1/files", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("path")) {
                json(response, 400, R"({"error":"path_required"})"); return;
            }
            try {
                const auto id = storage.move_to_trash(request.get_param_value("path"));
                json(response, 200, "{\"trashId\":\"" + json_escape(id) + "\"}");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"delete_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Get("/api/v1/trash", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            const auto entries = storage.list_trash();
            ostringstream body;
            body << "{\"entries\":[";
            for (size_t index = 0; index < entries.size(); ++index) {
                if (index != 0) body << ',';
                body << "{\"id\":\"" << json_escape(entries[index].id)
                     << "\",\"originalPath\":\""
                     << json_escape(entries[index].original_path.generic_string())
                     << "\",\"deletedAt\":" << entries[index].deleted_at_epoch_seconds << '}';
            }
            body << "]}";
            json(response, 200, body.str());
        });

        server.Post("/api/v1/trash/restore", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("id")) {
                json(response, 400, R"({"error":"id_required"})"); return;
            }
            try {
                const auto path = storage.restore_from_trash(request.get_param_value("id"));
                json(response, 200, "{\"path\":\"" + json_escape(path.generic_string()) + "\"}");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"restore_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Delete("/api/v1/trash", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            if (!request.has_param("id")) {
                json(response, 400, R"({"error":"id_required"})"); return;
            }
            try {
                storage.permanently_delete_from_trash(request.get_param_value("id"));
                json(response, 200, R"({"status":"permanently_deleted"})");
            } catch (const exception& error) {
                json(response, 400, "{\"error\":\"permanent_delete_failed\",\"message\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        });

        server.Delete("/api/v1/trash/all", [&](const auto& request, auto& response) {
            if (!authorized_user(request, auth)) {
                json(response, 401, R"({"error":"unauthorized"})"); return;
            }
            storage.empty_trash();
            json(response, 200, R"({"status":"trash_emptied"})");
        });

        server.set_error_handler([](const auto&, auto& response) {
            if (response.status == 404) json(response, 404, R"({"error":"not_found"})");
        });

        std::cout << "HomeCloud API listening on http://127.0.0.1:8080\n";
        std::cout << "Network exposure remains disabled until encrypted remote access is configured.\n";
        if (!server.listen("127.0.0.1", 8080)) {
            throw std::runtime_error("Could not listen on 127.0.0.1:8080");
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "HomeCloud API error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
