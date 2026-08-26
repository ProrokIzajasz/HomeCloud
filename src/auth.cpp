#include "homecloud/auth.hpp"
#include "homecloud/crypto.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

using namespace std;

namespace homecloud {

AuthService::AuthService(std::filesystem::path database_path,
                         std::chrono::hours session_lifetime)
    : database_path_(std::move(database_path)), session_lifetime_(session_lifetime) {
    load_users();
}

void AuthService::validate_credentials(std::string_view username, std::string_view password) {
    const bool valid_username = username.size() >= 3 && username.size() <= 32 &&
        std::ranges::all_of(username, [](unsigned char character) {
            return std::isalnum(character) || character == '_' || character == '-';
        });
    if (!valid_username) {
        throw std::invalid_argument("Username must be 3-32 letters, digits, '_' or '-'");
    }
    if (password.size() < 12 || password.size() > 1024) {
        throw std::invalid_argument("Password must contain 12-1024 bytes");
    }
}

void AuthService::create_user(std::string_view username, std::string_view password) {
    validate_credentials(username, password);
    std::scoped_lock lock(mutex_);
    if (std::ranges::any_of(users_, [&](const User& user) { return user.username == username; })) {
        throw std::invalid_argument("User already exists");
    }
    User user;
    user.username = username;
    user.iterations = kPasswordIterations;
    user.salt = crypto::random_bytes(16);
    user.password_hash = crypto::pbkdf2_sha256(password, user.salt, user.iterations, 32);

    std::filesystem::create_directories(database_path_.parent_path());
    std::ofstream output(database_path_, std::ios::binary | std::ios::app);
    output << user.username << '\t' << user.iterations << '\t'
           << crypto::hex_encode(user.salt) << '\t'
           << crypto::hex_encode(user.password_hash) << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error("Could not persist user account");
    }
    users_.push_back(std::move(user));
}

std::optional<std::string> AuthService::login(std::string_view username,
                                              std::string_view password) {
    std::scoped_lock lock(mutex_);
    const auto user = std::ranges::find_if(users_, [&](const User& candidate) {
        return candidate.username == username;
    });
    if (user == users_.end()) {
        return std::nullopt;
    }
    const auto candidate = crypto::pbkdf2_sha256(password, user->salt,
                                                  user->iterations, user->password_hash.size());
    if (!crypto::constant_time_equal(candidate, user->password_hash)) {
        return std::nullopt;
    }
    const auto token = crypto::hex_encode(crypto::random_bytes(32));
    sessions_[token] = {user->username, std::chrono::system_clock::now() + session_lifetime_};
    return token;
}

std::optional<std::string> AuthService::authenticate(std::string_view bearer_token) {
    std::scoped_lock lock(mutex_);
    const auto session = sessions_.find(std::string(bearer_token));
    if (session == sessions_.end()) {
        return std::nullopt;
    }
    if (session->second.expires_at <= std::chrono::system_clock::now()) {
        sessions_.erase(session);
        return std::nullopt;
    }
    return session->second.username;
}

void AuthService::logout(std::string_view bearer_token) {
    std::scoped_lock lock(mutex_);
    sessions_.erase(std::string(bearer_token));
}

std::size_t AuthService::user_count() const {
    std::scoped_lock lock(mutex_);
    return users_.size();
}

vector<string> AuthService::usernames() const {
    scoped_lock lock(mutex_);
    vector<string> result;
    result.reserve(users_.size());
    for (const auto& user : users_) result.push_back(user.username);
    ranges::sort(result);
    return result;
}

void AuthService::load_users() {
    if (!std::filesystem::exists(database_path_)) {
        return;
    }
    std::ifstream input(database_path_, std::ios::binary);
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream fields(line);
        User user;
        std::string iterations;
        std::string salt;
        std::string hash;
        if (std::getline(fields, user.username, '\t') &&
            std::getline(fields, iterations, '\t') &&
            std::getline(fields, salt, '\t') &&
            std::getline(fields, hash)) {
            user.iterations = static_cast<std::uint32_t>(std::stoul(iterations));
            user.salt = crypto::hex_decode(salt);
            user.password_hash = crypto::hex_decode(hash);
            users_.push_back(std::move(user));
        }
    }
}

} // namespace homecloud
