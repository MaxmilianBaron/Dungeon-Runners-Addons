#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <thread>
#include "https_client.h"
#include "trade_auction.h"
#include "windows_compat.h"

namespace TradeAuction {
inline constexpr const char* ServiceHost=AddonHttps::TradeServiceHost;
inline constexpr uint64_t WantedRefreshIntervalMs=5*60*1000;
inline constexpr uint64_t WantedLifetimeMs=WantedRefreshIntervalMs+30*1000;

inline std::string NewOfferId() {
    unsigned char bytes[16]{};
    HCRYPTPROV provider=0;
    if (!CryptAcquireContextW(&provider,nullptr,nullptr,PROV_RSA_FULL,CRYPT_VERIFYCONTEXT)) return {};
    const bool ok=CryptGenRandom(provider,sizeof(bytes),bytes)!=FALSE;
    CryptReleaseContext(provider,0);
    if (!ok) return {};
    const char* digits="0123456789abcdef";
    std::string result;
    for (auto value:bytes) {
        result+=digits[value>>4];
        result+=digits[value&15];
    }
    return result;
}

struct Query {
    bool mine=false;
    std::string before;
};
struct Result {
    enum class Kind { List, Save };
    Kind kind=Kind::List;
    Page page;
    std::string error;
};

enum class ConnectionStatus { Checking, Connected, Offline };

class Client {
    struct State {
        std::filesystem::path tokenFile,trackDirectory;
        std::string host;
        std::mutex mutex;
        std::optional<Result> result;
        std::atomic<bool> stopped{false};
        std::atomic<ConnectionStatus> connection{ConnectionStatus::Checking};
        bool busy=false,wantedBusy=false;
        uint64_t retryAt=0,nextWanted=0,trackRevision=0;
        std::set<std::string> tracked;
        std::shared_ptr<const WantedIndex> wanted;
    };
    std::shared_ptr<State> state=std::make_shared<State>();

    static bool IsJson(const AddonHttps::Response& response) {
        return response.contentType=="application/json" || response.contentType.rfind("application/json;",0)==0;
    }

    static AddonHttps::Response Send(State& state,const std::string& path,const char* method,
        const std::string& body={},const std::string& token={}) {
        try {
            auto reply=AddonHttps::Trade(state.host,path,method,body,token,state.stopped);
            const bool reachable=IsJson(reply) && (reply.status/100==2 || reply.status/100==4);
            state.connection.store(reachable ? ConnectionStatus::Connected : ConnectionStatus::Offline);
            return reply;
        } catch (...) {
            state.connection.store(ConnectionStatus::Offline);
            throw;
        }
    }

    static std::string ReadToken(const State& state) {
        const DWORD attributes=GetFileAttributesW(state.tokenFile.c_str());
        if (attributes==INVALID_FILE_ATTRIBUTES) {
            if (GetLastError()==ERROR_FILE_NOT_FOUND) return {};
            throw std::runtime_error("Trade Auction cannot read its local identity.");
        }
        if (attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))
            throw std::runtime_error("Trade Auction cannot read its local identity.");
        std::ifstream input(state.tokenFile,std::ios::binary);
        std::string token(64,'\0');
        if (!input.read(token.data(),64) || input.peek()!=EOF || !Hex(token,64))
            throw std::runtime_error("Trade Auction cannot read its local identity.");
        return token;
    }

    static std::string Token(State& state,bool create) {
        auto token=ReadToken(state);
        if (!token.empty() || !create) return token;
        auto reply=Send(state,"/v1/identity","POST");
        if (reply.status!=201) throw std::runtime_error("Cannot create a Trade Auction identity. Try again later.");
        auto identity=Leaderboard::Json::Reader(reply.body).Parse();
        token=Text(Require(identity,"token",Value::Type::String),64);
        if (!Hex(token,64)) throw std::runtime_error("Invalid Trade Auction identity.");
        const HANDLE file=CreateFileW(state.tokenFile.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file==INVALID_HANDLE_VALUE) {
            if (GetLastError()==ERROR_FILE_EXISTS) {
                auto saved=ReadToken(state);
                if (!saved.empty()) return saved;
            }
            throw std::runtime_error("Cannot save the local Trade Auction identity.");
        }
        DWORD count=0;
        const bool written=WriteFile(file,token.data(),64,&count,nullptr) && count==64 && FlushFileBuffers(file);
        CloseHandle(file);
        if (!written) {
            DeleteFileW(state.tokenFile.c_str());
            throw std::runtime_error("Cannot save the local Trade Auction identity.");
        }
        return token;
    }

