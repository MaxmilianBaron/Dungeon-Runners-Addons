#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cfloat>
#include <memory>
#include <string>
#include <vector>

using UiColor = uint32_t;
using UiTexture = IDirect3DTexture9*;
constexpr UiColor UI_COLOR(unsigned r,unsigned g,unsigned b,unsigned a) { return (a<<24)|(r<<16)|(g<<8)|b; }
constexpr UiColor UI_WHITE = 0xffffffffu;
struct UiPoint { float x=0,y=0; UiPoint()=default; UiPoint(float a,float b):x(a),y(b) {} };
struct UiRect { UiPoint min,max; };
struct UiGlyph { UiTexture texture=nullptr; UiPoint uv0,uv1,offset{-2,0}; float advance=0,width=0,height=0; };
class UiFont {
    struct Data;
    std::unique_ptr<Data> data;
public:
    UiFont();
    ~UiFont();
    bool Load(IDirect3DDevice9*,const unsigned char*,size_t);
    bool LoadAtlas(UiTexture,const unsigned char*,size_t,UiFont*);
    const UiGlyph& Glyph(uint32_t);
    UiPoint CalcTextSizeA(float,float,float,const char*);
    float Height() const;
    float SizeForEm(float) const;
};
struct UiVertex { float x,y,z,rhw; UiColor color; float u,v; };
struct UiCommand { UiTexture texture; UiRect clip; size_t first,count; };
class UiDrawList {
    std::vector<UiRect> clips;
public:
    std::vector<UiVertex> vertices;
    std::vector<UiCommand> commands;
    void Clear(UiPoint);
    void PushClipRect(UiPoint,UiPoint,bool=true);
    void PopClipRect();
    void AddImage(UiTexture,UiPoint,UiPoint,UiPoint={},UiPoint={1,1},UiColor=UI_WHITE);
    void AddRectFilled(UiPoint,UiPoint,UiColor);
    void AddLine(UiPoint,UiPoint,UiColor,float=1);
    void AddText(UiFont*,float,UiPoint,UiColor,const char*,const char* = nullptr,float=0);
};
struct UiInput {
    UiPoint DisplaySize,MousePos,MouseDelta;
    bool MouseDown[3]{};
    void AddMousePosEvent(float x,float y) { MousePos={x,y}; }
};
struct UiContext;
namespace Ui {
    constexpr int NoInputs=1, NoSavedSettings=2, FirstUse=1;
    UiContext* CreateContext(HWND,IDirect3DDevice9*,const char*);
    void DestroyContext(UiContext*);
    void SetCurrentContext(UiContext*);
    UiInput& GetIO();
    UiFont* LoadFont(const unsigned char*,size_t);
    void Message(HWND,UINT,WPARAM,LPARAM);
    void NewFrame();
    void Render();
    void Flush();
    void EndFrame();
    void SetInputEnabled(bool);
    void SetNextWindowPos(UiPoint,int=0,UiPoint={});
    void SetNextWindowSize(UiPoint);
    void SetWindowPos(UiPoint);
    UiPoint GetWindowPos();
    UiPoint GetWindowSize();
    bool Begin(const char*,bool* = nullptr,int=0);
    void End();
    bool BeginChild(const char*,UiPoint,int=0,int=0);
    void EndChild();
    void BeginDisabled(bool=true);
    void EndDisabled();
    void PushID(const char*);
    void PushID(int);
    void PopID();
    UiDrawList* GetWindowDrawList();
    UiDrawList* GetForegroundDrawList();
    UiPoint GetCursorScreenPos();
    void SetCursorScreenPos(UiPoint);
    UiPoint GetItemRectMin();
    UiPoint GetItemRectMax();
    bool InvisibleButton(const char*,UiPoint);
    bool IsItemActivated();
    bool IsItemActive();
    bool IsItemHovered();
    bool IsWindowHovered(int=0);
    bool IsMouseHoveringRect(UiPoint,UiPoint);
    bool IsMouseDragging(int,float=0);
    bool IsMouseDoubleClicked(int);
    void Dummy(UiPoint);
    void ClearInput();
}
