#pragma once

#include "homecloud_client/client.hpp"

#include <string>

namespace homecloud_windows {

class WinHttpTransport {
public:
    explicit WinHttpTransport(std::wstring base_url);
    [[nodiscard]] homecloud_client::HttpResponse send(
        const homecloud_client::HttpRequest& request) const;

private:
    std::wstring host_;
    std::wstring base_path_;
    unsigned short port_{};
    bool secure_{};
};

} // namespace homecloud_windows
