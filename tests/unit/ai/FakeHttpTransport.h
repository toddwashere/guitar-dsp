#pragma once
#include "ai/IHttpTransport.h"

#include <queue>
#include <vector>

namespace guitar_dsp::ai::test {

struct RecordedCall {
    std::string                         method;   // "POST" or "GET"
    std::string                         url;
    std::map<std::string, std::string>  headers;
    std::string                         body;
};

class FakeHttpTransport : public IHttpTransport {
public:
    std::vector<RecordedCall>  calls;
    std::queue<HttpResponse>   replies;

    HttpResponse post(const std::string& url,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body,
                      std::chrono::milliseconds,
                      CancellationToken* = nullptr) override {
        calls.push_back({"POST", url, headers, body});
        return nextReply();
    }

    HttpResponse get(const std::string& url,
                     std::chrono::milliseconds,
                     CancellationToken* = nullptr) override {
        calls.push_back({"GET", url, {}, ""});
        return nextReply();
    }

private:
    HttpResponse nextReply() {
        if (replies.empty()) return HttpResponse{0, "", "no scripted reply", {}};
        auto r = std::move(replies.front()); replies.pop();
        return r;
    }
};

} // namespace guitar_dsp::ai::test
