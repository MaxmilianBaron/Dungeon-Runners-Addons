#pragma once
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <system_error>
#include "trade_auction_catalog.h"

namespace MythicCollection {
using Finds = std::map<std::string,unsigned,std::less<>>;

inline std::optional<unsigned> CollectedTier(const TradeAuction::CatalogItem& item,const Finds& finds) {
    std::optional<unsigned> tier;
    for (const auto* recipe:item.recipes) {
        const auto found=finds.find(TradeAuction::CanonicalDefinition(recipe->item));
        if (found!=finds.end()) tier=std::max(tier.value_or(0),found->second);
    }
    return tier;
}

inline bool Record(Finds& finds,const std::string& key,unsigned tier) {
    if (key.empty() || key.size()>255 || key.find_first_of(" \r\n\t")!=std::string::npos || tier>3) return false;
    auto [entry,inserted]=finds.try_emplace(key,tier);
    if (!inserted && tier<=entry->second) return false;
    entry->second=tier;
    return true;
}

inline std::filesystem::path LocalPath() {
    wchar_t buffer[MAX_PATH]{};
    DWORD size=GetEnvironmentVariableW(L"LOCALAPPDATA",buffer,MAX_PATH);
    if (!size) size=GetEnvironmentVariableW(L"APPDATA",buffer,MAX_PATH);
    if (!size || size>=MAX_PATH) return {};
    std::filesystem::path root(buffer);
    return root.is_absolute() ? root/L"Dungeon Runners Addons"/L"Mythic Collection"/L"collection.txt" : std::filesystem::path{};
}

class Book {
    static constexpr const char* Header="DR Mythic Collection 1";
    std::filesystem::path path;
    Finds finds;
    bool dirty=false;
    uint64_t revision=0;

    bool Read(Finds& saved) const {
        std::error_code error;
        const auto status=std::filesystem::symlink_status(path,error);
        if (status.type()==std::filesystem::file_type::not_found) return true;
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::file_size(path,error)>262144 || error) return false;
        std::ifstream input(path);
        std::string line;
        if (!std::getline(input,line) || line!=Header) return false;
        while (std::getline(input,line)) {
            if (line.size()<3 || line[1]!=' ' || line[0]<'0' || line[0]>'3' ||
                !Record(saved,line.substr(2),static_cast<unsigned>(line[0]-'0'))) return false;
        }
        return input.eof();
    }

    bool Write(const Finds& saved) const {
        auto pending=path;
        pending+=L".tmp";
        std::ofstream output(pending,std::ios::trunc);
        output<<Header<<'\n';
        for (const auto& pair:saved) output<<pair.second<<' '<<pair.first<<'\n';
        output.close();
        return output.good() && MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
    }

public:
    void Initialize(std::filesystem::path file) { path=std::move(file); finds.clear(); dirty=false; ++revision; }
    const Finds& Entries() const { return finds; }
    bool Dirty() const { return dirty; }
    uint64_t Revision() const { return revision; }
    void Add(const Finds& sample) {
        for (const auto& pair:sample) if (Record(finds,pair.first,pair.second)) {
            dirty=true;
            ++revision;
        }
    }
    bool Sync() {
        if (path.empty()) return false;
        if (!dirty) {
            Finds saved;
            if (!Read(saved)) return false;
            for (const auto& pair:saved) if (Record(finds,pair.first,pair.second)) ++revision;
            return true;
        }
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(),error);
        if (error) return false;
        auto lockPath=path;
        lockPath+=L".lock";
        const auto lock=CreateFileW(lockPath.c_str(),GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (lock==INVALID_HANDLE_VALUE) return false;
        struct FileLock { HANDLE handle; ~FileLock() { CloseHandle(handle); } } guard{lock};
        Finds saved;
        if (!Read(saved)) return false;
        for (const auto& pair:finds) Record(saved,pair.first,pair.second);
        if (!Write(saved)) return false;
        if (finds!=saved) ++revision;
        finds=std::move(saved);
        dirty=false;
        return true;
    }
};
}
