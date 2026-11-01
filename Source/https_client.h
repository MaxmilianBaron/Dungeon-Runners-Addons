#pragma once
#include <atomic>
#include <string>

namespace AddonHttps {
struct Response {unsigned status=0;std::string contentType,retryAfter,body;};
Response Parse(const std::string& response);
Response Get(const std::string& host,const std::string& path,const std::atomic<bool>& cancelled);
}
