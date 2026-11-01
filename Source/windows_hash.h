#pragma once
#include <windows.h>
#include <wincrypt.h>
#include <stdexcept>
#include <string>

class WindowsHash {
    HCRYPTPROV provider=0;
    HCRYPTHASH hash=0;
public:
    static constexpr ALG_ID Sha256Algorithm=ALG_CLASS_HASH|ALG_TYPE_ANY|12;
    explicit WindowsHash(ALG_ID algorithm=Sha256Algorithm) {
        if (!CryptAcquireContextW(&provider,nullptr,nullptr,algorithm==CALG_SHA1 ? PROV_RSA_FULL : PROV_RSA_AES,CRYPT_VERIFYCONTEXT) || !CryptCreateHash(provider,algorithm,0,0,&hash)) {
            if (provider) CryptReleaseContext(provider,0);
            throw std::runtime_error("Hash initialization failed");
        }
    }
    WindowsHash(const WindowsHash&)=delete;
    WindowsHash& operator=(const WindowsHash&)=delete;
    ~WindowsHash() { if (hash) CryptDestroyHash(hash); if (provider) CryptReleaseContext(provider,0); }
    void Add(const unsigned char* data,DWORD size) { if (!CryptHashData(hash,data,size,0)) throw std::runtime_error("Hash input failed"); }
    std::string Finish(DWORD size=32) {
        unsigned char digest[32]{};
        DWORD count=sizeof(digest);
        if (size>sizeof(digest) || !CryptGetHashParam(hash,HP_HASHVAL,digest,&count,0) || count!=size) throw std::runtime_error("Hash finalization failed");
        std::string result;
        for (DWORD i=0;i<count;i++) { result+="0123456789abcdef"[digest[i]>>4];result+="0123456789abcdef"[digest[i]&15]; }
        return result;
    }
    static std::string Sha256(const unsigned char* data,DWORD size) { WindowsHash hash;hash.Add(data,size);return hash.Finish(); }
};
