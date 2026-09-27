#pragma once
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>
#pragma warning(push,0)
#define DRWAV_API static
#define DRWAV_PRIVATE static
#define DR_WAV_IMPLEMENTATION
#include "third_party/dr_wav.h"
#define DRMP3_API static
#define DRMP3_PRIVATE static
#define DR_MP3_IMPLEMENTATION
#include "third_party/dr_mp3.h"
#pragma warning(pop)

class CustomSoundDevice {
    struct PcmBlock { WAVEHDR header{}; std::vector<short> samples; bool prepared = false, queued = false; };
    HMODULE library = nullptr;
    decltype(&waveOutOpen) open = nullptr;
    decltype(&waveOutClose) close = nullptr;
    decltype(&waveOutReset) reset = nullptr;
    decltype(&waveOutPrepareHeader) prepare = nullptr;
    decltype(&waveOutUnprepareHeader) unprepare = nullptr;
    decltype(&waveOutWrite) write = nullptr;
    HWAVEOUT output = nullptr;
    std::unique_ptr<drwav> wave;
    std::unique_ptr<drmp3> mp3;
    std::array<PcmBlock,2> blocks;
    unsigned channels = 0, rate = 0, gain = 100;
    bool eof = false;
    uint64_t Read(uint64_t frames,short* data) {
        return wave ? drwav_read_pcm_frames_s16(wave.get(),frames,data) : mp3 ? drmp3_read_pcm_frames_s16(mp3.get(),frames,data) : 0;
    }
    bool Fill(PcmBlock& block) {
        if (eof) return false;
        const auto frames = Read(block.samples.size()/channels,block.samples.data());
        if (!frames) { eof = true; return false; }
        const size_t count = static_cast<size_t>(frames)*channels;
        if (gain != 100) for (size_t i=0;i<count;++i) block.samples[i] = static_cast<short>(int(block.samples[i])*int(gain)/100);
        block.header.dwBufferLength = static_cast<DWORD>(count*sizeof(short));
        if (write(output,&block.header,sizeof(block.header)) != MMSYSERR_NOERROR) { eof = true; return false; }
        block.queued = true;
        return true;
    }
public:
    CustomSoundDevice() {
        library = LoadLibraryExW(L"winmm.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (library) {
            open = reinterpret_cast<decltype(open)>(GetProcAddress(library,"waveOutOpen"));
            close = reinterpret_cast<decltype(close)>(GetProcAddress(library,"waveOutClose"));
            reset = reinterpret_cast<decltype(reset)>(GetProcAddress(library,"waveOutReset"));
            prepare = reinterpret_cast<decltype(prepare)>(GetProcAddress(library,"waveOutPrepareHeader"));
            unprepare = reinterpret_cast<decltype(unprepare)>(GetProcAddress(library,"waveOutUnprepareHeader"));
            write = reinterpret_cast<decltype(write)>(GetProcAddress(library,"waveOutWrite"));
        }
    }
    ~CustomSoundDevice() { Close(); if (library) FreeLibrary(library); }
    bool Available() const { return open && close && reset && prepare && unprepare && write; }
    void Close() {
        if (output) {
            reset(output);
            for (auto& block:blocks) {
                if (block.prepared) unprepare(output,&block.header,sizeof(block.header));
                block.prepared = block.queued = false; block.header = {};
            }
            close(output); output = nullptr;
        }
        if (wave) { drwav_uninit(wave.get()); wave.reset(); }
        if (mp3) { drmp3_uninit(mp3.get()); mp3.reset(); }
        eof = false; channels = rate = 0;
    }
    bool Open(const std::filesystem::path& path) {
        Close();
        std::ifstream input(path,std::ios::binary);
        char tag[4]{};
        if (!input.read(tag,sizeof(tag))) return false;
        input.close();
        if (!std::memcmp(tag,"RIFF",4)) {
            auto decoder = std::make_unique<drwav>();
            if (!drwav_init_file_w(decoder.get(),path.c_str(),nullptr)) return false;
            channels = decoder->channels; rate = decoder->sampleRate; wave = std::move(decoder);
        } else {
            auto decoder = std::make_unique<drmp3>();
            if (!drmp3_init_file_w(decoder.get(),path.c_str(),nullptr)) return false;
            channels = decoder->channels; rate = decoder->sampleRate; mp3 = std::move(decoder);
        }
        if (channels < 1 || channels > 2 || rate < 8000 || rate > 192000) { Close(); return false; }
        short sample[2]{};
        const bool valid = Read(1,sample) == 1 && (wave ? drwav_seek_to_pcm_frame(wave.get(),0) : drmp3_seek_to_pcm_frame(mp3.get(),0));
        if (!valid) Close();
        return valid;
    }
    bool Play(unsigned volume) {
        if (!Available() || (!wave && !mp3) || output) return false;
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = static_cast<WORD>(channels);
        format.nSamplesPerSec = rate; format.wBitsPerSample = 16;
        format.nBlockAlign = static_cast<WORD>(channels*sizeof(short));
        format.nAvgBytesPerSec = rate*format.nBlockAlign;
        gain = std::min(volume,100u);
        if (open(&output,WAVE_MAPPER,&format,0,0,CALLBACK_NULL) != MMSYSERR_NOERROR) { output = nullptr; return false; }
        for (auto& block:blocks) {
            block.samples.resize((rate/4)*channels);
            block.header = {}; block.header.lpData = reinterpret_cast<char*>(block.samples.data());
            block.header.dwBufferLength = static_cast<DWORD>(block.samples.size()*sizeof(short));
            if (prepare(output,&block.header,sizeof(block.header)) != MMSYSERR_NOERROR) { Close(); return false; }
            block.prepared = true;
        }
        bool started = false;
        for (auto& block:blocks) started |= Fill(block);
        if (!started) Close();
        return started;
    }
    bool Playing() {
        if (!output) return false;
        bool playing = false;
        for (auto& block:blocks) {
            if (block.queued && (block.header.dwFlags & WHDR_DONE)) block.queued = false;
            if (!block.queued) Fill(block);
            playing |= block.queued;
        }
        return playing;
    }
};
