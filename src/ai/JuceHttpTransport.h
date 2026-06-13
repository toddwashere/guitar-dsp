#pragma once
#include "ai/IHttpTransport.h"

namespace guitar_dsp::ai {

class JuceHttpTransport : public IHttpTransport {
public:
    HttpResponse post(const std::string& url,
                      const std::map<std::string, std::string>& headers,
                      const std::string& body,
                      std::chrono::milliseconds timeout,
                      CancellationToken* cancel = nullptr) override;

    HttpResponse get(const std::string& url,
                     std::chrono::milliseconds timeout,
                     CancellationToken* cancel = nullptr) override;
};

} // namespace guitar_dsp::ai
