#pragma once
#include "windows_compat.h"
#include "AardvarkUI/ui.h"
#include <algorithm>
#include <cmath>

class HealthVignette {
    UiTexture texture = nullptr;
    IDirect3DDevice9* owner = nullptr;
    uint64_t retryAt = 0;
public:
    HealthVignette() = default;
    HealthVignette(const HealthVignette&) = delete;
    HealthVignette& operator=(const HealthVignette&) = delete;
    ~HealthVignette() { Reset(); }
    void Reset() {
        if (texture) texture->Release();
        texture = nullptr; owner = nullptr; retryAt = 0;
    }
    void Draw(IDirect3DDevice9* device,UiDrawList* draw,UiPoint size,unsigned intensity) {
        if (!device || !draw || !std::isfinite(size.x) || !std::isfinite(size.y) || size.x <= 0 || size.y <= 0 || !intensity || intensity > 100) return;
        if (owner != device) { Reset(); owner = device; }
        if (!texture) {
            const auto now = WindowsCompat::Milliseconds();
            if (now < retryAt) return;
            retryAt = now + 1000;
            constexpr unsigned side = 128;
            UiTexture next = nullptr;
            if (FAILED(device->CreateTexture(side,side,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&next,nullptr))) return;
            D3DLOCKED_RECT pixels{};
            if (FAILED(next->LockRect(0,&pixels,nullptr,0))) { next->Release(); return; }
            for (unsigned y = 0; y < side; ++y) {
                auto* row = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(pixels.pBits) + y * pixels.Pitch);
                const float vertical = std::clamp((std::abs(2.f * y / (side - 1) - 1) - .55f) / .45f,0.f,1.f);
                for (unsigned x = 0; x < side; ++x) {
                    const float horizontal = std::clamp((std::abs(2.f * x / (side - 1) - 1) - .55f) / .45f,0.f,1.f);
                    const float alpha = 1 - (1 - horizontal * horizontal) * (1 - vertical * vertical);
                    row[x] = UI_COLOR(210,0,0,unsigned(alpha * 255 + .5f));
                }
            }
            if (FAILED(next->UnlockRect(0))) { next->Release(); return; }
            texture = next;
        }
        draw->AddImage(texture,{0,0},size,{0,0},{1,1},UI_COLOR(255,255,255,intensity * 255 / 100));
    }
};
