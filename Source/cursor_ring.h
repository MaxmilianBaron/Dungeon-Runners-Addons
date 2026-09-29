#pragma once
#include "ui.h"
#include <algorithm>
#include <cmath>

class CursorRing {
    UiTexture texture = nullptr;
    IDirect3DDevice9* owner = nullptr;
    float radius = 0, scale = 0, extent = 0;
    UiColor color = 0;
    uint64_t retryAt = 0;
    unsigned side = 0;
public:
    CursorRing() = default;
    CursorRing(const CursorRing&) = delete;
    CursorRing& operator=(const CursorRing&) = delete;
    ~CursorRing() { Reset(); }
    void Reset() {
        if (texture) texture->Release();
        texture = nullptr; owner = nullptr; radius = scale = extent = 0; color = 0; retryAt = 0; side = 0;
    }
    void Draw(IDirect3DDevice9* device,UiDrawList* draw,UiPoint center,float nextRadius,float nextScale,UiColor nextColor) {
        if (!device || !draw || !std::isfinite(center.x) || !std::isfinite(center.y) || !std::isfinite(nextRadius) || !std::isfinite(nextScale) ||
            nextRadius <= 0 || nextScale <= 0 || !(nextColor >> 24)) return;
        const float nextExtent = std::ceil(nextRadius + 2.25f * nextScale + 1);
        if (!std::isfinite(nextExtent) || nextExtent > 1024) return;
        const bool same = owner == device && radius == nextRadius && scale == nextScale && color == nextColor;
        if (!same || !texture) {
            const auto now = GetTickCount64();
            if (same && now < retryAt) return;
            Reset(); owner = device; radius = nextRadius; scale = nextScale; color = nextColor; extent = nextExtent; retryAt = now + 1000;
            for (side = 8; side < unsigned(2 * extent); side *= 2) {}
            UiTexture next = nullptr;
            if (FAILED(device->CreateTexture(side,side,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&next,nullptr))) return;
            D3DLOCKED_RECT pixels{};
            if (FAILED(next->LockRect(0,&pixels,nullptr,0))) { next->Release(); return; }
            for (unsigned y = 0; y < side; ++y) {
                auto* row = reinterpret_cast<uint32_t*>(static_cast<unsigned char*>(pixels.pBits) + y * pixels.Pitch);
                for (unsigned x = 0; x < side; ++x) {
                    const float dx = x + .5f - extent, dy = y + .5f - extent;
                    const float distance = std::abs(std::sqrt(dx*dx + dy*dy) - radius);
                    const float outer = std::clamp(2.25f * scale + .5f - distance,0.f,1.f) * (220.f / 255);
                    const float inner = std::clamp(1.1f * scale + .5f - distance,0.f,1.f) * ((color >> 24) / 255.f);
                    const float alpha = inner + outer * (1 - inner);
                    const float tint = alpha > 0 ? inner / alpha : 0;
                    row[x] = UI_COLOR(unsigned(((color >> 16) & 255) * tint + .5f),unsigned(((color >> 8) & 255) * tint + .5f),
                        unsigned((color & 255) * tint + .5f),unsigned(alpha * 255 + .5f));
                }
            }
            const auto unlocked = next->UnlockRect(0);
            if (FAILED(unlocked)) { next->Release(); return; }
            texture = next;
        }
        const float uv = 2 * extent / side;
        draw->AddImage(texture,{center.x-extent,center.y-extent},{center.x+extent,center.y+extent},{0,0},{uv,uv});
    }
};
