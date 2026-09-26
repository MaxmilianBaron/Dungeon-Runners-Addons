#pragma once
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <cstring>
#include "native_skin.generated.h"

static std::vector<unsigned char> SkinButton, SkinFrame, SkinFont, SkinAdvance, SkinSylfaen;

static bool LoadSkinData(const std::filesystem::path& path) {
    std::error_code error;
    if (std::filesystem::file_size(path,error) != SkinFileSize || error) return false;
    std::vector<unsigned char> data(SkinFileSize);
    std::ifstream input(path,std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(data.data()),data.size())) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    unsigned char digest[32]{};
    bool valid = BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0) >= 0;
    if (valid) valid = BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0) >= 0;
    if (valid) valid = BCryptHashData(hash,data.data(),static_cast<ULONG>(data.size()),0) >= 0;
    if (valid) valid = BCryptFinishHash(hash,digest,sizeof(digest),0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm,0);
    if (!valid) return false;
    char hex[65]{};
    for (size_t i=0;i<32;++i) { hex[i*2]="0123456789abcdef"[digest[i]>>4]; hex[i*2+1]="0123456789abcdef"[digest[i]&15]; }
    if (std::strcmp(hex,SkinFileSha256) || std::memcmp(data.data(),"DRUI0001",8)) return false;
    std::array<std::vector<unsigned char>*,5> targets{&SkinButton,&SkinFrame,&SkinFont,&SkinAdvance,&SkinSylfaen};
    size_t offset = 28;
    for (size_t i=0;i<targets.size();++i) {
        uint32_t size = 0;
        std::memcpy(&size,data.data()+8+i*4,4);
        if (size > data.size()-offset) return false;
        targets[i]->assign(data.begin()+offset,data.begin()+offset+size);
        offset += size;
    }
    return offset == data.size() && SkinButton.size()==SkinButtonSize*SkinButtonSize && SkinFrame.size()==SkinFrameSize*SkinFrameSize && SkinFont.size()==SkinFontSize*SkinFontSize && SkinAdvance.size()==256 && SkinSylfaen.size()>1024;
}
