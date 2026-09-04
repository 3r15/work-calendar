#pragma once

#include "core/util/result.h"

#include <filesystem>
#include <string>

namespace platform {

// HTTP GET. WinHTTP 를 쓰므로 Windows 전용이고, 그래서 core/ 가 아니라 여기 있다.
//
// WinHTTP 를 고른 이유는 OS 에 들어 있어서다 (DESIGN 6.1). TLS 가 포함되고 의존성이 늘지 않고
// 배포 크기가 커지지 않는다.
//
// 네트워크가 없다고 프로그램이 멈추면 안 된다. 실패는 전부 Result 로 돌아온다.

struct HttpResponse {
    int status{0};
    std::string body;
};

// GitHub API 는 User-Agent 를 요구한다. 없으면 403 이 돌아온다.
util::Result<HttpResponse> httpGet(const std::string& url, const std::string& userAgent);

// 큰 파일을 메모리에 다 올리지 않고 바로 파일로 내린다.
util::Result<void> httpDownload(const std::string& url, const std::filesystem::path& target,
                                const std::string& userAgent);

}  // namespace platform
