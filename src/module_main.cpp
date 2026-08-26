#include "homecloud/module_repository.hpp"
#include "httplib.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace std;

namespace {

void secure_headers(httplib::Response& response) {
    response.set_header("X-Content-Type-Options", "nosniff");
    response.set_header("X-Frame-Options", "DENY");
    response.set_header("Referrer-Policy", "no-referrer");
    response.set_header("Content-Security-Policy", "default-src 'none'; frame-ancestors 'none'");
}

void json(httplib::Response& response, int status, string body) {
    response.status = status;
    response.set_content(std::move(body), "application/json; charset=utf-8");
    response.set_header("Cache-Control", status == 200 ? "public, max-age=60" : "no-store");
    secure_headers(response);
}

bool valid_port(string_view value) {
    if (value.empty() || value.size() > 5) return false;
    try {
        const auto port = stoi(string(value));
        return port >= 1024 && port <= 65535;
    } catch (...) { return false; }
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const filesystem::path storage_root = argc > 1
            ? filesystem::absolute(argv[1]).lexically_normal()
            : filesystem::path("/srv/homecloud/data");
        const string bind_address = argc > 2 ? argv[2] : "127.0.0.1";
        const string port_text = argc > 3 ? argv[3] : "8081";
        if (bind_address != "127.0.0.1" && bind_address != "0.0.0.0" && bind_address != "::")
            throw invalid_argument("Bind address must be 127.0.0.1, 0.0.0.0, or ::");
        if (!valid_port(port_text)) throw invalid_argument("Port must be between 1024 and 65535");
        const int port = stoi(port_text);

        homecloud::ModuleRepository modules(storage_root / ".homecloud" / "modules");
        modules.initialize();
        httplib::Server server;
        server.set_payload_max_length(1024);

        server.Get("/api/hiphop/health", [](const auto&, auto& response) {
            json(response, 200, R"({"status":"ok","service":"hiphop-modules"})");
        });
        server.Get("/api/hiphop/modules", [&](const auto&, auto& response) {
            json(response, 200, modules.catalog_json());
        });
        server.Get(R"(/api/hiphop/modules/([a-z0-9-]+)/([A-Za-z0-9.-]+)/download)",
                   [&](const httplib::Request& request, httplib::Response& response) {
            const auto platform = request.has_param("platform")
                ? request.get_param_value("platform") : "";
            const auto file = modules.resolve_download(request.matches[1].str(),
                                                       request.matches[2].str(), platform);
            if (!file) {
                json(response, 404, R"({"error":"module_release_not_found"})");
                return;
            }
            secure_headers(response);
            response.set_header("Cache-Control", "public, max-age=31536000, immutable");
            response.set_header("Content-Disposition", "attachment; filename=\"" +
                                file->filename().string() + "\"");
            response.set_file_content(file->string(), "application/vnd.android.package-archive");
        });
        server.set_error_handler([](const auto&, auto& response) {
            if (response.status == 404) json(response, 404, R"({"error":"not_found"})");
        });

        cout << "HipHop module service listening on http://" << bind_address << ':' << port << '\n';
        cout << "Only the read-only module catalog and immutable APK downloads are exposed.\n";
        if (!server.listen(bind_address, port)) throw runtime_error("Could not start module service");
        return EXIT_SUCCESS;
    } catch (const exception& error) {
        cerr << "HomeCloud module service error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
