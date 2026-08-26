#include "homecloud_client/client.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace std;
using namespace homecloud_client;

namespace {

void require(bool condition, string_view message) {
    if (!condition) throw runtime_error(string(message));
}

HttpResponse response(int status, string_view body) {
    return {status, {}, vector<uint8_t>(body.begin(), body.end())};
}

} // namespace

int main() {
    try {
        Client client;
        const auto login = client.login_request("Paka", "a password & symbols");
        require(login.method == HttpMethod::post, "Login method is incorrect");
        require(string(login.body.begin(), login.body.end()) ==
                    "username=Paka&password=a%20password%20%26%20symbols",
                "Login form encoding is incorrect");

        client.accept_login("Paka", response(200,
            R"({"token":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"})"));
        require(client.authenticated() && client.username() == "Paka", "Login state is incorrect");
        require(client.storage_request().headers.contains("Authorization"),
                "Bearer token is missing");

        const auto storage = client.parse_storage(response(200,
            R"({"usedBytes":10,"availableBytes":90,"quotaBytes":100})"));
        require(storage.used_bytes == 10 && storage.available_bytes == 90,
                "Storage response parsing failed");

        const auto files = client.parse_files(response(200,
            R"({"entries":[{"path":"Zdjecia","sizeBytes":0,"directory":true},{"path":"a.txt","sizeBytes":7,"directory":false}]})"));
        require(files.size() == 2 && files[0].directory && files[1].size_bytes == 7,
                "File response parsing failed");

        client.open_folder("Zdjecia/2026");
        client.go_back();
        require(client.current_path() == "Zdjecia", "Parent navigation failed");
        require(client.upload_request("Zdjecia/a b.jpg", {1, 2, 3}).path ==
                    "/api/v1/upload?relativePath=Zdjecia%2Fa%20b.jpg",
                "Upload path encoding failed");

        client.clear_session();
        require(!client.authenticated() && client.current_path() == ".",
                "Session clearing failed");
        cout << "All HomeCloud client core tests passed\n";
        return EXIT_SUCCESS;
    } catch (const exception& error) {
        cerr << "Client test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
