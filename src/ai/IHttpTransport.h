#pragma once
#include <chrono>
#include <map>
#include <string>

namespace guitar_dsp::ai {

class CancellationToken;

struct HttpResponse {
    int                                 status = 0;   // 0 = transport error
    std::string                         body;
    std::string                         error;        // populated when status==0
    std::map<std::string, std::string>  headers;
};

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;

    virtual HttpResponse post(const std::string& url,
                              const std::map<std::string, std::string>& headers,
                              const std::string& body,
                              std::chrono::milliseconds timeout,
                              CancellationToken* cancel = nullptr) = 0;

    virtual HttpResponse get(const std::string& url,
                             std::chrono::milliseconds timeout,
                             CancellationToken* cancel = nullptr) = 0;
};

} // namespace guitar_dsp::ai
