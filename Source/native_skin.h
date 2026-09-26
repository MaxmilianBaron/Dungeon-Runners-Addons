#pragma once
#include "native_skin_data.h"

class NativeSkin {
    IDirect3DTexture9* button = nullptr;
    IDirect3DTexture9* font = nullptr;
    IDirect3DTexture9* frame = nullptr;
    IDirect3DTexture9* effectFrame = nullptr;
    std::array<UiPoint,256> textInk{};
    static IDirect3DTexture9* Texture(IDirect3DDevice9* device, unsigned size, const unsigned char* bytes, bool monochrome = false) {
        IDirect3DTexture9* texture = nullptr;
        if (FAILED(device->CreateTexture(size, size, 1, 0, D3DFMT_DXT3, D3DPOOL_MANAGED, &texture, nullptr))) return nullptr;
        D3DLOCKED_RECT locked{};
        if (FAILED(texture->LockRect(0, &locked, nullptr, 0))) { texture->Release(); return nullptr; }
        const unsigned rowBytes = size * 4;
        if (locked.Pitch < static_cast<int>(rowBytes)) { texture->UnlockRect(0); texture->Release(); return nullptr; }
        for (unsigned y = 0; y < size / 4; ++y) {
            auto* row = static_cast<unsigned char*>(locked.pBits) + y * locked.Pitch;
            std::memcpy(row, bytes + y * rowBytes, rowBytes);
            if (monochrome) for (unsigned x = 0; x < rowBytes; x += 16) for (unsigned endpoint : {8u,10u}) {
                const unsigned color = row[x + endpoint] | (row[x + endpoint + 1] << 8);
                const unsigned red = ((color >> 11) & 31) * 255 / 31, green = ((color >> 5) & 63) * 255 / 63, blue = (color & 31) * 255 / 31;
                const unsigned light = std::max({red,green,blue});
                const unsigned gray = ((light >> 3) << 11) | ((light >> 2) << 5) | (light >> 3);
                row[x + endpoint] = static_cast<unsigned char>(gray);
                row[x + endpoint + 1] = static_cast<unsigned char>(gray >> 8);
            }
        }
        texture->UnlockRect(0);
        return texture;
    }
    static void Image(UiDrawList* draw, IDirect3DTexture9* texture, float extent, UiPoint a, UiPoint b, float x0, float y0, float x1, float y1, UiColor color = UI_WHITE) {
        draw->AddImage(reinterpret_cast<UiTexture>(texture), a, b, UiPoint(x0 / extent, y0 / extent), UiPoint(x1 / extent, y1 / extent), color);
    }
public:
    void Release() {
        if (button) button->Release(); if (font) font->Release(); if (frame) frame->Release(); if (effectFrame) effectFrame->Release(); button = font = frame = effectFrame = nullptr;
    }
    bool Load(IDirect3DDevice9* device) {
        if (Ready()) return true;
        Release();
        button = Texture(device, SkinButtonSize, SkinButton.data());
        font = Texture(device, SkinFontSize, SkinFont.data());
        frame = Texture(device, SkinFrameSize, SkinFrame.data());
        effectFrame = Texture(device, SkinFrameSize, SkinFrame.data(), true);
        if (!Ready()) { Release(); return false; }
        for (unsigned ch=0;ch<textInk.size();++ch) {
            unsigned top=32,bottom=0;
            const unsigned left=16-(SkinAdvance[ch]>>1);
            for (unsigned y=0;y<32;++y) for (unsigned x=left;x<32-left;++x) {
                const unsigned tx=(ch&15)*32+x,ty=(ch>>4)*32+y;
                const unsigned pixel=(ty%4)*4+tx%4;
                const size_t offset=((ty/4)*(SkinFontSize/4)+tx/4)*16+pixel/2;
                if (((SkinFont[offset]>>((pixel%2)*4))&15)>7) { top=std::min(top,y); bottom=std::max(bottom,y+1); }
            }
            textInk[ch]=UiPoint(static_cast<float>(top),static_cast<float>(bottom));
        }
        return true;
    }

