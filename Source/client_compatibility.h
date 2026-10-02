#pragma once
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstring>
#include <client_compatibility.generated.h>

inline bool ClientRangeMatches(const unsigned char* bytes, unsigned size, const ClientProtectedRange& range) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32]{};
    bool good = BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0) >= 0;
    good = good && BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0) >= 0;
    good = good && BCryptHashData(hash,const_cast<PUCHAR>(bytes),size,0) >= 0;
    good = good && BCryptFinishHash(hash,digest,sizeof(digest),0) >= 0;
    if(hash) BCryptDestroyHash(hash);
    if(algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    if(!good) return false;
    char hex[65]{};
    for(unsigned i=0;i<32;++i) { hex[i*2]="0123456789abcdef"[digest[i]>>4];hex[i*2+1]="0123456789abcdef"[digest[i]&15]; }
    for(const auto expected:range.hashes) if(expected && std::strcmp(expected,hex)==0) return true;
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
