#include "homecloud/auth.hpp"
#include "homecloud/module_repository.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace std;

namespace {

class ConsoleEchoGuard {
public:
    ConsoleEchoGuard() {
#ifdef _WIN32
        input_ = GetStdHandle(STD_INPUT_HANDLE);
        if (input_ != INVALID_HANDLE_VALUE && GetConsoleMode(input_, &original_mode_)) {
            active_ = SetConsoleMode(input_, original_mode_ & ~ENABLE_ECHO_INPUT) != 0;
        }
#endif
    }

    ~ConsoleEchoGuard() {
#ifdef _WIN32
        if (active_) SetConsoleMode(input_, original_mode_);
#endif
    }

private:
#ifdef _WIN32
    HANDLE input_{INVALID_HANDLE_VALUE};
    DWORD original_mode_{};
#endif
    bool active_{};
};

string read_password(string_view prompt) {
    cout << prompt << flush;
    ConsoleEchoGuard guard;
    string password;
    getline(cin, password);
    cout << '\n';
    if (!cin) throw runtime_error("Could not read password");
    return password;
}

void usage() {
    cout << "Usage:\n"
         << "  homecloud_admin <storage-path> create-user <username>\n"
         << "  homecloud_admin <storage-path> list-users\n"
         << "  homecloud_admin <storage-path> publish-android <module-id> <version> <apk-path>\n";
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        if (argc < 3) {
            usage();
            return EXIT_FAILURE;
        }
        const filesystem::path root = filesystem::absolute(argv[1]).lexically_normal();
        homecloud::AuthService auth(root / ".homecloud" / "users.db");
        const string command = argv[2];

        if (command == "publish-android" && argc == 6) {
            homecloud::ModuleRepository modules(root / ".homecloud" / "modules");
            modules.publish_android(argv[3], argv[4], argv[5]);
            cout << "Android module published: " << argv[3] << " " << argv[4] << '\n';
            return EXIT_SUCCESS;
        }

        if (command == "list-users") {
            const auto users = auth.usernames();
            if (users.empty()) {
                cout << "No HomeCloud users configured.\n";
            } else {
                for (const auto& username : users) cout << username << '\n';
            }
            return EXIT_SUCCESS;
        }

        if (command == "create-user" && argc == 4) {
            const auto first = read_password("Password: ");
            const auto second = read_password("Repeat password: ");
            if (first != second) throw invalid_argument("Passwords do not match");
            auth.create_user(argv[3], first);
            cout << "HomeCloud user created: " << argv[3] << '\n';
            return EXIT_SUCCESS;
        }

        usage();
        return EXIT_FAILURE;
    } catch (const exception& error) {
        cerr << "HomeCloud administration error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
