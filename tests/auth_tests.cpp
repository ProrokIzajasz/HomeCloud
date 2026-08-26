#include "homecloud/auth.hpp"
#include "homecloud/crypto.hpp"

#include <filesystem>
#include <stdexcept>

using namespace std;

void run_auth_tests() {
    const vector<uint8_t> known_salt{'s', 'a', 'l', 't'};
    const auto known_hash = homecloud::crypto::pbkdf2_sha256(
        "password", known_salt, 1, 32);
    if (homecloud::crypto::hex_encode(known_hash) !=
        "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b")
        throw runtime_error("PBKDF2-HMAC-SHA256 compatibility vector failed");

    const auto root = std::filesystem::temp_directory_path() / "homecloud-auth-tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    const auto database = root / "users.db";

    homecloud::AuthService auth(database);
    auth.create_user("bart", "correct horse battery staple");
    auth.create_user("alice", "another secure test password");
    if (auth.user_count() != 2) throw std::runtime_error("Users were not created");
    const auto names = auth.usernames();
    if (names.size() != 2 || names[0] != "alice" || names[1] != "bart")
        throw std::runtime_error("Usernames were not listed in sorted order");
    if (auth.login("bart", "wrong password!")) throw std::runtime_error("Wrong password accepted");
    const auto token = auth.login("bart", "correct horse battery staple");
    if (!token || token->size() != 64) throw std::runtime_error("Login did not create token");
    if (auth.authenticate(*token) != std::optional<std::string>("bart"))
        throw std::runtime_error("Valid token rejected");
    auth.logout(*token);
    if (auth.authenticate(*token)) throw std::runtime_error("Logged-out token accepted");

    homecloud::AuthService reloaded(database);
    if (!reloaded.login("bart", "correct horse battery staple"))
        throw std::runtime_error("Persisted account could not log in");
    std::filesystem::remove_all(root);
}