    bool Ready() const { return button && font && frame && effectFrame; }
    UiTexture FontTexture() const { return font; }
    void IconFrame(UiDrawList* draw,UiPoint a,UiPoint size,float strength,const std::array<UiColor,8>& colors,bool glow) const {
        if (!Ready() || size.x <= 0 || size.y <= 0 || !std::isfinite(strength) || strength <= 0) return;
        const unsigned alpha = static_cast<unsigned>(255 * std::min(strength,1.0f));
        const float edge = std::min(size.x,size.y) * (glow ? 0.10f : 0.125f);
        const float x[] = {a.x,a.x+edge,a.x+size.x-edge,a.x+size.x};
        const float y[] = {a.y,a.y+edge,a.y+size.y-edge,a.y+size.y};
        const float u[] = {3,18,72,87}, v[] = {9,20,37,48};
        if (glow) draw->AddRectFilled(UiPoint(x[1],y[1]),UiPoint(x[2],y[2]),UI_COLOR(255,255,255,alpha / 12));
        const auto tint = glow ? UI_COLOR(255,255,255,alpha) : (colors[0] & 0xffffff) | (alpha << 24);
        for (unsigned row = 0; row < 3; ++row) for (unsigned col = 0; col < 3; ++col) if (row != 1 || col != 1) {
            const UiPoint start(x[col],y[row]), end(x[col+1],y[row+1]);
            if (!glow) draw->AddRectFilled(start,end,UI_COLOR(12,16,14,alpha));
            Image(draw,glow ? frame : effectFrame,512,start,end,u[col],v[row],u[col+1],v[row+1],tint);
        }
        const float middleX = (x[1] + x[2]) * 0.5f, middleY = (y[1] + y[2]) * 0.5f;
        const UiPoint points[] = {{x[1],y[1]},{middleX,y[1]},{x[2],y[1]},{x[2],middleY},{x[2],y[2]},{middleX,y[2]},{x[1],y[2]},{x[1],middleY}};
        const float line = std::max(1.0f,std::min(size.x,size.y) * (glow ? 0.072f : 0.035f));
        for (unsigned i = 0; i < 8; ++i) for (unsigned step = 0; step < 4; ++step) {
            const auto from = colors[i], to = colors[(i + 1) % 8];
            const auto channel = [from,to,step](unsigned shift) { return (((from >> shift) & 255) * (4 - step) + ((to >> shift) & 255) * step) / 4; };
            const auto& start = points[i]; const auto& end = points[(i + 1) % 8];
            const UiPoint first(start.x + (end.x - start.x) * step / 4,start.y + (end.y - start.y) * step / 4);
            const UiPoint last(start.x + (end.x - start.x) * (step + 1) / 4,start.y + (end.y - start.y) * (step + 1) / 4);
            if (glow) draw->AddLine(first,last,UI_COLOR(channel(16),channel(8),channel(0),alpha / 4),line * 2);
            draw->AddLine(first,last,UI_COLOR(channel(16),channel(8),channel(0),alpha),line);
        }
    }
    void IconReady(UiDrawList* draw,UiPoint a,UiPoint size,float strength) const {
        static constexpr std::array<UiColor,8> colors{0xffff6767,0xffffbc53,0xffeeed63,0xff70e477,0xff64e5e7,0xff719aff,0xffbb7dff,0xffff79cf};
        IconFrame(draw,{a.x-size.x*0.1f,a.y-size.y*0.1f},{size.x*1.2f,size.y*1.2f},strength,colors,true);
    }
    void IconEffect(UiDrawList* draw,UiPoint a,UiPoint size,float opacity,uint8_t alignment) const {
        if (alignment != 1 && alignment != 2) return;
        std::array<UiColor,8> colors;
        colors.fill(alignment == 1 ? UI_COLOR(245,72,65,255) : UI_COLOR(93,222,112,255));
        IconFrame(draw,a,size,opacity,colors,false);
    }
    void Row(UiDrawList* draw,UiPoint a,UiPoint size,UiPoint scale,float fraction,bool hovered) const {
        if (!Ready() || size.x<=0 || size.y<=0) return;
        const float edgeX=std::min(6*scale.x,size.x*0.5f),edgeY=std::min(5*scale.y,size.y*0.5f);
        const float x[]={a.x,a.x+edgeX,a.x+size.x-edgeX,a.x+size.x};
        const float y[]={a.y,a.y+edgeY,a.y+size.y-edgeY,a.y+size.y};
        const float u[]={8,14,135,141},top=hovered ? 132.0f : 84.0f;
        const float v[]={top,top+5,top+26,top+31};
        const UiPoint inside(x[1],y[1]),end(x[2],y[2]);
        Image(draw,frame,512,inside,end,u[1],v[1],u[2],v[2],UI_COLOR(55,49,39,255));
        fraction=std::isfinite(fraction) ? std::clamp(fraction,0.0f,1.0f) : 0;
        if (fraction>0) {
            draw->PushClipRect(inside,UiPoint(inside.x+(end.x-inside.x)*fraction,end.y));
            Image(draw,frame,512,inside,end,u[1],v[1],u[2],v[2]);
            draw->PopClipRect();
        }
        for (unsigned row=0;row<3;++row) for (unsigned col=0;col<3;++col)
            if (row!=1 || col!=1) Image(draw,frame,512,UiPoint(x[col],y[row]),UiPoint(x[col+1],y[row+1]),u[col],v[row],u[col+1],v[row+1]);
    }
    static float TextWidth(const char* text, float scale) {
        float width = 0;
        for (const char* c = text; *c; ++c) width += SkinAdvance[static_cast<unsigned char>(*c)];
        return width * scale;
    }
    void Text(UiDrawList* draw, const char* text, UiPoint a, UiPoint size, UiPoint scale, bool centered = false, UiColor color = UI_COLOR(251,187,6,255)) const {
        if (!Ready()) return;
        float x = a.x + (centered ? std::floor((size.x / scale.x - TextWidth(text, 1)) * 0.5f) * scale.x : 0);
        const float y = a.y + (centered ? -2.0f : -9.0f) * scale.y;
        for (const char* c = text; *c; ++c) {
            const auto ch = static_cast<unsigned char>(*c);
            const float advance = SkinAdvance[ch];
            const int inset = 16 - (SkinAdvance[ch] >> 1);
            const float u0 = (ch & 15) * 32.0f + inset - 1;
            const float u1 = ((ch & 15) + 1) * 32.0f - inset + 2;
            const float v0 = (ch >> 4) * 32.0f;
            Image(draw, font, 512.5f, UiPoint(x - scale.x, y), UiPoint(x + (advance + 2) * scale.x, y + 34 * scale.y), u0, v0, u1, v0 + 34, color);
            x += advance * scale.x;
        }
    }
    void Button(UiDrawList* draw, UiPoint a, UiPoint size, bool hovered, bool pressed, const char* text = "Addons") const {
        ButtonScaled(draw, a, size, UiPoint(size.x / 141.0f, size.y / 39.0f), hovered, pressed, text);
    }
    void AlignedText(UiDrawList* draw,const char* text,UiPoint a,UiPoint size,UiPoint scale,bool centered=false,UiColor color=UI_COLOR(251,187,6,255)) const {
        if (!Ready() || size.x<=0 || size.y<=0) return;
        float top=32,bottom=0;
        for (const char* c=text;*c;++c) {
            const auto ink=textInk[static_cast<unsigned char>(*c)];
            if (ink.x<ink.y) { top=std::min(top,ink.x); bottom=std::max(bottom,ink.y); }
        }
        const float width=TextWidth(text,1);
        if (width<=0 || bottom<=top) return;
        const float fit=std::min({1.0f,size.x/(width*scale.x),size.y/((bottom-top)*scale.y)});
        scale=UiPoint(scale.x*fit,scale.y*fit);
        const UiPoint position(a.x+(centered ? (size.x-width*scale.x)*.5f : 0),a.y+(size.y-(bottom-top)*scale.y)*.5f-(top-9)*scale.y);
        Text(draw,text,position,size,scale,false,color);
    }
    void CompactButton(UiDrawList* draw,UiPoint a,UiPoint size,UiPoint scale,bool hovered,bool pressed,const char* text) const {
        if (!Ready()) return;
        const float edge=7*scale.x,top=pressed ? 104.0f : 37.0f;
        Image(draw,button,SkinButtonSize,a,UiPoint(a.x+edge,a.y+size.y),811,top,821,top+31);
        Image(draw,button,SkinButtonSize,UiPoint(a.x+edge,a.y),UiPoint(a.x+size.x-edge,a.y+size.y),821,top,942,top+31);
        Image(draw,button,SkinButtonSize,UiPoint(a.x+size.x-edge,a.y),UiPoint(a.x+size.x,a.y+size.y),979,top,989,top+31);
        AlignedText(draw,text,UiPoint(a.x+edge,a.y),UiPoint(size.x-2*edge,size.y),UiPoint(scale.x*.8f,scale.y*.8f),true,hovered ? UI_COLOR(255,210,35,255) : UI_COLOR(251,187,6,255));
    }
    void ButtonScaled(UiDrawList* draw, UiPoint a, UiPoint size, UiPoint scale, bool hovered, bool pressed, const char* text) const {
        if (!Ready()) return;
        const float edge = 10 * scale.x, top = pressed ? 104.0f : 37.0f;
        Image(draw, button, SkinButtonSize, a, UiPoint(a.x + edge, a.y + size.y), 811, top, 821, top + 39);
        Image(draw, button, SkinButtonSize, UiPoint(a.x + edge, a.y), UiPoint(a.x + size.x - edge, a.y + size.y), 821, top, 942, top + 39);
        Image(draw, button, SkinButtonSize, UiPoint(a.x + size.x - edge, a.y), UiPoint(a.x + size.x, a.y + size.y), 979, top, 989, top + 39);
        AlignedText(draw,text,UiPoint(a.x+edge,a.y),UiPoint(size.x-2*edge,size.y*31.0f/39.0f),scale,true,hovered ? UI_COLOR(255,187,6,255) : UI_COLOR(251,187,6,255));
    }
    void Arrow(UiDrawList* draw, UiPoint a, UiPoint scale, bool right, bool pressed) const {
        if (!Ready()) return;
        const float x = (right ? 860.0f : 904.0f) + (pressed ? 21.0f : 0.0f);
        Image(draw, button, SkinButtonSize, a, UiPoint(a.x + 20 * scale.x, a.y + 20 * scale.y), x, 156, x + 20, 176);
    }
    void Tooltip(UiDrawList* draw, UiPoint a, UiPoint size, UiPoint scale) const {
        if (!Ready()) return;
        const float left = a.x + 9 * scale.x, right = a.x + size.x - 9 * scale.x;
        const float top = a.y + 8 * scale.y, bottom = a.y + size.y - 8 * scale.y;
        draw->AddRectFilled(UiPoint(left,top),UiPoint(right,bottom),UI_COLOR(0,0,0,204));
        Image(draw,frame,512,a,UiPoint(left,top),3,9,12,17);
        Image(draw,frame,512,UiPoint(left,a.y),UiPoint(right,top),20,9,40,17);
        Image(draw,frame,512,UiPoint(right,a.y),UiPoint(a.x+size.x,top),77,9,85,17);
        Image(draw,frame,512,UiPoint(a.x,top),UiPoint(left,bottom),3,20,12,35);
        Image(draw,frame,512,UiPoint(right,top),UiPoint(a.x+size.x,bottom),77,20,87,35);
        Image(draw,frame,512,UiPoint(a.x,bottom),UiPoint(left,a.y+size.y),3,40,12,47);
        Image(draw,frame,512,UiPoint(left,bottom),UiPoint(right,a.y+size.y),20,40,40,47);
        Image(draw,frame,512,UiPoint(right,bottom),UiPoint(a.x+size.x,a.y+size.y),77,40,85,47);
    }
    void Frame(UiDrawList* draw, UiPoint a, UiPoint size, UiPoint scale) const {
        if (!Ready()) return;
        const float left = a.x + 15 * scale.x, right = a.x + size.x - 15 * scale.x;
        const float top = a.y + 11 * scale.y, bottom = a.y + size.y - 11 * scale.y;
        draw->AddRectFilled(UiPoint(left,top),UiPoint(right,bottom),UI_COLOR(0,0,0,150));
        Image(draw,frame,512,a,UiPoint(left,top),3,9,18,20);
        Image(draw,frame,512.5f,UiPoint(left,a.y),UiPoint(right,top),20,9,40,20);
        Image(draw,frame,512,UiPoint(right,a.y),UiPoint(a.x+size.x,top),72,9,87,20);
        Image(draw,frame,512.5f,UiPoint(a.x,top),UiPoint(left,bottom),3,20,18,35);
        Image(draw,frame,512.5f,UiPoint(right,top),UiPoint(a.x+size.x,bottom),71,20,87,35);
        Image(draw,frame,512,UiPoint(a.x,bottom),UiPoint(left,a.y+size.y),3,37,18,48);
        Image(draw,frame,512.5f,UiPoint(left,bottom),UiPoint(right,a.y+size.y),20,37,40,48);
        Image(draw,frame,512,UiPoint(right,bottom),UiPoint(a.x+size.x,a.y+size.y),72,37,87,48);
    }
};
