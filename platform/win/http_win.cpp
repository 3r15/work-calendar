#include "platform/http.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace platform {
namespace {

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                           static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(),
                        length);
    return out;
}

util::Error networkError(std::string what) {
    return util::makeError(util::ErrorCode::Io, std::move(what),
                           "인터넷 연결을 확인해 주세요. 업데이트는 나중에 해도 됩니다.");
}

// WinHTTP 핸들을 반드시 닫는다. 중간에 return 이 많아 손으로 닫으면 새기 쉽다.
class Handle {
public:
    explicit Handle(HINTERNET handle) : handle_(handle) {}
    ~Handle() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    HINTERNET get() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }

private:
    HINTERNET handle_;
};

// 열린 응답 하나. 본문을 조각내어 콜백에 넘긴다.
template <typename Sink>
util::Result<int> request(const std::string& url, const std::string& userAgent, Sink&& sink) {
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);

    const std::wstring wideUrl = toWide(url);
    if (WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts) == FALSE) {
        return util::makeError(util::ErrorCode::InvalidUsage,
                               "주소를 이해할 수 없습니다: " + url);
    }

    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength > 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }

    const Handle session{WinHttpOpen(toWide(userAgent).c_str(),
                                     WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) {
        return networkError("네트워크를 시작할 수 없습니다.");
    }

    const Handle connection{
        WinHttpConnect(session.get(), host.c_str(), parts.nPort, 0)};
    if (!connection) {
        return networkError("서버에 연결할 수 없습니다.");
    }

    const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    const Handle req{WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr,
                                        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)};
    if (!req) {
        return networkError("요청을 만들 수 없습니다.");
    }

    if (WinHttpSendRequest(req.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                           0, 0) == FALSE ||
        WinHttpReceiveResponse(req.get(), nullptr) == FALSE) {
        return networkError("서버가 응답하지 않습니다.");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(req.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

    std::vector<char> chunk(16 * 1024);
    for (;;) {
        DWORD available = 0;
        if (WinHttpQueryDataAvailable(req.get(), &available) == FALSE) {
            return networkError("응답을 읽는 중 연결이 끊겼습니다.");
        }
        if (available == 0) {
            break;
        }
        while (available > 0) {
            const DWORD want =
                (available < static_cast<DWORD>(chunk.size())) ? available
                                                               : static_cast<DWORD>(chunk.size());
            DWORD read = 0;
            if (WinHttpReadData(req.get(), chunk.data(), want, &read) == FALSE || read == 0) {
                return networkError("응답을 읽는 중 연결이 끊겼습니다.");
            }
            sink(chunk.data(), static_cast<std::size_t>(read));
            available -= read;
        }
    }
    return static_cast<int>(status);
}

}  // namespace

util::Result<HttpResponse> httpGet(const std::string& url, const std::string& userAgent) {
    HttpResponse response;
    const util::Result<int> status =
        request(url, userAgent,
                [&response](const char* data, std::size_t length) { response.body.append(data, length); });
    if (!status) {
        return status.error();
    }
    response.status = status.value();
    return response;
}

util::Result<void> httpDownload(const std::string& url, const std::filesystem::path& target,
                                const std::string& userAgent) {
    // 받다 만 파일이 진짜 파일 자리에 남지 않게 임시 이름으로 받는다.
    std::filesystem::path temp = target;
    temp += ".part";

    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return util::makeError(util::ErrorCode::Io,
                                   temp.string() + " 파일을 만들 수 없습니다.");
        }
        const util::Result<int> status =
            request(url, userAgent, [&out](const char* data, std::size_t length) {
                out.write(data, static_cast<std::streamsize>(length));
            });
        if (!status) {
            out.close();
            std::filesystem::remove(temp, ec);
            return status.error();
        }
        if (status.value() != 200) {
            out.close();
            std::filesystem::remove(temp, ec);
            return util::makeError(util::ErrorCode::Io,
                                   "서버가 " + std::to_string(status.value()) +
                                       " 로 답했습니다. 파일을 받지 못했습니다.");
        }
    }

    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return util::makeError(util::ErrorCode::Io, target.string() + " 로 옮기지 못했습니다.");
    }
    return {};
}

}  // namespace platform
