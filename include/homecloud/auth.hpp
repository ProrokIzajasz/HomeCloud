#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace homecloud {

class AuthService {
public:
    static constexpr std::uint32_t kPasswordIterations = 600'000;

    explicit AuthService(std::filesystem::path database_path,
                         std::chrono::hours session_lifetime = std::chrono::hours(24));

    void create_user(std::string_view username, std::string_view password);
    [[nodiscard]] std::optional<std::string> login(
        std::string_view username, std::string_view password);
    [[nodiscard]] std::optional<std::string> authenticate(
        std::string_view bearer_token);
    void logout(std::string_view bearer_token);
    [[nodiscard]] std::size_t user_count() const;
    [[nodiscard]] std::vector<std::string> usernames() const;

private:
    struct User {
        std::string username;
        std::uint32_t iterations{};
        std::vector<std::uint8_t> salt;
        std::vector<std::uint8_t> password_hash;
    };

    struct Session {
        std::string username;
        std::chrono::system_clock::time_point expires_at;
    };

    std::filesystem::path database_path_;
    std::chrono::hours session_lifetime_;
    std::vector<User> users_;
    std::unordered_map<std::string, Session> sessions_;
    mutable std::mutex mutex_;

    void load_users();
    static void validate_credentials(std::string_view username, std::string_view password);
};

} // namespace homecloud