    static void RequireSuccess(const AddonHttps::Response& response,bool writing) {
        if ((response.status!=200 && (!writing || response.status!=201)) ||
            !IsJson(response)) {
            if (response.status==429) throw std::runtime_error("Trade Auction is busy. Try again in a minute.");
            if (response.status==409) throw std::runtime_error("The offer changed or your list is full. Refresh My Offers.");
            if (response.status==404 && writing) throw std::runtime_error("This offer is no longer available.");
            throw std::runtime_error("Trade Auction could not finish the request.");
        }
    }
    static Result Run(State& state,const Query& query,const std::optional<Offer>& offer) {
        Result result;
        if (offer) {
            result.kind=Result::Kind::Save;
            const auto token=Token(state,!offer->revision);
            if (token.empty()) throw std::runtime_error("Trade Auction identity is unavailable.");
            const auto reply=Send(state,"/v1/offers/"+offer->id,"PUT",Write(*offer),token);
            RequireSuccess(reply,true);
            auto receipt=Leaderboard::Json::Reader(reply.body).Parse();
            const auto revision=Leaderboard::Json::Integer(Require(receipt,"revision",Value::Type::Number));
            if (Require(receipt,"id",Value::Type::String).text!=offer->id || revision!=offer->revision+1)
                throw std::runtime_error("The offer could not be confirmed. Refresh My Offers.");
            return result;
        }
        const auto token=query.mine ? Token(state,false) : std::string{};
        if (query.mine && token.empty()) return result;
        std::string url="/v1/offers";
        if (query.mine) url+="?mine=1";
        if (!query.before.empty()) url+=(query.mine ? "&before=" : "?before=")+query.before;
        const auto reply=Send(state,url,"GET",{},token);
        RequireSuccess(reply,false);
        result.page=ReadPage(Leaderboard::Json::Reader(reply.body).Parse());
        return result;
    }

    static bool ReadTracked(State& state,const std::set<std::string>& selected,WantedIndex& index,std::set<std::string>& verified) {
        if (selected.empty()) return true;
        std::string url="/v1/tracked?ids=";
        for (const auto& id:selected) {
            if (url.back()!='=') url+='.';
            url+=id;
        }
        const auto reply=Send(state,url,"GET");
        RequireSuccess(reply,false);
        const auto page=Leaderboard::Json::Reader(reply.body).Parse();
        const auto& rows=Require(page,"tracked",Value::Type::Array).array;
        if (rows.size()>selected.size()) return false;
        decltype(index.tracked) tracks;
        for (const auto& row:rows) {
            TrackedOffer offer;
            offer.id=Text(Require(row,"id",Value::Type::String),32);
            offer.definition=Text(Require(row,"definition",Value::Type::String),192);
            offer.character=Text(Require(row,"character",Value::Type::String),64);
            offer.offered=Text(Require(row,"offered",Value::Type::String),160);
            const auto signature=Text(Require(row,"signature",Value::Type::String),64);
            if (!selected.count(offer.id) || !verified.insert(offer.id).second || !Definition(offer.definition) || !Hex(signature,64)) return false;
            tracks[signature].push_back(std::move(offer));
        }
        for (const auto& row:tracks) for (const auto& offer:row.second)
            index.definitions[offer.definition].insert(row.first);
        index.tracked=std::move(tracks);
        return true;
    }

