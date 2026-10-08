#pragma once
#include "loadouts.h"
#include <filesystem>
#include <fstream>
#include <windows.h>

class LoadoutStore {
    std::filesystem::path root, file;
    std::string owner;
    bool readable=true;
    static bool Plain(const std::filesystem::path& path,bool directory) {
        const auto attrs=GetFileAttributesW(path.c_str());
        return attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_REPARSE_POINT) && bool(attrs&FILE_ATTRIBUTE_DIRECTORY)==directory;
    }
public:
    std::vector<Loadout> sets;
    std::string error;
    void Initialize(const std::filesystem::path& path) { root=path; file.clear(); owner.clear(); sets.clear(); error.clear(); readable=true; }
    const std::string& Owner() const { return owner; }
    bool Ready() const { return readable && !owner.empty() && !file.empty(); }
    bool Select(const std::string& character) {
        if (owner==character) return readable;
        owner=character; sets.clear(); error.clear(); file.clear(); readable=true;
        if (owner.empty() || root.empty() || owner.size()>256) { readable=false; return false; }
        uint64_t hash=14695981039346656037ull;
        for (unsigned char c:owner) { hash^=c; hash*=1099511628211ull; }
        std::ostringstream name; name<<"character-"<<std::hex<<std::setw(16)<<std::setfill('0')<<hash<<".loadouts";
        file=root/name.str();
        std::error_code ec;
        if (!std::filesystem::exists(root,ec) && !ec) return true;
        if (!Plain(root,true) || ec) { readable=false; error="Loadouts folder is unavailable."; return false; }
        if (!std::filesystem::exists(file,ec) && !ec) return true;
        std::ifstream input(file,std::ios::binary);
        if (ec || !Plain(file,false) || !input || !ReadLoadouts(input,owner,sets)) { readable=false; error="Saved loadouts could not be read. The file was left unchanged."; }
        return readable;
    }
    bool Save(const std::vector<Loadout>& value) {
        if (!readable || owner.empty() || file.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories(root,ec);
        if (ec || !Plain(root,true)) { error="Could not create the Loadouts folder."; return false; }
        auto pending=file; pending+=L".pending";
        if ((std::filesystem::exists(pending,ec) && !Plain(pending,false)) || ec ||
            (std::filesystem::exists(file,ec) && !Plain(file,false)) || ec) { error="Loadouts file is unavailable."; return false; }
        std::ostringstream serialized;
        if (!WriteLoadouts(serialized,owner,value)) { error="Choose a name and at least one compatible item."; return false; }
        std::ofstream out(pending,std::ios::binary|std::ios::trunc);
        out<<serialized.str(); out.close();
        if (!out.good() || !MoveFileExW(pending.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { error="Could not save loadouts. Previous settings were kept."; return false; }
        sets=value; error.clear(); return true;
    }
};
