#pragma once
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <string>

struct MeterArchiveResult {
    unsigned copied = 0;
    unsigned existing = 0;
    unsigned skipped = 0;
    unsigned errors = 0;
};

template<class Hasher>
MeterArchiveResult ArchiveMeterReports(const std::filesystem::path& client, const std::filesystem::path& destination, Hasher hash) {
    namespace fs = std::filesystem;
    MeterArchiveResult result;
    try {
        const auto directory = client / L"logs";
        if (!fs::is_directory(directory)) return result;
        fs::create_directories(destination);
        unsigned retained = 0;
        for (auto entry=fs::directory_iterator(destination); entry!=fs::directory_iterator{}; ++entry) {
            ++retained;
            if (retained >= 128) { ++result.skipped; return result; }
        }
        unsigned visited = 0;
        const auto preserve = [&](const fs::path& source, const std::wstring& name) {
            try {
                const DWORD attributes = GetFileAttributesW(source.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES) return;
                if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) || fs::file_size(source) > 1024 * 1024 || retained >= 128) { ++result.skipped; return; }
                const auto digest = hash(source);
                if (digest.size() != 64 || digest.find_first_not_of("0123456789abcdef") != std::string::npos) { ++result.errors; return; }
                const fs::path target = destination / (name + L"-" + std::wstring(digest.begin(), digest.end()) + source.extension().wstring());
                if (fs::exists(target)) {
                    if (hash(target) == digest) ++result.existing;
                    else ++result.errors;
                    return;
                }
                if (!fs::copy_file(source, target) || hash(target) != digest) { ++result.errors; return; }
                ++retained;
                ++result.copied;
            } catch (...) { ++result.errors; }
        };
        preserve(destination.parent_path() / L"status.txt", L"startup");
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (++visited > 4096) { ++result.skipped; break; }
            const std::wstring name = entry.path().filename().wstring();
            if (name.rfind(L"DungeonRunnersCrash-", 0) == 0 && entry.path().extension() == L".log")
                preserve(entry.path(), entry.path().stem().wstring());
        }
    } catch (...) { ++result.errors; }
    return result;
}