    static std::shared_ptr<WantedIndex> ReadWanted(State& state,uint64_t now) {
        auto index=std::make_shared<WantedIndex>();
        index->expires=now+WantedLifetimeMs;
        std::string cursor;
        for (unsigned number=0;number<32 && !state.stopped;++number) {
            const auto url=cursor.empty() ? std::string("/v1/wanted") : "/v1/wanted?after="+cursor;
            const auto reply=Send(state,url,"GET");
            RequireSuccess(reply,false);
            const auto page=Leaderboard::Json::Reader(reply.body).Parse();
            const auto& rows=Require(page,"wanted",Value::Type::Array).array;
            if (rows.size()>128) return {};
            auto last=cursor;
            for (const auto& row:rows) {
                auto definition=Text(Require(row,"definition",Value::Type::String),192);
                auto signature=Text(Require(row,"signature",Value::Type::String),64);
                if (!Definition(definition) || !Hex(signature,64) || signature<=last) return {};
                last=signature;
                index->definitions[std::move(definition)].insert(std::move(signature));
            }
            cursor=Text(Require(page,"next",Value::Type::String),64,true);
            if (cursor.empty()) return index;
            if (rows.empty() || cursor!=last) return {};
        }
        return {};
    }

public:
    Client(std::string host,const std::filesystem::path& directory) {
        if (!AddonHttps::TradeHost(host)) return;
        state->host=std::move(host);
        state->tokenFile=directory/std::filesystem::u8path(state->host+".trade-token");
        state->trackDirectory=directory/L"Tracked"/std::filesystem::u8path(state->host);
        std::error_code error;
        const std::filesystem::directory_iterator end;
        for (std::filesystem::directory_iterator it(state->trackDirectory,error); !error && it!=end; it.increment(error)) {
            const auto name=it->path().stem().u8string();
            if (it->path().extension()==L".track" && Hex(name,32) && it->is_regular_file(error))
                state->tracked.insert(name);
            if (state->tracked.size()>=TrackLimit) break;
        }
    }
    ~Client() { state->stopped=true; }
    bool Configured() const { return !state->host.empty(); }
    ConnectionStatus Connection() const {
        return Configured() ? state->connection.load() : ConnectionStatus::Offline;
    }
    bool Request(Query query,std::optional<Offer> offer={}) {
        const auto current=state;
        std::lock_guard<std::mutex> guard(current->mutex);
        if (!Configured() || current->busy || current->result || WindowsCompat::Milliseconds()<current->retryAt) return false;
        current->busy=true;
        try {
            std::thread([current,query,offer] {
                Result result;
                result.kind=offer ? Result::Kind::Save : Result::Kind::List;
                try { result=Run(*current,query,offer); }
                catch (const std::exception& error) { result.error=error.what(); }
                catch (...) { result.error="Trade Auction could not finish the request."; }
                std::lock_guard<std::mutex> lock(current->mutex);
                current->busy=false;
                if (result.kind==Result::Kind::Save && result.error.empty()) current->nextWanted=0;
                const bool rateLimited=result.error.find("Try again in a minute.")!=std::string::npos;
                current->retryAt=WindowsCompat::Milliseconds()+(rateLimited ? 60000 : result.error.empty() ? 0 : 1000);
                if (!current->stopped) current->result=std::move(result);
            }).detach();
        } catch (...) {
            current->busy=false;
            return false;
        }
        return true;
    }
    bool Busy() const {
        std::lock_guard<std::mutex> guard(state->mutex);
        return state->busy;
    }
    bool Tracking(const std::string& id) const {
        std::lock_guard<std::mutex> guard(state->mutex);
        return state->tracked.count(id)!=0;
    }
    std::string ToggleTrack(const Offer& offer) {
        std::lock_guard<std::mutex> guard(state->mutex);
        if (!Configured() || !Hex(offer.id,32)) return "Trade Auction service is unavailable.";
        const bool remove=state->tracked.count(offer.id)!=0;
        if (!remove && !CanTrack(offer)) return "This offer needs an exact wanted item.";
        if (!remove && state->tracked.size()>=TrackLimit) return "Untrack another offer first.";
        std::error_code error;
        const auto marker=state->trackDirectory/std::filesystem::u8path(offer.id+".track");
        if (remove) {
            std::filesystem::remove(marker,error);
            if (error) return "Could not untrack the offer.";
            state->tracked.erase(offer.id);
        } else {
            std::filesystem::create_directories(state->trackDirectory,error);
            if (error) return "Could not track the offer.";
            const HANDLE file=CreateFileW(marker.c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
            if (file==INVALID_HANDLE_VALUE) return "Could not track the offer.";
            CloseHandle(file);
            state->tracked.insert(offer.id);
        }
        if (state->wanted && remove) {
            auto next=std::make_shared<WantedIndex>(*state->wanted);
            for (auto& row:next->tracked)
                row.second.erase(std::remove_if(row.second.begin(),row.second.end(),[&](const TrackedOffer& old) {
                    return old.id==offer.id;
                }),row.second.end());
            state->wanted=std::move(next);
        }
        ++state->trackRevision;
        state->nextWanted=0;
        return remove ? "Offer untracked." : "Tracking this offer.";
    }
    std::shared_ptr<const WantedIndex> Wanted(uint64_t now) {
        const auto current=state;
        std::lock_guard<std::mutex> guard(current->mutex);
        if (!Configured()) return {};
        if (!current->wantedBusy && now>=current->nextWanted) {
            current->wantedBusy=true;
            current->nextWanted=now+WantedRefreshIntervalMs;
            try {
                const auto selected=current->tracked;
                const auto revision=current->trackRevision;
                std::thread([current,now,selected,revision] {
                    std::shared_ptr<WantedIndex> wanted;
                    std::set<std::string> tracked;
                    bool verified=false;
                    try { wanted=ReadWanted(*current,now); } catch (...) {}
                    if (wanted) try { verified=ReadTracked(*current,selected,*wanted,tracked); } catch (...) {}
                    std::lock_guard<std::mutex> lock(current->mutex);
                    if (wanted && !current->stopped && current->trackRevision==revision) {
                        if (verified) for (const auto& id:selected) if (!tracked.count(id)) {
                            std::error_code error;
                            std::filesystem::remove(current->trackDirectory/std::filesystem::u8path(id+".track"),error);
                            if (!error) current->tracked.erase(id);
                        }
                        current->wanted=std::move(wanted);
                    }
                    current->wantedBusy=false;
                }).detach();
            } catch (...) { current->wantedBusy=false; }
        }
        return current->wanted && now<current->wanted->expires ? current->wanted : nullptr;
    }
    std::optional<Result> Poll() {
        std::lock_guard<std::mutex> guard(state->mutex);
        auto result=std::move(state->result);
        state->result.reset();
        return result;
    }
};
}
