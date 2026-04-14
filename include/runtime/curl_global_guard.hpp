#pragma once

#include <curl/curl.h>
#include <stdexcept>
#include <string>

class CurlGlobalGuard {
  public:
    CurlGlobalGuard() {
        const auto rc = curl_global_init(CURL_GLOBAL_DEFAULT);
        if (rc != CURLE_OK) {
            throw std::runtime_error(
                std::string("curl_global_init failed: ") + curl_easy_strerror(rc));
        };
    }

    ~CurlGlobalGuard() {
        curl_global_cleanup();
    }

    // Disable copy/move, guard owns process-wide curl lifecycle
    CurlGlobalGuard(const CurlGlobalGuard&) = delete;
    CurlGlobalGuard& operator=(const CurlGlobalGuard&) = delete;
    CurlGlobalGuard(CurlGlobalGuard&&) = delete;
    CurlGlobalGuard& operator=(CurlGlobalGuard&&) = delete;
};
