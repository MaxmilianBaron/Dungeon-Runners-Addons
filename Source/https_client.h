#pragma once
#include <atomic>
#include <string>

namespace AddonHttps {
inline constexpr char TradeServiceHost[]="dungeon-runners-trade-auction.pages.dev";
struct Response {unsigned status=0;std::string contentType,retryAfter,body;};
Response Parse(const std::string& response);
Response Get(const std::string& host,const std::string& path,const std::atomic<bool>& cancelled);
Response Trade(const std::string& host,const std::string& path,const std::string& method,
    const std::string& body,const std::string& token,const std::atomic<bool>& cancelled);
bool TradeHost(const std::string& host);
}
