#pragma once
#include <windows.h>
#include "windows_hash.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <client_compatibility.generated.h>

inline bool ClientRangeMatches(const unsigned char* bytes, unsigned size, const ClientProtectedRange& range) {
    try { const auto digest=WindowsHash::Sha256(bytes,size);for(const auto expected:range.hashes) if(expected && digest==expected) return true; }
    catch (...) { return false; }
    return false;
}

inline bool ClientImageCompatible(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input) return false;
    const auto size=input.tellg();
    if(size < ClientMinimumSize || size > ClientMaximumSize) return false;
    std::vector<unsigned char> bytes;
    for(const auto& range:ClientProtectedRanges) {
        if(range.offset > ClientMinimumSize || range.length > ClientMinimumSize-range.offset) return false;
        bytes.resize(range.length);
        input.seekg(range.offset);
        if(!input.read(reinterpret_cast<char*>(bytes.data()),range.length) || !ClientRangeMatches(bytes.data(),range.length,range)) return false;
    }
    return true;
}
