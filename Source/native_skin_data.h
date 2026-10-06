#pragma once
#include "windows_hash.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <array>
#include <cstring>
#include "native_skin.generated.h"

static std::vector<unsigned char> SkinButton, SkinFrame, SkinFont, SkinAdvance, SkinSylfaen, SkinWell, SkinWellReady;

static bool LoadSkinData(const std::filesystem::path& path) {
    std::error_code error;
    const auto fileSize = std::filesystem::file_size(path,error);
    if (error || (fileSize != SkinFileSize && fileSize != SkinLegacyFileSize)) return false;
    const bool legacy = fileSize == SkinLegacyFileSize;
    std::vector<unsigned char> data(static_cast<size_t>(fileSize));
    std::ifstream input(path,std::ios::binary);
    if (!input.read(reinterpret_cast<char*>(data.data()),data.size())) return false;
    try { if (WindowsHash::Sha256(data.data(),static_cast<DWORD>(data.size()))!=(legacy ? SkinLegacyFileSha256 : SkinFileSha256)) return false; }
    catch (...) { return false; }
    if (std::memcmp(data.data(),legacy ? "DRUI0001" : "DRUI0002",8)) return false;
    std::array<std::vector<unsigned char>,7> parts;
    const size_t count = legacy ? 5 : 7;
    size_t offset = 8 + count * 4;
    for (size_t i=0;i<count;++i) {
        uint32_t size = 0;
        std::memcpy(&size,data.data()+8+i*4,4);
        if (size > data.size()-offset) return false;
        parts[i].assign(data.begin()+offset,data.begin()+offset+size);
        offset += size;
    }
    if (offset != data.size() || parts[0].size()!=SkinButtonSize*SkinButtonSize || parts[1].size()!=SkinFrameSize*SkinFrameSize || parts[2].size()!=SkinFontSize*SkinFontSize || parts[3].size()!=256 || parts[4].size()<=1024) return false;
    if (!legacy && (parts[5].size()!=21972 || parts[6].size()!=11088)) return false;
    std::array<std::vector<unsigned char>*,7> targets{&SkinButton,&SkinFrame,&SkinFont,&SkinAdvance,&SkinSylfaen,&SkinWell,&SkinWellReady};
    for (size_t i=0;i<targets.size();++i) targets[i]->swap(parts[i]);
    return true;
}
