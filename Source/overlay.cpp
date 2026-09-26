#include <windows.h>
#include <windowsx.h>
#include <d3d9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include "ui.h"
#include "overlay_protocol.h"
#include "addon_features.h"
#include "native_history.h"
#include "meter_details.h"
#include "native_skin.h"
#include "native_registry.h"
#include "cooldown_timers.h"
#include "native_ui.h"
#include "hotkey_binding.h"
#include "nameplates.h"
#include "character_sheet.h"
#include "mythic_sounds.h"



static CRITICAL_SECTION gate;
static INIT_ONCE gateOnce = INIT_ONCE_STATIC_INIT;
static MeterPacket packet{};
static MeterPacket pendingPacket{};
static uint64_t detailRevision = 0;
static uint64_t detailSkill = 0;
static std::vector<MeterDetailRow> detailRows;
static std::vector<float> detailOffsets;
static uint64_t cachedDetailRevision = UINT64_MAX, cachedDetailActor = 0;
static const PlayerRow* cachedDetailPlayer = nullptr;
static const MeterView* cachedDetailView = nullptr;
static std::string cachedDetailName;
static bool cachedDetailActors = false;
static bool detailHasLimit = false;
static UiPoint cachedDetailScale;
static DungeonHistorySnapshot historyData;
static bool historyOpen = false;
static uint64_t historySelected = 0;
static RECT historyButtonRect{}, historyFirstRect{}, historyPlayerRect{}, historyBackRect{}, historyFinishRect{}, historyReportRect{}, historyArea{};
static IDirect3DDevice9* graphics = nullptr;
static HWND gameWindow = nullptr;
static WNDPROC previousProcedure = nullptr;
static UiContext* context = nullptr;
static bool visible = true;
static bool measurementEnabled = true;
static bool worldVisible = false;
static bool nativeFrame = false;
static unsigned nativeLayers = 0;
static AddonInputTest nativeInputTest = nullptr;
static AddonHotkeyTest nativeHotkeyTest = nullptr;
static bool addonsOpen = false;
static bool goldHidden = true, draftGoldHidden = true;
static bool cooldownsEnabled = true, draftCooldownsEnabled = true;
static bool cooldownTenths = true, draftCooldownTenths = true;
static bool cooldownReady = true, draftCooldownReady = true;
static bool effectTimers = true, draftEffectTimers = true;
static CooldownFrame cooldownFrame;
static EffectFrame effectFrame;
static CooldownHighlights cooldownHighlights;
static std::filesystem::path cooldownSettings;
static std::string cooldownMessage;
static NameplateSettings nameplateOptions, draftNameplateOptions;
static std::filesystem::path nameplateSettings;
static std::string nameplateMessage;
static std::array<RECT,3> nameplateOptionRects{}, nameplateCategoryRects{};
static unsigned nameplatePage = 0;
static bool characterSheetEnabled = true, draftCharacterSheetEnabled = true;
static CharacterSheetFrame characterSheetFrame;
static std::filesystem::path characterSheetSettings;
static std::string characterSheetMessage;
static MythicSettings mythicOptions, draftMythicOptions;
static unsigned mythicPreviewVolume = 100;
static std::filesystem::path mythicSettings;
static std::string mythicMessage;
static int mythicPreview = -1;
static bool mythicFailed = false;
static bool mythicBrowse = false;
static CustomSoundStatus mythicCustomStatus = CustomSoundStatus::None;
static AddonInputTest characterInputTest = nullptr;
static NameplateSettings nameplatePageOptions;
static bool reportOpen = false, reportPending = false, reportBusy = false, reportParty = false;
static MeterReport reportPreview, reportRequest;
struct ReportChoice { std::string label; std::array<MeterReport,2> reports; };
static std::vector<ReportChoice> reportChoices;
static size_t reportChoice = 0;
static unsigned reportType = 0;
static std::array<RECT,2> reportSourceRects{}, reportTypeRects{};
static ReportContext reportDestination;
static std::string reportMessage, lootMessage;
static std::filesystem::path lootSettings;
struct AddonDefinition {
    const char* id;
    const char* name;
    const char* description;
    void (*open)();
    void (*settings)(UiPoint,UiPoint);
    const ExtensionDefinition* extension;
    const ExtensionSettings* advanced = nullptr;
};
static AddonRegistry addonRegistry;
static std::vector<AddonDefinition> registeredAddons;
static int extensionDraft[ExtensionSettingsLimit]{};
static unsigned extensionPage=0;
static bool extensionReadable=false;
static std::string extensionMessage;
static const AddonDefinition* activeAddon = nullptr;
static UiPoint logicalUiSize;
static bool consumeEscapeUp = false;
static float menuBounds[4]{};
static bool dragging = false;
static float panelWidth = 330, panelRowHeight = 25;
static bool panelResizing = false, panelSizeDirty = false, panelSizeSaveFailed = false;
static unsigned resizeAxes = 0, resizeRows = 1;
static float resizeStartRowHeight = 25;
static UiPoint resizeStartMouse, resizeStartSize, resizeStartOrigin, panelNextPosition;
static bool panelPositionPending = false;
static HCURSOR panelCursor = nullptr, previousCursor = nullptr;
static RECT panelResizeRect{}, panelWidthRect{}, panelHeightRect{}, panelLeftRect{}, panelTopRect{};
static bool panelOptionsOpen = false;
static RECT panelOptionsRect{}, panelOptionsArea{}, panelModeRect{}, panelDirectionRect{}, panelResetRect{}, panelHideRect{};
static bool shuttingDown = false;
static uint32_t selectedCharacter = 0;
static uint64_t selectedPet = 0;
static unsigned mode = 0;
static unsigned direction = 0;
static bool detailByActor = true;
static uint64_t detailActor = 0;
static std::string detailActorName;
static volatile LONG actions = 0;
static volatile LONG frames = 0;
static volatile LONG stopped = 0;
static RECT hitRects[4]{};
static unsigned hitCount = 0;
static RECT firstRowRect{};
static RECT addonsButtonRect{};
static RECT meterCheckboxRect{};
static RECT okayRect{}, cancelRect{};
static RECT libraryEntryRect{}, libraryBackRect{}, helpRect{}, detailBackRect{}, detailViewRect{}, detailCloseRect{}, detailsArea{}, panelArea{};
static RECT lootEntryRect{}, goldOptionRect{}, reportButtonRect{}, reportSendRect{}, reportCloseRect{};
static bool draftVisible = true, draftEnabled = true, draftReset = false;
static unsigned draftMode = 0;
static unsigned meterScalePercent = 100, draftMeterScalePercent = 100;
static RECT meterScaleRect{};
static RECT meterHotkeyRect{};
static HotkeyBinding visibilityHotkey, draftVisibilityHotkey;
static HotkeyInput hotkeyInput;
static std::string hotkeyMessage;
static UiFont* valueFont = nullptr;
static std::unique_ptr<UiFont> meterFont;
static ULONGLONG lastUpdate = 0;
static std::string settingsFile;
static NativeSkin nativeSkin;
static bool skinAttempted = false;
static UINT shutdownMessage = 0;
static void ShutdownUnlocked();
static void BackFromAddons();
static void BackFromDetails();
static void OpenReport(uint64_t dungeon = 0);

static void SavePreferences() {
    if (settingsFile.empty()) return;
    std::ofstream output(std::filesystem::u8path(settingsFile + ".prefs"), std::ios::trunc);
    output << (visible ? 1 : 0) << ' ' << mode << ' ' << (measurementEnabled ? 1 : 0) << ' ' << direction << ' ' << meterScalePercent << '\n';
}

static bool SaveHotkey() {
    if (settingsFile.empty() || !draftVisibilityHotkey.Valid()) return false;
    const auto path=std::filesystem::u8path(settingsFile+".hotkey");
    auto pending=path;
    pending+=L".pending";
    std::ofstream output(pending,std::ios::trunc);
    output<<draftVisibilityHotkey.key<<' '<<draftVisibilityHotkey.modifiers<<'\n';
    output.close();
    return output.good() && MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static bool CheckHotkey(const HotkeyBinding& binding) {
    const auto state=nativeHotkeyTest ? nativeHotkeyTest(binding.key,binding.modifiers,false) : AddonHotkeyState::Available;
    if (state==AddonHotkeyState::Available) return true;
    hotkeyMessage=state==AddonHotkeyState::Conflict ? "Already used by the game." : "Game key bindings unavailable.";
    return false;
}

static bool SaveLootSettings() {
    if (lootSettings.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(lootSettings.parent_path(),error);
    if (error) return false;
    auto pending = lootSettings;
    pending += L".pending";
    std::ofstream output(pending,std::ios::trunc);
    output << (draftGoldHidden ? 1 : 0) << '\n';
    output.close();
    return output.good() && MoveFileExW(pending.c_str(),lootSettings.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static bool SaveCooldownSettings() {
    if (cooldownSettings.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(cooldownSettings.parent_path(),error);
    if (error) return false;
    auto pending = cooldownSettings;
    pending += L".pending";
    std::ofstream output(pending,std::ios::trunc);
    output << (draftCooldownsEnabled ? 1 : 0) << ' ' << (draftCooldownTenths ? 1 : 0) << ' ' << (draftCooldownReady ? 1 : 0) << ' ' << (draftEffectTimers ? 1 : 0) << '\n';
    output.close();
    return output.good() && MoveFileExW(pending.c_str(),cooldownSettings.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static bool SaveNameplateSettings() {
    if (nameplateSettings.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(nameplateSettings.parent_path(),error);
    if (error) return false;
    auto pending = nameplateSettings;
    pending += L".pending";
    std::ofstream output(pending,std::ios::trunc);
    draftNameplateOptions.Save(output);
    output.close();
    return output.good() && MoveFileExW(pending.c_str(),nameplateSettings.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static bool SaveCharacterSheetSettings() {
    if (characterSheetSettings.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(characterSheetSettings.parent_path(),error);
    if (error) return false;
    auto pending = characterSheetSettings;
    pending += L".pending";
    std::ofstream output(pending,std::ios::trunc);
    output << (draftCharacterSheetEnabled ? 1 : 0) << '\n';
    output.close();
    return output.good() && MoveFileExW(pending.c_str(),characterSheetSettings.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static bool SaveMythicSettings(const MythicSettings& value = draftMythicOptions) {
    if (mythicSettings.empty() || value.sound >= std::size(MythicSounds) || value.volume > 100) return false;
    std::error_code error;
    std::filesystem::create_directories(mythicSettings.parent_path(),error);
    if (error) return false;
    auto pending = mythicSettings;
    pending += L".pending";
    std::ofstream output(pending,std::ios::trunc);
    output << (value.enabled ? 1 : 0) << ' ' << value.sound << ' ' << (value.announcements ? 1 : 0) << ' ' << value.volume << '\n';
    output.close();
    return output.good() && MoveFileExW(pending.c_str(),mythicSettings.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static void SavePanelSize() {
    if (!panelSizeDirty) return;
    panelSizeDirty = false;
    panelSizeSaveFailed = true;
    if (settingsFile.empty()) return;
    const auto path = std::filesystem::u8path(settingsFile + ".scale");
    auto pending = path;
    pending += L".pending";
    std::ofstream output(pending,std::ios::trunc);
    output.precision(9);
    output << "size " << panelWidth << ' ' << panelRowHeight << '\n';
    output.close();
    panelSizeSaveFailed = !output.good() || !MoveFileExW(pending.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}

static void RestoreResizeCursor() {
    if (panelCursor && GetCursor() == panelCursor) SetCursor(previousCursor);
    panelCursor = previousCursor = nullptr;
}

static void EndPanelResize() {
    panelResizing = false;
    resizeAxes = 0;
    RestoreResizeCursor();
    SavePanelSize();
}

static BOOL CALLBACK InitializeGate(PINIT_ONCE, PVOID, PVOID*) {
    InitializeCriticalSection(&gate);
    return TRUE;
}

struct Lock {
    Lock() { InitOnceExecuteOnce(&gateOnce, InitializeGate, nullptr, nullptr); EnterCriticalSection(&gate); }
    ~Lock() { LeaveCriticalSection(&gate); }
};

static bool MouseInside(LPARAM value) {
    POINT point{GET_X_LPARAM(value), GET_Y_LPARAM(value)};
    for (unsigned i = 0; i < hitCount; ++i) if (PtInRect(&hitRects[i], point)) return true;
    return false;
}

static bool InputAllowed(UiPoint point,bool menuLayer) {
    if (!nativeInputTest) return true;
    const auto display = Ui::GetIO().DisplaySize;
    return display.x > 0 && display.y > 0 && nativeInputTest(point.x / display.x,point.y / display.y,menuLayer);
}

static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    WNDPROC original;
    bool consume = false;
    {
        Lock lock;
        original = previousProcedure;
        if (shutdownMessage && message == shutdownMessage) {
            ShutdownUnlocked();
            InterlockedExchange(&stopped, 1);
            return 0;
        }
        if (context && !shuttingDown) {
            const auto previousDraft=draftVisibilityHotkey;
            auto binding=visibilityHotkey;
            if (!hotkeyInput.recording && worldVisible && nativeHotkeyTest && (message==WM_KEYDOWN || message==WM_SYSKEYDOWN) && wparam==binding.key && nativeHotkeyTest(binding.key,binding.modifiers,true)!=AddonHotkeyState::Available) binding={0,0};
            const auto shortcut=hotkeyInput.Message(message,wparam,lparam,worldVisible,HotkeyBinding::Modifiers(),binding,draftVisibilityHotkey);
            consume=shortcut!=HotkeyEvent::Pass;
            if (shortcut==HotkeyEvent::Toggle) {
                EndPanelResize(); panelOptionsOpen=false; visible=!visible; dragging=false;
                if (addonsOpen && activeAddon && !std::strcmp(activeAddon->id,"damage-meter")) draftVisible=visible;
                SavePreferences();
            } else if (shortcut==HotkeyEvent::Invalid) hotkeyMessage="Choose another key or combination.";
            else if (shortcut==HotkeyEvent::Changed) {
                if (CheckHotkey(draftVisibilityHotkey)) hotkeyMessage.clear();
                else { draftVisibilityHotkey=previousDraft; hotkeyInput.recording=true; }
            } else if (shortcut==HotkeyEvent::Cancelled || message==WM_KILLFOCUS) hotkeyMessage.clear();
        }
        if (context && !shuttingDown && worldVisible && !consume) {
            Ui::SetCurrentContext(context);
            if (message == WM_KEYDOWN && wparam == VK_ESCAPE && (panelOptionsOpen || addonsOpen || reportOpen || historyOpen || (visible && selectedCharacter) || consumeEscapeUp)) {
                if (!(lparam & (1LL << 30)) && !consumeEscapeUp) {
                    if (reportOpen) reportOpen = false;
                    else if (addonsOpen) BackFromAddons();
                    else if (selectedCharacter && (visible || historyOpen)) BackFromDetails();
                    else if (historyOpen) { if (historySelected) historySelected=0; else historyOpen=false; }
                    else panelOptionsOpen = false;
                }
                consumeEscapeUp = true;
                consume = true;
            } else if (message == WM_KEYUP && wparam == VK_ESCAPE && consumeEscapeUp) {
                consumeEscapeUp = false;
                consume = true;
            } else {
                const bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN || message == WM_LBUTTONDBLCLK;
                const bool up = message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP || message == WM_XBUTTONUP;
                LPARAM position = lparam;
                if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
                    POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                    ScreenToClient(window, &point);
                    position = MAKELPARAM(point.x, point.y);
                }
                const POINT pointer{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
                const bool mouse = message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
                const bool allowed = !mouse || InputAllowed({static_cast<float>(pointer.x),static_cast<float>(pointer.y)},addonsOpen || PtInRect(&addonsButtonRect,pointer));
                if (!allowed) { Ui::ClearInput(); dragging = false; EndPanelResize(); }
                const bool dismissOptions = allowed && down && panelOptionsOpen && !PtInRect(&panelArea,pointer);
                if (dismissOptions) { panelOptionsOpen = false; dragging = true; Ui::ClearInput(); }
                const bool inside = allowed && MouseInside(position);
                if (down && inside) dragging = true;
                consume = mouse && allowed && (reportOpen || inside || dragging);
                if (consume && (down || up))
                    Ui::GetIO().AddMousePosEvent(static_cast<float>(GET_X_LPARAM(position)), static_cast<float>(GET_Y_LPARAM(position)));
                if (!dismissOptions && (consume || message == WM_MOUSEMOVE || message == WM_MOUSELEAVE || message == WM_KILLFOCUS || message == WM_SETFOCUS))
                    Ui::Message(window, message, wparam, lparam);
                if (up) dragging = false;
                if (message == WM_KILLFOCUS || message == WM_CAPTURECHANGED) { dragging = false; EndPanelResize(); }
                if (message == WM_KILLFOCUS) panelOptionsOpen = false;
            }
        }
    }
    return consume ? 0 : original ? CallWindowProcW(original, window, message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
}

static void ShutdownUnlocked() {
    std::vector<MeterDetailRow>().swap(detailRows); std::vector<float>().swap(detailOffsets); cachedDetailRevision = UINT64_MAX;
    EndPanelResize();
    hotkeyInput.Reset();
    if (!context) return;
    Ui::SetCurrentContext(context);
    if (gameWindow && previousProcedure && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(gameWindow, GWLP_WNDPROC)) == WindowProcedure)
        SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(previousProcedure));
    nativeSkin.Release();
    meterFont.reset();
    skinAttempted = false;
    Ui::DestroyContext(context);
    context = nullptr;
    valueFont = nullptr;
    graphics = nullptr;
    gameWindow = nullptr;
    hitCount = 0;
    dragging = false;
    addonsOpen = false;
    panelOptionsOpen = false;
}

static bool InitializeGraphics(IDirect3DDevice9* device) {
    D3DDEVICE_CREATION_PARAMETERS creation{};
    D3DVIEWPORT9 viewport{};
    if (FAILED(device->GetCreationParameters(&creation)) || FAILED(device->GetViewport(&viewport))) return false;
    DWORD owner = 0;
    GetWindowThreadProcessId(creation.hFocusWindow, &owner);
    if (!creation.hFocusWindow || owner != GetCurrentProcessId()) return false;
    graphics = device;
    gameWindow = creation.hFocusWindow;
    context = Ui::CreateContext(gameWindow,device,settingsFile.c_str());
    Ui::SetCurrentContext(context);
    valueFont = Ui::LoadFont(SkinSylfaen.data(),SkinSylfaen.size());
    if (!valueFont) { ShutdownUnlocked(); return false; }
    SetLastError(0);
    const LONG_PTR result = SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProcedure));
    if (!result && GetLastError()) {
        ShutdownUnlocked();
        return false;
    }
    previousProcedure = reinterpret_cast<WNDPROC>(result);
    return true;
}

static void RegisterHitArea() {
    if (hitCount >= std::size(hitRects)) return;
    const UiPoint pos = Ui::GetWindowPos();
    const UiPoint size = Ui::GetWindowSize();
    hitRects[hitCount++] = RECT{static_cast<LONG>(pos.x), static_cast<LONG>(pos.y), static_cast<LONG>(pos.x + size.x), static_cast<LONG>(pos.y + size.y)};
}

static constexpr UiColor BodyColor = UI_COLOR(244, 238, 216, 255);
static constexpr UiColor MutedColor = UI_COLOR(185, 172, 143, 255);
static constexpr UiColor GoldColor = UI_COLOR(251, 187, 6, 255);
static constexpr int SurfaceFlags = 0;

struct HelpRequest {
    std::string text;
    UiPoint position, scale;
    float otherEdge = 0;
    bool native = false;
};
static HelpRequest help;

static RECT Rectangle(UiPoint a, UiPoint size) {
    return RECT{static_cast<LONG>(a.x), static_cast<LONG>(a.y), static_cast<LONG>(a.x + size.x), static_cast<LONG>(a.y + size.y)};
}

static RECT ItemRectangle() {
    const UiPoint a = Ui::GetItemRectMin(), b = Ui::GetItemRectMax();
    return Rectangle(a, UiPoint(b.x - a.x, b.y - a.y));
}

static UiPoint GameScale() {
    const UiPoint display = Ui::GetIO().DisplaySize;
    if (logicalUiSize.x > 0 && logicalUiSize.y > 0)
        return UiPoint(display.x / logicalUiSize.x, display.y / logicalUiSize.y);
    if (menuBounds[2] > 0 && menuBounds[3] > 0)
        return UiPoint(menuBounds[2] * display.x / 141, menuBounds[3] * display.y / 39);
    const float scale = std::clamp(display.y / 1080.0f, 1.0f, 2.0f);
    return UiPoint(scale, scale);
}

static UiPoint FitScale(UiPoint scale, UiPoint logicalSize) {
    const UiPoint display = Ui::GetIO().DisplaySize;
    const float factor = std::min({scale.x, scale.y, (display.x - 16) / logicalSize.x, (display.y - 16) / logicalSize.y});
    return UiPoint(factor, factor);
}

static UiPoint MeterScale(UiPoint logicalSize) {
    const auto scale = GameScale();
    const float zoom = meterScalePercent / 100.0f;
    return FitScale(UiPoint(scale.x * zoom, scale.y * zoom), logicalSize);
}

static UiPoint At(UiPoint origin, UiPoint scale, float x, float y) {
    return UiPoint(origin.x + x * scale.x, origin.y + y * scale.y);
}

static void BodyText(UiDrawList* draw, const char* text, UiPoint position, UiPoint scale, UiColor color = BodyColor, float wrap = 0) {
    draw->AddText(valueFont, 14 * scale.y, UiPoint(position.x + scale.x * 0.6f, position.y + scale.y * 0.6f), UI_COLOR(0,0,0,220), text, nullptr, wrap);
    draw->AddText(valueFont, 14 * scale.y, position, color, text, nullptr, wrap);
}

static UiPoint MeterTextSize(const char* text,UiPoint scale,float wrap=0) {
    return meterFont->CalcTextSizeA(14*scale.y,FLT_MAX,wrap,text);
}

static void MeterText(UiDrawList* draw,const char* text,UiPoint position,UiPoint scale,UiColor color=BodyColor,float wrap=0) {
    draw->AddText(meterFont.get(),14*scale.y,UiPoint(std::round(position.x),std::round(position.y)),color,text,nullptr,wrap);
}

static void RightMeterText(UiDrawList* draw,const char* text,UiPoint right,UiPoint scale,UiColor color=BodyColor,float width=0) {
    const float actual=MeterTextSize(text,scale).x;
    if (width>0 && actual>width) { const float factor=width/actual; scale.x*=factor; scale.y*=factor; }
    MeterText(draw,text,UiPoint(right.x-MeterTextSize(text,scale).x,right.y),scale,color);
}

static std::string MeterName(const char* text,UiPoint scale,float width) {
    std::string name(text);
    if (MeterTextSize(text,scale).x<=width) return name;
    while (!name.empty()) {
        size_t end=name.size()-1;
        while (end && (static_cast<unsigned char>(name[end])&0xc0)==0x80) --end;
        name.resize(end);
        if (MeterTextSize((name+"...").c_str(),scale).x<=width) break;
    }
    return name+"...";
}

static void Heading(UiDrawList* draw, const char* text, UiPoint position, UiPoint scale, float width, float factor = 1) {
    nativeSkin.Text(draw, text, position, UiPoint(width * scale.x, 24 * scale.y), UiPoint(scale.x * factor, scale.y * factor));
}

static bool HoverArea(UiPoint a, UiPoint size) {
    return Ui::IsWindowHovered(0) && Ui::IsMouseHoveringRect(a, UiPoint(a.x + size.x, a.y + size.y));
}

static void QueueHelp(const char* text, UiPoint right, UiPoint scale, float left, bool native = false) {
    help = {text, right, scale, left, native};
}

static void DrawHelp() {
    helpRect = {};
    if (help.text.empty()) return;
    const UiPoint display = Ui::GetIO().DisplaySize;
    const UiPoint scale = FitScale(help.scale, UiPoint(205, 300));
    const float width = (help.native ? 190 : 205) * scale.x, padding = (help.native ? 11 : 15) * scale.x;
    const float textSize = help.native ? valueFont->SizeForEm(14 * scale.y) : 14 * scale.y;
    const UiPoint extent = valueFont->CalcTextSizeA(textSize, FLT_MAX, width - padding * 2, help.text.c_str());
    const UiPoint size(width, extent.y + (help.native ? 12 : 28) * scale.y);
    UiPoint position = help.position;
    if (position.x + size.x > display.x - 8) position.x = help.otherEdge - size.x - 5 * scale.x;
    position.x = std::clamp(position.x, 8.0f, std::max(8.0f, display.x - size.x - 8));
    position.y = std::clamp(position.y, 8.0f, std::max(8.0f, display.y - size.y - 8));
    UiDrawList* draw = Ui::GetForegroundDrawList();
    if (help.native) {
        nativeSkin.Tooltip(draw, position, size, scale);
        draw->AddText(valueFont,textSize,At(position,scale,11,6),UI_WHITE,help.text.c_str(),nullptr,width - padding * 2);
    } else {
        nativeSkin.Frame(draw, position, size, scale);
        BodyText(draw, help.text.c_str(), At(position, scale, 15, 14), scale, UI_WHITE, width - padding * 2);
    }
    helpRect = Rectangle(position, size);
}

static bool SkinControl(const char* id, const char* label, UiPoint position, UiPoint size, UiPoint textScale, RECT* bounds = nullptr) {
    Ui::SetCursorScreenPos(position);
    const bool clicked = Ui::InvisibleButton(id, size);
    if (bounds) *bounds = ItemRectangle();
    nativeSkin.ButtonScaled(Ui::GetWindowDrawList(), position, size, textScale, Ui::IsItemHovered(), Ui::IsItemActive(), label);
    return clicked;
}

static bool DrawSkinControl(const char* label, UiPoint position, UiPoint scale, RECT* bounds = nullptr) {
    return SkinControl(label, label, position, UiPoint(141 * scale.x, 39 * scale.y), scale, bounds);
}

static UiPoint MoveSurface(UiPoint size, UiPoint scale, float titleWidth, float titleHeight = 25) {
    UiPoint position = Ui::GetWindowPos();
    Ui::SetCursorScreenPos(At(position, scale, 15, 10));
    Ui::InvisibleButton("##move", UiPoint(titleWidth * scale.x, titleHeight * scale.y));
    if (Ui::IsItemActive() && Ui::IsMouseDragging(0, 0)) {
        position.x += Ui::GetIO().MouseDelta.x;
        position.y += Ui::GetIO().MouseDelta.y;
    }
    const UiPoint display = Ui::GetIO().DisplaySize;
    position.x = std::clamp(position.x, 0.0f, std::max(0.0f, display.x - size.x));
    position.y = std::clamp(position.y, 0.0f, std::max(0.0f, display.y - size.y));
    Ui::SetWindowPos(position);
    return position;
}

static void ResizePanel(UiPoint origin,UiPoint size,UiPoint scale,unsigned rows,float rowHeight,float headerHeight,float footer) {
    const UiPoint corner(std::max(8.0f,12*scale.x),std::max(8.0f,12*scale.y));
    const UiPoint edge(std::max(5.0f,7*scale.x),std::max(5.0f,7*scale.y));
    const UiInput& io = Ui::GetIO();
    if (panelResizing && resizeRows != rows) EndPanelResize();
    bool handleActive = false;
    HCURSOR cursor = nullptr;
    auto handle = [&](const char* id,UiPoint at,UiPoint extent,unsigned axes,RECT& bounds) {
        Ui::SetCursorScreenPos(at);
        Ui::InvisibleButton(id,extent);
        bounds = ItemRectangle();
        const bool hovered = Ui::IsItemHovered(), active = Ui::IsItemActive();
        const bool left = (axes & 4) != 0, top = (axes & 8) != 0;
        handleActive |= active;
        if (hovered && Ui::IsMouseDoubleClicked(0)) {
            panelWidth = 330;
            panelRowHeight = 25;
            panelSizeDirty = true;
            EndPanelResize();
        } else if (Ui::IsItemActivated()) {
            panelResizing = true;
            resizeAxes = axes;
            resizeRows = rows;
            resizeStartMouse = io.MousePos;
            resizeStartOrigin = origin;
            resizeStartSize = size;
            resizeStartRowHeight = rowHeight;
        }
        if (panelResizing && active && resizeAxes == axes) {
            const float roomX = left ? resizeStartOrigin.x+resizeStartSize.x : io.DisplaySize.x-resizeStartOrigin.x;
            const float roomY = top ? resizeStartOrigin.y+resizeStartSize.y : io.DisplaySize.y-resizeStartOrigin.y;
            const float widthLimit = std::max(240.0f,std::min(1200.0f,roomX/scale.x));
            const float rowLimit = std::max(18.0f,std::min(160.0f,(roomY/scale.y-headerHeight-footer)/rows));
            UiPoint nextPosition = resizeStartOrigin;
            if (axes & 1) {
                const float delta = (io.MousePos.x-resizeStartMouse.x)*(left ? -1.0f : 1.0f);
                const float next = std::clamp((resizeStartSize.x+delta)/scale.x,240.0f,widthLimit);
                if (panelWidth != next) { panelWidth = next; panelSizeDirty = true; }
                if (left) nextPosition.x += resizeStartSize.x-next*scale.x;
            }
            if (axes & 2) {
                const float delta = (io.MousePos.y-resizeStartMouse.y)*(top ? -1.0f : 1.0f);
                const float next = std::clamp(resizeStartRowHeight+delta/(scale.y*resizeRows),18.0f,rowLimit);
                if (panelRowHeight != next) { panelRowHeight = next; panelSizeDirty = true; }
                if (top) nextPosition.y += resizeStartSize.y-(headerHeight+next*rows+footer)*scale.y;
            }
            if (left || top) { panelNextPosition = nextPosition; panelPositionPending = true; }
        }
        if (hovered || active) {
            const unsigned dimensions = axes & 3;
            cursor = LoadCursorW(nullptr,dimensions == 1 ? IDC_SIZEWE : dimensions == 2 ? IDC_SIZENS : left == top ? IDC_SIZENWSE : IDC_SIZENESW);
            QueueHelp(dimensions == 1 ? "Drag to change width only. Double-click to restore the default size." : dimensions == 2 ? "Drag to change player bar height. The panel still follows party size. Double-click to restore the default size." : "Drag to change width and height independently. Text fits the player bars. Double-click to restore the default size.",UiPoint(origin.x+size.x+5*scale.x,at.y),scale,origin.x);
        }
    };
    RECT unused{};
    handle("##resize-top-left",origin,corner,15,unused);
    handle("##resize-top-right",UiPoint(origin.x+size.x-corner.x,origin.y),corner,11,unused);
    handle("##resize-bottom-left",UiPoint(origin.x,origin.y+size.y-corner.y),corner,7,unused);
    handle("##resize-panel",UiPoint(origin.x+size.x-corner.x,origin.y+size.y-corner.y),corner,3,panelResizeRect);
    handle("##resize-width",UiPoint(origin.x+size.x-edge.x,origin.y+corner.y),UiPoint(edge.x,size.y-2*corner.y),1,panelWidthRect);
    handle("##resize-left",UiPoint(origin.x,origin.y+corner.y),UiPoint(edge.x,size.y-2*corner.y),5,panelLeftRect);
    handle("##resize-height",UiPoint(origin.x+corner.x,origin.y+size.y-edge.y),UiPoint(size.x-2*corner.x,edge.y),2,panelHeightRect);
    handle("##resize-top",UiPoint(origin.x+corner.x,origin.y),UiPoint(size.x-2*corner.x,edge.y),10,panelTopRect);
    if (panelResizing && (!handleActive || !io.MouseDown[0])) EndPanelResize();
    if (cursor) {
        if (!panelCursor) previousCursor = GetCursor();
        panelCursor = cursor;
        SetCursor(cursor);
    } else RestoreResizeCursor();
}

static UiColor DamageColor(const char* type) {
    if (!std::strcmp(type,"Fire")) return UI_COLOR(255,169,104,255);
    if (!std::strcmp(type,"Ice")) return UI_COLOR(158,219,255,255);
    if (!std::strcmp(type,"Poison")) return UI_COLOR(173,227,123,255);
    if (!std::strcmp(type,"Shadow")) return UI_COLOR(209,176,255,255);
    if (!std::strcmp(type,"Divine")) return UI_COLOR(255,235,150,255);
    return BodyColor;
}

struct MeterDisplayRow {
    const PlayerRow* data;
    uint64_t pet;
    const char* owner;
};

static std::vector<MeterDisplayRow> DisplayRows(const MeterView& view) {
    std::vector<MeterDisplayRow> rows;
    rows.reserve(view.playerCount+view.petCount);
    for (unsigned i=0;i<view.playerCount;++i) {
        const auto& owner=view.players[i];
        rows.push_back({&owner,0,owner.name});
        for (unsigned j=0;j<view.petCount;++j) {
            const auto& pet=view.pets[j];
            if (pet.data.characterId==owner.characterId) rows.push_back({&pet.data,pet.key,owner.name});
        }
    }
    return rows;
}

static void SelectRow(const MeterDisplayRow& row) {
    detailSkill = 0;
    selectedCharacter=row.data->characterId;
    selectedPet=row.pet;
    detailByActor=direction!=0;
    detailActor=0;
    detailActorName.clear();
}

static void BackFromDetails() {
    if (detailSkill) { detailSkill = 0; return; }
    if (detailByActor || (!direction && detailActorName.empty())) { selectedCharacter = 0; selectedPet = 0; }
    detailByActor = true;
    detailActor = 0;
    detailActorName.clear();
}

static void CriticalRateText(char* text,size_t capacity,const char* label,uint32_t critical,uint32_t hits) {
    if (hits) std::snprintf(text,capacity,"%s: %u/%u (%.1f%%)",label,critical,hits,100.0*critical/hits);
    else std::snprintf(text,capacity,"%s: 0/0 (-)",label);
}

static void DrawDetails(const MeterView& view,const char* period=nullptr) {
    const PlayerRow* player = nullptr;
    if (selectedPet) {
        for (unsigned i=0;i<view.petCount;++i) if (view.pets[i].key==selectedPet && view.pets[i].data.characterId==selectedCharacter) player=&view.pets[i].data;
    } else {
        for (unsigned i=0;i<view.playerCount;++i) if (view.players[i].characterId==selectedCharacter) player=&view.players[i];
    }
    if (!player) { selectedCharacter = 0; selectedPet = 0; detailSkill = 0; return; }
    const UiPoint display = Ui::GetIO().DisplaySize;
    const float detailHeight = detailSkill ? 490.0f : 420.0f;
    const UiPoint scale = MeterScale(UiPoint(720,detailHeight));
    const UiPoint size(720 * scale.x, detailHeight * scale.y);
    Ui::SetNextWindowSize(size);
    Ui::SetNextWindowPos(UiPoint(display.x * 0.5f, display.y * 0.5f), Ui::FirstUse, UiPoint(0.5f,0.5f));
    bool back = false, close = false;
    const bool showView = !detailSkill && (detailByActor || (!direction && detailActorName.empty()));
    const bool showBack = !showView || historyOpen;
    detailBackRect = detailViewRect = {};
    if (Ui::Begin("Damage details", nullptr, SurfaceFlags | ((addonsOpen || reportOpen) ? Ui::NoInputs : 0))) {
        const UiPoint origin = MoveSurface(size, scale, showBack && showView ? 350.0f : 450.0f);
        detailsArea = Rectangle(origin, size);
        auto point = [origin,scale](float x, float y) { return At(origin,scale,x,y); };
        UiDrawList* draw = Ui::GetWindowDrawList();
        nativeSkin.Frame(draw, origin, size, scale);
        Heading(draw, direction ? "Damage Taken" : "Damage Done", point(23,17), scale, 350);
        if (showBack) {
            back = SkinControl("detail-back","Back",point(showView ? 378.0f : 478.0f,10),UiPoint((showView ? 90.0f : 142.0f) * scale.x,29.25f * scale.y),UiPoint(scale.x * 0.72f,scale.y * 0.72f),&detailBackRect);
            if (Ui::IsItemHovered()) QueueHelp(detailSkill ? "Returns to the skill list. Escape does the same." : showView ? "Returns to this dungeon's players. Escape does the same." : "Returns to the attacker or target list. Escape does the same.",point(725,10),scale,origin.x);
        }
        if (showView) {
            const bool showSkills = detailByActor;
            if (SkinControl("detail-view",showSkills ? "Skills" : "Targets",point(478,10),UiPoint(142 * scale.x,29.25f * scale.y),UiPoint(scale.x * 0.72f,scale.y * 0.72f),&detailViewRect)) { detailByActor=!detailByActor; detailActor=0; detailActorName.clear(); }
            if (Ui::IsItemHovered()) QueueHelp(showSkills ? "Shows skills across all attackers or targets in the selected view." : "Shows targets in the selected view. Click a target for its skill breakdown.",point(725,10),scale,origin.x);
        }
        close = SkinControl("close", "Close", point(630,10), UiPoint(72 * scale.x,29.25f * scale.y), UiPoint(scale.x * 0.75f,scale.y * 0.75f), &detailCloseRect);
        if (Ui::IsItemHovered()) QueueHelp("Closes this window and returns to the damage meter.",point(725,10),scale,origin.x);
        char summary[192];
        std::snprintf(summary,sizeof(summary),"%s  |  %s damage  |  %.0f DPS",player->name,FormatDamage(player->damage).c_str(),player->dps);
        MeterText(draw,summary,point(23,49),scale);
        std::snprintf(summary,sizeof(summary),"%s  |  %.1f seconds  |  %u hits",period ? period : mode ? "Overall" : "Current fight",view.duration,player->hits);
        MeterText(draw,summary,point(23,69),scale,MutedColor);
        const auto& crit = player->critical;
        const uint64_t samples = uint64_t(crit.weaponHits)+crit.magicHits+crit.otherHits;
        const bool ratesKnown = crit.known == player->hits && samples+crit.reflectedHits == crit.known;
        if (ratesKnown) {
            CriticalRateText(summary,sizeof(summary),"Critical hits",crit.total,static_cast<uint32_t>(samples));
            MeterText(draw,summary,point(23,89),scale,MutedColor);
            if (crit.otherHits) {
                CriticalRateText(summary,sizeof(summary),"Other",crit.total-crit.weapon-crit.magic,crit.otherHits);
                MeterText(draw,summary,point(370,89),scale,MutedColor);
            }
            CriticalRateText(summary,sizeof(summary),"Weapon",crit.weapon,crit.weaponHits);
            MeterText(draw,summary,point(23,109),scale,MutedColor);
            CriticalRateText(summary,sizeof(summary),"Magic",crit.magic,crit.magicHits);
            MeterText(draw,summary,point(370,109),scale,MutedColor);
        } else {
            if (crit.known == player->hits) std::snprintf(summary,sizeof(summary),"Critical hits: %u | Rates not recorded in this history",crit.total);
            else std::snprintf(summary,sizeof(summary),"Critical hits: not fully recorded");
            MeterText(draw,summary,point(23,89),scale,MutedColor);
        }
        if (HoverArea(point(23,86),UiPoint(670*scale.x,40*scale.y)))
            QueueHelp("Critical hits / damaging hits, per category. Reflection excluded. Other = unclassified. Damage components count separately.",point(725,89),scale,origin.x);
        if (detailSkill) {
            MeterDetailRow skill;
            if (MeterSkillDetail(view,*player,detailSkill,detailActor,detailActorName,skill)) {
                const auto& c = skill.critical;
                draw->AddRectFilled(point(15,135),point(705,179),UI_COLOR(60,38,8,195));
                Heading(draw,skill.name,point(23,143),scale,655,0.82f);
                std::snprintf(summary,sizeof(summary),"%s / %s",skill.component,skill.damageType);
                MeterText(draw,summary,point(23,188),scale,DamageColor(skill.damageType));
                std::snprintf(summary,sizeof(summary),"%s damage  |  %.0f DPS  |  %u hits",FormatDamage(skill.damage).c_str(),skill.dps,skill.hits);
                MeterText(draw,summary,point(23,214),scale);
                const uint64_t count = uint64_t(c.weaponHits)+c.magicHits+c.otherHits;
                if (c.known == skill.hits && count+c.reflectedHits == c.known) {
                    CriticalRateText(summary,sizeof(summary),"Critical hits",c.total,static_cast<uint32_t>(count));
                    MeterText(draw,summary,point(23,247),scale,GoldColor);
                    CriticalRateText(summary,sizeof(summary),"Weapon",c.weapon,c.weaponHits);
                    MeterText(draw,summary,point(23,282),scale);
                    CriticalRateText(summary,sizeof(summary),"Magic",c.magic,c.magicHits);
                    MeterText(draw,summary,point(370,282),scale);
                    CriticalRateText(summary,sizeof(summary),"Other",c.total-c.weapon-c.magic,c.otherHits);
                    MeterText(draw,summary,point(23,312),scale);
                    if (c.reflectedHits) {
                        std::snprintf(summary,sizeof(summary),"Reflection: %u hits (excluded)",c.reflectedHits);
                        MeterText(draw,summary,point(370,312),scale,MutedColor);
                    }
                } else MeterText(draw,"Critical hits: not recorded for this skill",point(23,247),scale,MutedColor);
                draw->AddRectFilled(point(15,337),point(705,364),UI_COLOR(60,38,8,195));
                Heading(draw,"Hit",point(23,342),scale,130,0.72f);
                Heading(draw,"Normal",point(240,342),scale,200,0.72f);
                Heading(draw,"Critical",point(475,342),scale,200,0.72f);
                const bool valuesKnown = uint64_t(skill.values.normal.hits)+skill.values.critical.hits == skill.hits;
                const char* labels[] = {"Average hit","Lowest hit","Highest hit"};
                const HitValues* groups[] = {&skill.values.normal,&skill.values.critical};
                for (unsigned row=0;row<3;++row) {
                    const float y = 372.0f+row*27.0f;
                    MeterText(draw,labels[row],point(23,y),scale,MutedColor);
                    for (unsigned column=0;column<2;++column) {
                        const auto& values = *groups[column];
                        if (!valuesKnown) std::snprintf(summary,sizeof(summary),"Not recorded");
                        else if (!values.hits) std::snprintf(summary,sizeof(summary),"-");
                        else {
                            const double value = row == 0 ? static_cast<double>(values.amount)/values.hits : row == 1 ? values.lowest : values.highest;
                            std::snprintf(summary,sizeof(summary),"%.2f",value/256.0);
                        }
                        MeterText(draw,summary,point(column ? 475.0f : 240.0f,y),scale);
                    }
                }
            } else MeterText(draw,"No damage recorded for this skill in the selected fight.",point(23,188),scale,MutedColor);
        } else {
        draw->AddRectFilled(point(15,135),point(705,162),UI_COLOR(60,38,8,195));
        const float columns[] = {23,232,376,465,545,626};
        const char* headers[] = {detailByActor ? (direction ? "Attacker" : "Target") : "Attack / Skill",detailByActor ? "Source" : "Component","Type","Damage","DPS","Share"};
        for (unsigned i=0;i<6;++i) Heading(draw,headers[i],point(columns[i],141),scale,100,0.72f);
        Ui::SetCursorScreenPos(point(17,163));
        if (Ui::BeginChild("damage-sources",UiPoint(686 * scale.x,223 * scale.y),0,0 | ((addonsOpen || reportOpen) ? Ui::NoInputs : 0))) {
            if (cachedDetailRevision!=detailRevision || cachedDetailPlayer!=player || cachedDetailView!=&view || cachedDetailActors!=detailByActor || cachedDetailActor!=detailActor || cachedDetailName!=detailActorName || cachedDetailScale.x!=scale.x || cachedDetailScale.y!=scale.y) {
                MeterDetailRows(view,*player,detailByActor,detailActor,detailActorName,detailRows);
                detailHasLimit=std::any_of(detailRows.begin(),detailRows.end(),[](const auto& row) { return row.limited; });
                detailOffsets.clear(); detailOffsets.push_back(0);
                for (const auto& entry:detailRows) {
                    const float textHeight=std::max(MeterTextSize(entry.name,scale,194*scale.x).y,MeterTextSize(entry.component,scale,134*scale.x).y);
                    detailOffsets.push_back(detailOffsets.back()+std::max(25*scale.y,textHeight+9*scale.y));
                }
                cachedDetailRevision=detailRevision; cachedDetailPlayer=player; cachedDetailView=&view; cachedDetailActors=detailByActor;
                cachedDetailActor=detailActor; cachedDetailName=detailActorName; cachedDetailScale=scale;
            }
            const auto& rows=detailRows;
            const UiPoint listStart=Ui::GetCursorScreenPos(),clipTop=Ui::GetWindowPos(),clipSize=Ui::GetWindowSize();
            size_t first=static_cast<size_t>(std::upper_bound(detailOffsets.begin(),detailOffsets.end(),clipTop.y-listStart.y)-detailOffsets.begin());
            if (first) --first;
            first=std::min(first,rows.size());
            size_t last=static_cast<size_t>(std::lower_bound(detailOffsets.begin(),detailOffsets.end(),clipTop.y+clipSize.y-listStart.y)-detailOffsets.begin());
            last=std::min(last,rows.size());
            Ui::Dummy(UiPoint(0,detailOffsets[first]));
            for (size_t i=first;i<last;++i) {
                const MeterDetailRow& skill = rows[i];
                const float height=detailOffsets[i+1]-detailOffsets[i];
                const UiPoint row = Ui::GetCursorScreenPos();
                Ui::PushID(static_cast<int>(i));
                if (Ui::InvisibleButton("source",UiPoint(673 * scale.x,height)) && !skill.limited) {
                    if (detailByActor) { detailByActor=false; detailActor=skill.counterpartId; detailActorName=skill.counterpart; }
                    else detailSkill=skill.key;
                }
                UiDrawList* list = Ui::GetWindowDrawList();
                const bool hovered = Ui::IsItemHovered();
                const float fraction = player->damage > 0 ? static_cast<float>(std::min(1.0,skill.damage / player->damage)) : 0;
                nativeSkin.Row(list,row,UiPoint(673*scale.x,height-scale.y),scale,fraction,hovered);
                MeterText(list,skill.name,UiPoint(origin.x+25 * scale.x,row.y+5 * scale.y),scale,BodyColor,192 * scale.x);
                MeterText(list,skill.component,UiPoint(origin.x+232 * scale.x,row.y+5 * scale.y),scale,MutedColor,134 * scale.x);
                MeterText(list,skill.damageType,UiPoint(origin.x+376 * scale.x,row.y+5 * scale.y),scale,DamageColor(skill.damageType));
                char number[64];
                RightMeterText(list,FormatDamage(skill.damage).c_str(),UiPoint(origin.x+530 * scale.x,row.y+5 * scale.y),scale,BodyColor,76*scale.x);
                std::snprintf(number,sizeof(number),"%.0f",skill.dps);
                RightMeterText(list,number,UiPoint(origin.x+607 * scale.x,row.y+5 * scale.y),scale,BodyColor,68*scale.x);
                std::snprintf(number,sizeof(number),"%.1f%%",100 * fraction);
                RightMeterText(list,number,UiPoint(origin.x+681 * scale.x,row.y+5 * scale.y),scale,GoldColor,65*scale.x);
                if (hovered) {
                    char description[512];
                    const char* identity = skill.limited ? "The detail capacity was reached. Total damage and hits are preserved; these individual sources were not recorded.\n" : !std::strcmp(skill.name,"Other effects") ? "This is a combined row from older history. Its original skill breakdown was not recorded.\n" : !std::strcmp(skill.name,"Additional damage") ? "No named skill is attached to this damage event. It is not classified as a melee/ranged weapon hit or reflection; the exact item or effect is unknown.\n" : "";
                    char critical[160]{};
                    const auto& c = skill.critical;
                    const uint64_t count = uint64_t(c.weaponHits)+c.magicHits+c.otherHits;
                    if (!detailByActor && !skill.limited) {
                        if (c.known == skill.hits && count+c.reflectedHits == c.known)
                            CriticalRateText(critical,sizeof(critical),"Critical hits",c.total,static_cast<uint32_t>(count));
                        else std::snprintf(critical,sizeof(critical),"Critical hits: not recorded");
                    }
                    std::snprintf(description,sizeof(description),"%s\n%s / %s\n%u hits, %s damage.\n%s%s%s%s",skill.name,skill.component,skill.damageType,skill.hits,FormatDamage(skill.damage).c_str(),identity,critical,critical[0] ? "\n" : "",skill.limited ? "" : detailByActor ? "Click for skills." : "Click for hit details.");
                    QueueHelp(description,UiPoint(origin.x+size.x+5 * scale.x,row.y),scale,origin.x);
                }
                Ui::PopID();
            }
            Ui::Dummy(UiPoint(0,detailOffsets.back()-detailOffsets[last]));
            if (rows.empty()) BodyText(Ui::GetWindowDrawList(),"No damage recorded in this view.",Ui::GetCursorScreenPos(),scale,MutedColor);
        }
        Ui::EndChild();
        }
        draw->AddLine(point(23,390),point(694,390),UI_COLOR(109,79,33,200),scale.y);
        BodyText(draw,detailHasLimit ? "Detail limit reached; total damage is preserved" : detailActorName.empty() ? (selectedPet ? "Pet damage received, grouped across all summons of this type" : "Effective HP damage observed by this client") : detailActorName.c_str(),point(23,detailHeight-23),scale,detailHasLimit ? GoldColor : MutedColor);
        if (HoverArea(point(23,391),UiPoint(570 * scale.x,20 * scale.y)))
            QueueHelp("Counts actual HP removed, capped at the target's remaining HP. Only combat simulated by this client is visible. Distant party damage and pet ownership may be unavailable.",point(725,320),scale,origin.x);
        if (!addonsOpen && !reportOpen) RegisterHitArea();
    }
    Ui::End();
    if (close) { detailSkill = 0; selectedCharacter = 0; selectedPet = 0; historyOpen = false; historySelected = 0; detailActor = 0; detailActorName.clear(); detailByActor = true; }
    else if (back) BackFromDetails();
}

static void DrawPanelActions(UiPoint origin,UiPoint scale,float width,unsigned columns) {
    auto* draw = Ui::GetWindowDrawList();
    const float contentWidth = width-30;
    const float buttonWidth = (contentWidth-4*(columns-1))/columns;
    const float height = (4/columns)*30.0f+4;
    panelOptionsArea = Rectangle(At(origin,scale,15,60),UiPoint(contentWidth*scale.x,height*scale.y));
    draw->AddLine(At(origin,scale,15,58),At(origin,scale,width-15,58),UI_COLOR(125,92,47,200),std::max(1.0f,scale.y));
    const UiPoint textScale(scale.x*0.62f,scale.y*0.62f);
    Ui::PushID("options");
    auto option = [&](const char* label,unsigned index,RECT& bounds,const char* description) {
        const UiPoint at = At(origin,scale,15+(index%columns)*(buttonWidth+4),60+(index/columns)*30.0f);
        const bool clicked = SkinControl(label,label,at,UiPoint(buttonWidth*scale.x,27*scale.y),textScale,&bounds);
        if (Ui::IsItemHovered()) QueueHelp(description,UiPoint(origin.x+(width+5)*scale.x,at.y),scale,origin.x);
        if (clicked) { panelOptionsOpen=false; help.text.clear(); }
        return clicked;
    };
    if (option("History",0,historyButtonRect,"Last 20 dungeon runs, including the current run. Floors and town visits stay together. Click a run for players and skills.")) { historyOpen=true; historySelected=0; selectedCharacter=0; selectedPet=0; }
    if (option("Report",1,reportButtonRect,"Choose Current, Overall or a saved dungeon and Damage Done or Taken before sending a preview to party chat /g.")) OpenReport();
    if (option("Reset",2,panelResetRect,"Immediately clears Current and Overall damage totals for all displayed players.")) InterlockedOr(&actions,1);
    if (option("Hide",3,panelHideRect,"Hides this panel. Measurement continues while enabled. Use your Show / hide shortcut or ESC > Addons > Damage Meter to show it again.")) { visible=false; selectedCharacter=0; selectedPet=0; SavePreferences(); }
    Ui::PopID();
}

static void DrawPanel() {
    panelOptionsArea = {};
    if (!addonRegistry.Damage() || !nativeSkin.Ready() || historyOpen) { panelOptionsOpen=false; return; }
    if (addonsOpen || reportOpen) panelOptionsOpen=false;
    const bool optionsExpanded = panelOptionsOpen;
    const MeterView& view = packet.views[mode+direction*2];
    const UiPoint display = Ui::GetIO().DisplaySize;
    const char* status = packet.status[0] ? packet.status : panelSizeSaveFailed ? "Panel size could not be saved." : !measurementEnabled ? "Measurement paused" : GetTickCount64() - lastUpdate > 3000 ? "Waiting for combat data" : "";
    const UiPoint scale = MeterScale(UiPoint(330,300));
    const auto entries=DisplayRows(view);
    const unsigned rows=std::max(1u,std::min(12u,static_cast<unsigned>(entries.size())));
    const float footer = status[0] ? 36.0f : 18.0f;
    const float width = std::min(panelWidth,(display.x-8)/scale.x);
    const float contentWidth = width-30;
    const unsigned optionColumns = contentWidth >= 250 ? 4u : 2u;
    const float headerHeight = 60+(optionsExpanded ? (4/optionColumns)*30.0f+4 : 0);
    const float rowHeight = std::min(panelRowHeight,((display.y-8)/scale.y-headerHeight-footer)/rows);
    const float textFactor = std::clamp(std::min(rowHeight/25,contentWidth/300),0.72f,2.0f);
    const UiPoint textScale(scale.x*textFactor,scale.y*textFactor);
    const UiPoint size(width*scale.x,(headerHeight+rows*rowHeight+footer)*scale.y);
    if (panelPositionPending) { Ui::SetNextWindowPos(panelNextPosition); panelPositionPending=false; }
    else Ui::SetNextWindowPos(UiPoint(display.x-16*scale.x,display.y-155*scale.y),Ui::FirstUse,UiPoint(1,1));
    Ui::SetNextWindowSize(size);
    if (Ui::Begin(packet.flags & 1 ? "Damage meter [DEMO]" : "Damage meter",nullptr,SurfaceFlags | ((addonsOpen || reportOpen) ? Ui::NoInputs : 0))) {
        const UiPoint origin = MoveSurface(size,scale,width-30,18);
        panelArea = Rectangle(origin,size);
        UiDrawList* draw = Ui::GetWindowDrawList();
        auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
        nativeSkin.Frame(draw,origin,size,scale);
        Heading(draw,"Damage Meter",point(19,9),scale,128,0.62f);
        char elapsed[48]; std::snprintf(elapsed,sizeof(elapsed),"%.1f s",view.duration);
        RightMeterText(draw,elapsed,point(width-19,13),UiPoint(scale.x*0.85f,scale.y*0.85f),MutedColor,(width-158)*scale.x);
        const float controlFactor = 0.64f*std::min(1.0f,contentWidth/300);
        const UiPoint compact(scale.x*controlFactor,scale.y*controlFactor);
        const float modeWidth = (contentWidth-8)*0.26f, directionWidth = (contentWidth-8)*0.46f;
        if (SkinControl("view",mode ? "Overall" : "Current",point(15,31),UiPoint(modeWidth*scale.x,25*scale.y),compact,&panelModeRect)) { mode ^= 1; SavePreferences(); }
        if (Ui::IsItemHovered()) QueueHelp("Current starts when anyone in the party deals or takes damage. Done and Taken share the same fight, ending after 10 seconds without either type of hit. Timeout waiting is excluded from DPS. Overall excludes idle gaps.",point(width+5,31),scale,origin.x);
        if (SkinControl("direction",direction ? "Damage Taken" : "Damage Done",point(19+modeWidth,31),UiPoint(directionWidth*scale.x,25*scale.y),compact,&panelDirectionRect)) { direction ^= 1; selectedCharacter=0; selectedPet=0; detailActorName.clear(); detailByActor=true; SavePreferences(); }
        if (Ui::IsItemHovered()) QueueHelp("Damage Done includes attacks by you and your pets against any target. Damage Taken shows your own HP loss and separate bars for damage received by your pets. Each pet type is grouped per owner, including respawns. Done and Taken share the same party fight and timer.",point(width+5,31),scale,origin.x);
        if (SkinControl("options","Menu",point(23+modeWidth+directionWidth,31),UiPoint((contentWidth-modeWidth-directionWidth-8)*scale.x,25*scale.y),compact,&panelOptionsRect)) panelOptionsOpen = !panelOptionsOpen;
        if (Ui::IsItemHovered() && !panelOptionsOpen) QueueHelp("Expand the action bar for history, party report, reset and hide.",point(width+5,31),scale,origin.x);
        if (optionsExpanded) DrawPanelActions(origin,scale,width,optionColumns);
        double highest=1;
        for (const auto& entry:entries) highest=std::max(highest,entry.data->damage);
        const float barWidth=contentWidth-(entries.size()>rows ? 12.0f : 0.0f);
        const float innerWidth=barWidth-16;
        const float damageWidth=innerWidth*0.28f,dpsWidth=innerWidth*0.32f;
        const float nameWidth=innerWidth-damageWidth-dpsWidth-16;
        const float textY=(rowHeight-2-MeterTextSize("Max",textScale).y/scale.y)*0.5f;
        Ui::SetCursorScreenPos(point(15,headerHeight));
        if (Ui::BeginChild("meter-bars",UiPoint(contentWidth*scale.x,rows*rowHeight*scale.y))) {
            for (unsigned i=0;i<entries.size();++i) {
                const auto& entry=entries[i];
                const PlayerRow& row=*entry.data;
                const UiPoint position=Ui::GetCursorScreenPos();
                const UiPoint rowSize(barWidth*scale.x,(rowHeight-2)*scale.y);
                Ui::PushID(static_cast<int>(row.characterId));
                Ui::PushID(std::to_string(entry.pet).c_str());
                if (Ui::InvisibleButton("player",UiPoint(rowSize.x,rowHeight*scale.y))) SelectRow(entry);
                if (!i) firstRowRect=ItemRectangle();
                const bool hovered=Ui::IsItemHovered();
                auto* list=Ui::GetWindowDrawList();
                nativeSkin.Row(list,position,rowSize,scale,static_cast<float>(row.damage/highest),hovered);
                const float indent=entry.pet ? 8.0f : 0.0f;
                const std::string name=MeterName(row.name,textScale,(nameWidth-indent)*scale.x);
                list->PushClipRect(At(position,scale,5,2),At(position,scale,barWidth-5,rowHeight-4));
                MeterText(list,name.c_str(),At(position,scale,8+indent,textY),textScale,entry.pet ? BodyColor : GoldColor);
                char number[64];
                RightMeterText(list,FormatDamage(row.damage,1).c_str(),At(position,scale,barWidth-16-dpsWidth,textY),textScale,BodyColor,damageWidth*scale.x);
                std::snprintf(number,sizeof(number),"%.0f DPS",row.dps);
                RightMeterText(list,number,At(position,scale,barWidth-8,textY),textScale,BodyColor,dpsWidth*scale.x);
                list->PopClipRect();
                if (hovered) {
                    char description[384];
                    std::snprintf(description,sizeof(description),"%s%s%s\n%s damage, %.0f DPS, %u hits.\n%sClick for skills, damage components and their share of total damage.",row.name,entry.pet ? " | Owner: " : "",entry.pet ? entry.owner : "",FormatDamage(row.damage).c_str(),row.dps,row.hits,entry.pet ? "Damage received by all summons of this type, including respawns. Excluded from the owner's Damage Taken.\n" : "");
                    QueueHelp(description,UiPoint(origin.x+size.x+5*scale.x,position.y),scale,origin.x);
                }
                Ui::PopID();
                Ui::PopID();
            }
            if (entries.empty()) {
                const auto waiting=MeterName("Waiting for character / party",textScale,(contentWidth-16)*scale.x);
                MeterText(Ui::GetWindowDrawList(),waiting.c_str(),point(23,headerHeight+textY),textScale,MutedColor);
            }
        }
        Ui::EndChild();
        if (status[0]) {
            const auto label = MeterName(status,scale,contentWidth*scale.x);
            MeterText(draw,label.c_str(),point(15,headerHeight+3+rows*rowHeight),scale,MutedColor);
            if (HoverArea(point(15,headerHeight+rows*rowHeight),UiPoint(contentWidth*scale.x,18*scale.y))) QueueHelp(status,point(width+5,headerHeight+rows*rowHeight),scale,origin.x);
        }
        ResizePanel(origin,size,scale,rows,rowHeight,headerHeight,footer);
        if (!addonsOpen && !reportOpen) RegisterHitArea();
    }
    Ui::End();
    if (selectedCharacter && !historyOpen) DrawDetails(view);
}

static void DrawHistory() {
    if (!historyOpen || addonsOpen || reportOpen || !nativeSkin.Ready()) return;
    const DungeonRecord* record=nullptr;
    for (const auto& entry:historyData.records) if (entry->id==historySelected) record=entry.get();
    if (!record) { historySelected=0; selectedCharacter=0; selectedPet=0; }
    if (record && selectedCharacter) { DrawDetails(direction ? record->taken : record->view,"Dungeon run"); return; }
    const UiPoint scale=MeterScale(UiPoint(700,440));
    const UiPoint size(700*scale.x,440*scale.y),display=Ui::GetIO().DisplaySize;
    Ui::SetNextWindowSize(size);
    Ui::SetNextWindowPos(UiPoint((display.x-size.x)*0.5f,(display.y-size.y)*0.5f));
    bool back=false;
    if (Ui::Begin("##DungeonHistory",nullptr,SurfaceFlags | Ui::NoSavedSettings)) {
        const UiPoint origin=Ui::GetWindowPos();
        auto point=[origin,scale](float x,float y) { return At(origin,scale,x,y); };
        auto* draw=Ui::GetWindowDrawList();
        historyArea=Rectangle(origin,size);
        nativeSkin.Frame(draw,origin,size,scale);
        Heading(draw,"Dungeon History",point(24,17),scale,470);
        back=SkinControl("history-back",record ? "Back" : "Close",point(600,10),UiPoint(78*scale.x,29*scale.y),UiPoint(scale.x*.75f,scale.y*.75f),&historyBackRect);
        if (!record) {
            BodyText(draw,"Last 20 runs. Select a dungeon for players and skill details.",point(24,49),scale,MutedColor);
            Ui::SetCursorScreenPos(point(20,79));
            if (Ui::BeginChild("runs",UiPoint(660*scale.x,319*scale.y),0,0)) {
                unsigned i=0;
                for (const auto& item:historyData.records) {
                    const auto& entry=*item;
                    const UiPoint row=Ui::GetCursorScreenPos();
                    Ui::PushID(static_cast<int>(entry.id));
                    if (Ui::InvisibleButton("run",UiPoint(644*scale.x,48*scale.y))) historySelected=entry.id;
                    if (!i++) historyFirstRect=ItemRectangle();
                    auto* list=Ui::GetWindowDrawList();
                    nativeSkin.Row(list,row,UiPoint(644*scale.x,46*scale.y),scale,0,Ui::IsItemHovered());
                    MeterText(list,entry.title,At(row,scale,8,5),scale,GoldColor,450*scale.x);
                    std::tm time{}; const std::time_t stamp=static_cast<std::time_t>(entry.started); localtime_s(&time,&stamp);
                    char date[48]{}; std::strftime(date,sizeof(date),"%d.%m. %H:%M",&time);
                    RightMeterText(list,date,At(row,scale,636,5),scale,MutedColor);
                    char summary[160];
                    std::snprintf(summary,sizeof(summary),"%s%s | %.1f combat s | %u players",entry.ending,(entry.flags&2)?" (partial)":"",entry.view.duration,entry.view.playerCount);
                    MeterText(list,summary,At(row,scale,8,26),scale,BodyColor);
                    Ui::PopID();
                }
                if (historyData.records.empty()) BodyText(Ui::GetWindowDrawList(),"History starts with your first recorded dungeon combat.",Ui::GetCursorScreenPos(),scale,MutedColor);
            }
            Ui::EndChild();
        } else {
            const auto& runView=direction ? record->taken : record->view;
            BodyText(draw,record->title,point(24,51),scale,GoldColor,630*scale.x);
            char summary[192];
            std::snprintf(summary,sizeof(summary),"%s%s | %.1f combat s | %lld elapsed s",record->ending,(record->flags&2)?" (partial)":"",record->view.duration,static_cast<long long>(record->ended-record->started));
            BodyText(draw,summary,point(24,89),scale,MutedColor,645*scale.x);
            if (SkinControl("history-direction",direction ? "Damage Taken" : "Damage Done",point(470,110),UiPoint(180*scale.x,25*scale.y),UiPoint(scale.x*.65f,scale.y*.65f))) { direction^=1; SavePreferences(); }
            if (direction && !runView.playerCount) BodyText(draw,"Damage Taken was not recorded in this older run.",point(24,147),scale,MutedColor);
            const auto entries=DisplayRows(runView);
            double highest=1;
            for (const auto& entry:entries) highest=std::max(highest,entry.data->damage);
            Ui::SetCursorScreenPos(point(24,135));
            if (Ui::BeginChild("history-players",UiPoint(650*scale.x,202*scale.y))) {
                for (unsigned i=0;i<entries.size();++i) {
                    const auto& entry=entries[i];
                    const auto& player=*entry.data;
                    const UiPoint row=Ui::GetCursorScreenPos();
                    Ui::PushID(static_cast<int>(player.characterId));
                    Ui::PushID(std::to_string(entry.pet).c_str());
                    if (Ui::InvisibleButton("history-player",UiPoint(638*scale.x,35*scale.y))) SelectRow(entry);
                    if (!i) historyPlayerRect=ItemRectangle();
                    auto* list=Ui::GetWindowDrawList();
                    const bool hovered=Ui::IsItemHovered();
                    nativeSkin.Row(list,row,UiPoint(638*scale.x,32*scale.y),scale,static_cast<float>(player.damage/highest),hovered);
                    const auto name=MeterName(player.name,scale,280*scale.x);
                    MeterText(list,name.c_str(),At(row,scale,entry.pet ? 16.0f : 8.0f,9),scale,entry.pet ? BodyColor : GoldColor);
                    char number[96]; std::snprintf(number,sizeof(number),"%s damage | %.0f DPS",FormatDamage(player.damage).c_str(),player.dps);
                    RightMeterText(list,number,At(row,scale,628,9),scale,BodyColor,315*scale.x);
                    if (hovered && entry.pet) {
                        char description[256];
                        std::snprintf(description,sizeof(description),"%s | Owner: %s\nDamage received by all summons of this type, including respawns. Excluded from the owner's Damage Taken.",player.name,entry.owner);
                        QueueHelp(description,UiPoint(origin.x+size.x+5*scale.x,row.y),scale,origin.x);
                    }
                    Ui::PopID();
                    Ui::PopID();
                }
            }
            Ui::EndChild();
            if (DrawSkinControl("Report",point(365,347),scale,&historyReportRect)) OpenReport(record->id);
            if (record->flags&1) {
                if (DrawSkinControl("Finish run",point(180,347),scale,&historyFinishRect)) { InterlockedOr(&actions,2); historySelected=0; selectedCharacter=0; selectedPet=0; }
                if (Ui::IsItemHovered()) QueueHelp("Archives this run manually. Use after resetting an instance when no new floor seed has been observed yet. Does not reset the game or erase history.",point(705,300),scale,origin.x);
            }
        }
        BodyText(draw,historyData.status[0]?historyData.status:"DPS uses combat time. Only damage observed by this client is recorded.",point(24,411),scale,MutedColor,650*scale.x);
        RegisterHitArea();
    }
    Ui::End();
    if (back) { if (historySelected) historySelected=0; else historyOpen=false; selectedCharacter=0; selectedPet=0; }
}

static void BackFromAddons() {
    hotkeyInput.recording=false;
    if (activeAddon && std::strcmp(activeAddon->id,"nameplates") == 0 && nameplatePage) {
        draftNameplateOptions = nameplatePageOptions;
        nameplatePage = 0;
        nameplateMessage.clear();
        return;
    }
    if (activeAddon) activeAddon = nullptr;
    else addonsOpen = false;
}

static void OpenAddons() {
    selectedCharacter = 0; selectedPet = 0;
    reportOpen = false;
    historyOpen = false;
    activeAddon = nullptr;
    addonsOpen = true;
}

static void OpenDamageSettings() {
    draftVisible = visible;
    draftEnabled = measurementEnabled;
    draftMode = mode;
    draftMeterScalePercent = meterScalePercent;
    draftVisibilityHotkey=visibilityHotkey;
    hotkeyInput.recording=false;
    hotkeyMessage.clear();
    draftReset = false;
}

static void ApplyAddons() {
    if (hotkeyInput.recording) { hotkeyMessage="Finish or cancel the shortcut first."; return; }
    if (!CheckHotkey(draftVisibilityHotkey)) return;
    if (!SaveHotkey()) { hotkeyMessage="Shortcut could not be saved."; return; }
    visibilityHotkey=draftVisibilityHotkey;
    visible = draftVisible;
    measurementEnabled = draftEnabled;
    mode = draftMode;
    meterScalePercent = draftMeterScalePercent;
    if (draftReset) InterlockedOr(&actions,1);
    activeAddon = nullptr;
    SavePreferences();
}

static int OptionRow(const char* label,const char* value,UiPoint position,UiPoint scale,const char* description,UiPoint tooltip, float left,RECT* bounds = nullptr) {
    UiDrawList* draw = Ui::GetWindowDrawList();
    const UiPoint size(330 * scale.x,30 * scale.y);
    draw->AddRectFilled(position,UiPoint(position.x+size.x,position.y+size.y),UI_COLOR(0,0,0,100));
    nativeSkin.AlignedText(draw,label,At(position,scale,25,0),UiPoint(163 * scale.x,30 * scale.y),scale);
    const UiPoint extent = valueFont->CalcTextSizeA(14 * scale.y,FLT_MAX,0,value);
    BodyText(draw,value,UiPoint(position.x+238 * scale.x-extent.x * 0.5f,position.y+(30*scale.y-extent.y)*.5f),scale,UI_WHITE);
    int clicked = 0;
    Ui::PushID(label);
    for (unsigned arrow=0;arrow<2;++arrow) {
        const UiPoint point = At(position,scale,arrow?278.0f:178.0f,5);
        Ui::SetCursorScreenPos(point);
        if (Ui::InvisibleButton(arrow?"next":"previous",UiPoint(20 * scale.x,20 * scale.y))) clicked = arrow ? 1 : -1;
        if (bounds && !arrow) *bounds = ItemRectangle();
        nativeSkin.Arrow(draw,point,scale,arrow!=0,Ui::IsItemActive());
    }
    Ui::PopID();
    if (HoverArea(position,size)) QueueHelp(description,tooltip,scale,left);
    return clicked;
}

static void DrawDamageSettings(UiPoint origin,UiPoint scale) {
    auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    Heading(Ui::GetWindowDrawList(),"Damage Meter",point(34,20),scale,280);
    if (OptionRow("Measurement:",draftEnabled?"On":"Off",point(10,58),scale,"Records effective damage and combat time. Off pauses collection and keeps existing totals. Apply with Okay.",point(355,58),origin.x)) draftEnabled = !draftEnabled;
    if (OptionRow("Damage Meter:",draftVisible?"On":"Off",point(10,96),scale,"Shows or hides the damage panel. Hiding it does not pause enabled measurement. The Show / hide shortcut toggles visibility too.",point(355,96),origin.x,&meterCheckboxRect)) draftVisible = !draftVisible;
    if (OptionRow("View:",draftMode?"Overall":"Current",point(10,134),scale,"Done and Taken share one party fight. The first outgoing or incoming hit starts it; 10 seconds without either ends it. Timeout waiting is excluded from DPS. Minimum time: 1 second.",point(355,134),origin.x)) draftMode ^= 1;
    char zoom[16]; std::snprintf(zoom,sizeof(zoom),"%u%%",draftMeterScalePercent);
    const int step = OptionRow("UI scale:",zoom,point(10,172),scale,"Resizes the meter, details, history and reports. 100% follows the game's UI size. Apply with Okay. You can also drag the meter edges to change its width and bar height.",point(355,172),origin.x,&meterScaleRect);
    if (step) draftMeterScalePercent = static_cast<unsigned>(std::clamp(static_cast<int>(draftMeterScalePercent)+step*10,50,200));
    auto* draw=Ui::GetWindowDrawList();
    draw->AddRectFilled(point(10,210),point(340,240),UI_COLOR(0,0,0,100));
    nativeSkin.AlignedText(draw,"Show / hide:",point(35,210),UiPoint(145*scale.x,30*scale.y),scale);
    const auto keyName=hotkeyInput.recording ? std::string("Press a key...") : draftVisibilityHotkey.Name();
    Ui::SetCursorScreenPos(point(180,212));
    const bool keyClicked=Ui::InvisibleButton("meter-hotkey",UiPoint(136*scale.x,26*scale.y));
    meterHotkeyRect=ItemRectangle();
    nativeSkin.CompactButton(draw,point(180,212),UiPoint(136*scale.x,26*scale.y),scale,Ui::IsItemHovered(),Ui::IsItemActive(),keyName.c_str());
    if (keyClicked) { hotkeyInput.recording=!hotkeyInput.recording; hotkeyMessage.clear(); }
    if (HoverArea(point(10,210),UiPoint(330*scale.x,30*scale.y))) QueueHelp("Click, then press any key or combination unused by the game. Modifiers are optional. Game bindings and text input take priority. Escape cancels; Backspace clears. Default: F8. Save with Okay.",point(355,210),scale,origin.x);
    if (!hotkeyMessage.empty()) {
        const auto extent=valueFont->CalcTextSizeA(14*scale.y,FLT_MAX,296*scale.x,hotkeyMessage.c_str());
        BodyText(draw,hotkeyMessage.c_str(),point(175-extent.x/(2*scale.x),248),scale,GoldColor,296*scale.x);
    }
    if (DrawSkinControl(draftReset?"Reset pending":"Reset damage",point(104.5f,282),scale)) draftReset = !draftReset;
    if (Ui::IsItemHovered()) QueueHelp("Clears Current and Overall totals when you press Okay. Click again to cancel the pending reset. Back or Escape discards it.",point(355,282),scale,origin.x);
    if (DrawSkinControl("Okay",point(24,333),scale,&okayRect)) ApplyAddons();
    if (Ui::IsItemHovered()) QueueHelp("Applies and saves these settings, then returns to the Addons library.",point(355,333),scale,origin.x);
    if (DrawSkinControl("Back",point(185,333),scale,&cancelRect)) BackFromAddons();
    if (Ui::IsItemHovered()) QueueHelp("Returns to the Addons library and discards unapplied changes. Escape has the same effect.",point(355,333),scale,origin.x);
}

static void OpenLootSettings() { draftGoldHidden = goldHidden; lootMessage.clear(); }

static void OpenCooldownSettings() {
    draftCooldownsEnabled = cooldownsEnabled;
    draftCooldownTenths = cooldownTenths;
    draftCooldownReady = cooldownReady;
    draftEffectTimers = effectTimers;
    cooldownMessage.clear();
}

static void DrawCooldownSettings(UiPoint origin,UiPoint scale) {
    auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw = Ui::GetWindowDrawList();
    Heading(draw,"Cooldown Timers",point(34,20),scale,280);
    if (OptionRow("Timers:",draftCooldownsEnabled ? "On" : "Off",point(10,58),scale,"Enables skill cooldowns, ready flashes and Buff / Curse timers. Uses the game's current timers, including refreshed effects and cooldown resets.",point(355,58),origin.x)) draftCooldownsEnabled = !draftCooldownsEnabled;
    if (OptionRow("Decimals:",draftCooldownTenths ? "On" : "Off",point(10,96),scale,"Shows tenths on skill cooldowns below 10 seconds. Buff and Curse timers use whole seconds. From 100 seconds, all timers show whole minutes rounded up. Ready skills have no timer.",point(355,96),origin.x)) draftCooldownTenths = !draftCooldownTenths;
    if (OptionRow("Ready flash:",draftCooldownReady ? "On" : "Off",point(10,134),scale,"Flashes a rainbow border three times when a skill cooldown ends. Does not indicate whether you have enough mana or a valid target.",point(355,134),origin.x)) draftCooldownReady = !draftCooldownReady;
    if (OptionRow("Buff / Curse:",draftEffectTimers ? "On" : "Off",point(10,172),scale,"Buff: green border. Curse: red border. Timer numbers turn gold below 10 seconds and red below 3 seconds. Permanent effects keep their border without a timer.",point(355,172),origin.x)) draftEffectTimers = !draftEffectTimers;
    if (!cooldownMessage.empty()) BodyText(draw,cooldownMessage.c_str(),point(34,208),scale,GoldColor,282 * scale.x);
    if (DrawSkinControl("Okay",point(24,232),scale,&okayRect)) {
        if (SaveCooldownSettings()) {
            cooldownsEnabled = draftCooldownsEnabled;
            cooldownTenths = draftCooldownTenths;
            cooldownReady = draftCooldownReady;
            effectTimers = draftEffectTimers;
            cooldownFrame = {};
            effectFrame = {};
            cooldownHighlights.Reset();
            activeAddon = nullptr;
        } else cooldownMessage = "Settings could not be saved.";
    }
    if (Ui::IsItemHovered()) QueueHelp("Applies and saves Cooldown Timers settings.",point(355,232),scale,origin.x);
    if (DrawSkinControl("Back",point(185,232),scale,&cancelRect)) activeAddon = nullptr;
    if (Ui::IsItemHovered()) QueueHelp("Returns to Addons without applying changes.",point(355,232),scale,origin.x);
}

static void OpenCharacterSheetSettings() { draftCharacterSheetEnabled = characterSheetEnabled; characterSheetMessage.clear(); }

static void OpenMythicSettings() { draftMythicOptions = mythicOptions; mythicMessage.clear(); }

static void DrawMythicSettings(UiPoint origin,UiPoint scale) {
    auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw = Ui::GetWindowDrawList();
    Heading(draw,"Mythic Drop Sounds",point(34,20),scale,280);
    if (OptionRow("Drop sound:",draftMythicOptions.enabled ? "On" : "Off",point(10,58),scale,
        "Plays a sound for your and your party's Mythic drops in this zone.",point(355,58),origin.x)) draftMythicOptions.enabled = !draftMythicOptions.enabled;
    if (OptionRow("Announcements:",draftMythicOptions.announcements ? "On" : "Off",point(10,96),scale,
        "Shows Mythic drop messages in your chat.",point(355,96),origin.x)) draftMythicOptions.announcements = !draftMythicOptions.announcements;
    Heading(draw,"Sound:",point(35,137),scale,200,0.85f);
    draw->AddRectFilled(point(10,157),point(340,189),UI_COLOR(0,0,0,100));
    for (unsigned side=0;side<2;++side) {
        const UiPoint at = point(side ? 305.0f : 25.0f,163);
        Ui::SetCursorScreenPos(at);
        if (Ui::InvisibleButton(side ? "sound-next" : "sound-previous",UiPoint(20*scale.x,20*scale.y))) {
            draftMythicOptions.sound = (draftMythicOptions.sound + (side ? 1 : unsigned(std::size(MythicSounds)-1))) % std::size(MythicSounds);
        }
        nativeSkin.Arrow(draw,at,scale,side!=0,Ui::IsItemActive());
    }
    const char* name = MythicSounds[draftMythicOptions.sound].name;
    const auto extent = valueFont->CalcTextSizeA(14*scale.y,FLT_MAX,0,name);
    BodyText(draw,name,UiPoint(origin.x+175*scale.x-extent.x/2,origin.y+173*scale.y-extent.y/2),scale,BodyColor);
    char volume[16];
    std::snprintf(volume,sizeof(volume),"%u",draftMythicOptions.volume/10);
    const bool custom = draftMythicOptions.sound == MythicCustomSound;
    const int volumeStep = OptionRow("Volume:",volume,point(10,202),scale,custom ? "Mythic sound volume." : "Mythic sound volume. Uses the game's sound volume.",point(355,202),origin.x);
    if (volumeStep) draftMythicOptions.volume = static_cast<unsigned>(std::clamp(static_cast<int>(draftMythicOptions.volume)+volumeStep*10,0,100));
    if (custom && DrawSkinControl("Browse",point(24,240),scale)) mythicBrowse = true;
    if (DrawSkinControl("Play",point(custom ? 185.f : 105.f,240),scale)) {
        mythicPreview = static_cast<int>(draftMythicOptions.sound);
        mythicPreviewVolume = draftMythicOptions.volume;
    }
    if (Ui::IsItemHovered()) QueueHelp("Previews the selected sound and volume.",point(355,240),scale,origin.x);
    if (!mythicMessage.empty()) BodyText(draw,mythicMessage.c_str(),point(34,283),scale,GoldColor,282*scale.x);
    else if (custom) BodyText(draw,CustomSoundMessage(mythicCustomStatus),point(24,283),UiPoint(scale.x*.85f,scale.y*.85f),GoldColor,302*scale.x);
    else if (mythicFailed) BodyText(draw,"Sound unavailable. Restart the game.",point(34,283),scale,GoldColor,282*scale.x);
    if (DrawSkinControl("Okay",point(24,308),scale,&okayRect)) {
        if (SaveMythicSettings()) { mythicOptions=draftMythicOptions; activeAddon=nullptr; }
        else mythicMessage="Settings could not be saved.";
    }
    if (DrawSkinControl("Back",point(185,308),scale,&cancelRect)) activeAddon=nullptr;
}

static void DrawCharacterSheetSettings(UiPoint origin,UiPoint scale) {
    auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw = Ui::GetWindowDrawList();
    Heading(draw,"Better Character Sheet",point(34,20),scale,280);
    if (OptionRow("Character Sheet:",draftCharacterSheetEnabled ? "On" : "Off",point(10,58),scale,"Shows Weapon Crit, Magic Crit, Stun Resist and Movement.",point(355,58),origin.x)) draftCharacterSheetEnabled = !draftCharacterSheetEnabled;
    BodyText(draw,"Shows Weapon Crit, Magic Crit, Stun Resist and Movement.",point(34,106),scale,BodyColor,282 * scale.x);
    if (!characterSheetMessage.empty()) BodyText(draw,characterSheetMessage.c_str(),point(34,195),scale,GoldColor,282 * scale.x);
    if (DrawSkinControl("Okay",point(24,232),scale,&okayRect)) {
        if (SaveCharacterSheetSettings()) { characterSheetEnabled = draftCharacterSheetEnabled; characterSheetFrame = {}; activeAddon = nullptr; }
        else characterSheetMessage = "Settings could not be saved.";
    }
    if (DrawSkinControl("Back",point(185,232),scale,&cancelRect)) activeAddon = nullptr;
}

static void DrawCharacterSheet() {
    if (!addonRegistry.CharacterSheet() || !characterSheetEnabled || !ValidCharacterSheet(characterSheetFrame)) return;
    const auto& frame = characterSheetFrame;
    const auto display = Ui::GetIO().DisplaySize;
    const auto scale = GameScale();
    const float textSize = valueFont->SizeForEm(14 * scale.y);
    Ui::SetNextWindowPos(UiPoint(0,0));
    Ui::SetNextWindowSize(display);
    if (Ui::Begin("##CharacterSheetStats",nullptr,SurfaceFlags | Ui::NoInputs | Ui::NoSavedSettings)) {
        auto* draw = Ui::GetWindowDrawList();
        const UiColor color = UI_COLOR(255,255,255,static_cast<unsigned>(255 * frame.opacity));
        char crit[32] = "N/A", magicCrit[32] = "N/A", movement[32] = "N/A", stunResist[32] = "N/A";
        if (frame.criticalValid) std::snprintf(crit,sizeof(crit),"%.2f%%",SheetCriticalPercent(frame.critical,frame.pvp));
        if (frame.magicCriticalValid) std::snprintf(magicCrit,sizeof(magicCrit),"%.2f%%",SheetCriticalProbability(frame.magicCritical));
        if (frame.movementValid) std::snprintf(movement,sizeof(movement),"%.0f%%",frame.movement);
        if (frame.stunResistValid) std::snprintf(stunResist,sizeof(stunResist),"%.1f%%",frame.stunResist);
        const char* texts[] = {"Weapon Crit:",crit,"Magic Crit:",magicCrit,"Stun Resist:",stunResist,"Movement:",movement};
        for (unsigned i = 0; i < frame.cells.size(); ++i) {
            const auto& cell = frame.cells[i];
            const auto extent = valueFont->CalcTextSizeA(textSize,FLT_MAX,0,texts[i]);
            const float space = cell.w * display.x - extent.x;
            const float x = cell.x * display.x + (i & 1 ? space : 0);
            const float y = cell.y * display.y;
            draw->PushClipRect(UiPoint(cell.x * display.x,y),UiPoint((cell.x + cell.w) * display.x,(cell.y + cell.h) * display.y),true);
            draw->AddText(valueFont,textSize,UiPoint(std::round(x),std::round(y)),color,texts[i]);
            draw->PopClipRect();
        }
        const auto& section = frame.section;
        const UiPoint a(section.x * display.x,section.y * display.y), b((section.x + section.w) * display.x,(section.y + section.h) * display.y);
        const UiColor line = UI_COLOR(0,0,0,static_cast<unsigned>(135 * frame.opacity));
        draw->AddLine(UiPoint(a.x,a.y + 24 * scale.y),UiPoint(b.x,a.y + 24 * scale.y),line,std::max(1.0f,scale.y * .6f));
        draw->AddLine(UiPoint(a.x,a.y + 46 * scale.y),UiPoint(b.x,a.y + 46 * scale.y),line,std::max(1.0f,scale.y * .6f));
        draw->AddLine(UiPoint((a.x+b.x)*.5f,a.y),UiPoint((a.x+b.x)*.5f,b.y),line,std::max(1.0f,scale.x * .6f));
        const auto mouse = Ui::GetIO().MousePos;
        const float rowTop = frame.cells[0].y * display.y;
        if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= rowTop && mouse.y <= b.y &&
            (!characterInputTest || characterInputTest(mouse.x / display.x,mouse.y / display.y,false))) {
            const bool left = mouse.x < (a.x+b.x)*.5f;
            const bool top = mouse.y < a.y + 46 * scale.y;
            std::string tooltip = top ? (left ? "Critical hit chance with weapons." : "Critical hit chance with magic skills.") :
                left ? (frame.pvp ? "Chance to resist stun from players." : "Chance to resist stun from equal-level enemies.") : "Current movement speed.\n100% = unmodified speed.";
            if (top && !left && frame.magicSkills[0]) { tooltip += "\n"; tooltip += frame.magicSkills.data(); }
            help.text.clear();
            QueueHelp(tooltip.c_str(),UiPoint(b.x + 8 * scale.x,top ? rowTop : frame.cells[4].y * display.y),scale,a.x,true);
            DrawHelp();
        }
        for (const auto& resistance : frame.resistances) {
            const auto& bounds = resistance.bounds;
            if (bounds.w <= 0 || bounds.h <= 0 || mouse.x < bounds.x * display.x || mouse.x >= (bounds.x + bounds.w) * display.x ||
                mouse.y < bounds.y * display.y || mouse.y >= (bounds.y + bounds.h) * display.y ||
                (characterInputTest && !characterInputTest(mouse.x / display.x,mouse.y / display.y,false))) continue;
            char tooltip[160] = "Chance to resist: N/A";
            const char* reference = frame.pvp ? "Against players." : "Against equal-level enemies.";
            if (resistance.valid) {
                if (resistance.weapon == resistance.magic)
                    std::snprintf(tooltip,sizeof(tooltip),"Chance to resist: %.1f%%\n%s",resistance.weapon / 256.0,reference);
                else
                    std::snprintf(tooltip,sizeof(tooltip),"Chance to resist:\nWeapon: %.1f%%\nMagic: %.1f%%\n%s",resistance.weapon / 256.0,resistance.magic / 256.0,reference);
            }
            help.text.clear();
            QueueHelp(tooltip,UiPoint(b.x + 8 * scale.x,bounds.y * display.y),scale,a.x,true);
            DrawHelp();
            break;
        }
    }
    Ui::End();
}

static void OpenNameplateSettings() {
    draftNameplateOptions = nameplateOptions;
    nameplateMessage.clear();
    nameplatePage = 0;
}

static void DrawNameplateSettings(UiPoint origin,UiPoint scale) {
    auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw = Ui::GetWindowDrawList();
    const bool overview = nameplatePage == 0;
    const char* titles[] = {"Nameplates","Player","Players","Pets"};
    Heading(draw,titles[nameplatePage],point(34,20),scale,280);
    nameplateOptionRects = {}; nameplateCategoryRects = {};
    if (overview) {
        if (OptionRow("Nameplates:",draftNameplateOptions.enabled ? "On" : "Off",point(10,58),scale,"Enables these nameplate settings. Off restores the game's nameplate settings. Apply changes with Okay.",point(355,58),origin.x,&nameplateOptionRects[0])) draftNameplateOptions.enabled = !draftNameplateOptions.enabled;
        const char* descriptions[] = {"HP + Mana, Name and Posse above your own character.","HP + Mana, Name and Posse above other players.","Health bars above player-owned Bling Gnomes and Flaming Buddies."};
        for (unsigned i = 0; i < 3; ++i) {
            const float y = 104 + i * 48.0f;
            if (SkinControl(titles[i+1],titles[i+1],point(25,y),UiPoint(300*scale.x,39*scale.y),scale,&nameplateCategoryRects[i])) {
                nameplatePageOptions = draftNameplateOptions;
                nameplatePage = i + 1;
                nameplateMessage.clear();
            }
            if (Ui::IsItemHovered()) QueueHelp(descriptions[i],point(355,y),scale,origin.x);
        }
    } else {
        struct Option { const char* label; bool* value; const char* help; };
        const bool self = nameplatePage == 1;
        Option options[] = {
            {"HP + Mana:",self ? &draftNameplateOptions.selfBars : &draftNameplateOptions.playerBars,self ? "Shows health and mana above your own character. Does not change the portrait or target frame." : "Shows health and mana above other players. Does not change your own character, portraits or target frame."},
            {"Name:",self ? &draftNameplateOptions.selfName : &draftNameplateOptions.playerName,self ? "Shows your own character's name." : "Shows other players' character names."},
            {"Posse:",self ? &draftNameplateOptions.selfPosse : &draftNameplateOptions.playerPosse,self ? "Shows your Posse name above your character when you belong to a Posse." : "Shows other players' Posse names when they belong to a Posse."}
        };
        unsigned count = 3;
        if (nameplatePage == 3) {
            options[0] = {"Bling Gnome:",&draftNameplateOptions.blingBars,"On keeps native health bars for player-owned Bling Gnomes. Off hides these bars for all players' Bling Gnomes."};
            options[1] = {"Flaming Buddy:",&draftNameplateOptions.flamingBars,"On keeps native health bars for player-owned Flaming Buddies. Off hides these bars for all players' Flaming Buddies."};
            count = 2;
        }
        for (unsigned i = 0; i < count; ++i) {
            const float y = 58 + i * 38.0f;
            auto& option = options[i];
            if (OptionRow(option.label,*option.value ? "On" : "Off",point(10,y),scale,option.help,point(355,y),origin.x,&nameplateOptionRects[i])) *option.value = !*option.value;
        }
    }
    const float footer = overview ? 281.0f : 232.0f;
    if (!nameplateMessage.empty()) BodyText(draw,nameplateMessage.c_str(),point(34,footer-31),scale,GoldColor,282 * scale.x);
    if (DrawSkinControl("Okay",point(24,footer),scale,&okayRect)) {
        if (SaveNameplateSettings()) {
            nameplateOptions = draftNameplateOptions;
            if (overview) activeAddon = nullptr;
            else nameplatePage = 0;
            nameplateMessage.clear();
        }
        else nameplateMessage = "Settings could not be saved.";
    }
    if (Ui::IsItemHovered()) QueueHelp(overview ? "Applies and saves Nameplates settings, then returns to Addons." : "Applies and saves these settings, then returns to Nameplates.",point(355,footer),scale,origin.x);
    if (DrawSkinControl("Back",point(185,footer),scale,&cancelRect)) BackFromAddons();
    if (Ui::IsItemHovered()) QueueHelp(overview ? "Returns to Addons without applying changes." : "Returns to Nameplates without applying changes in this menu. Escape has the same effect.",point(355,footer),scale,origin.x);
}

static bool CooldownsVisible(uint64_t now) {
    if (!addonRegistry.Cooldowns() || !cooldownsEnabled) return false;
    for (unsigned i = 0; i < cooldownFrame.count; ++i)
        if (cooldownFrame.icons[i].ticks || (cooldownReady && cooldownHighlights.Strength(i,now) > 0)) return true;
    return effectTimers && effectFrame.count;
}

static void DrawTimerText(UiDrawList* draw,const TimerIcon& icon,const UiPoint& display,bool effect) {
    if (!icon.ticks) return;
    const UiPoint origin(icon.x * display.x,icon.y * display.y), size(icon.width * display.x,icon.height * display.y);
    const auto text = FormatCooldown(icon.ticks,effect ? false : cooldownTenths);
    const auto style = TimerStyle(icon.ticks);
    float fontSize = std::min(size.x,size.y) * style.scale;
    auto extent = meterFont->CalcTextSizeA(fontSize,FLT_MAX,0,text.c_str());
    if (extent.x > size.x * 0.9f) {
        fontSize *= size.x * 0.9f / extent.x;
        extent = meterFont->CalcTextSizeA(fontSize,FLT_MAX,0,text.c_str());
    }
    const UiPoint at(std::floor(origin.x + (size.x - extent.x) * 0.5f),std::floor(origin.y + (size.y - extent.y) * 0.5f));
    const unsigned alpha = static_cast<unsigned>(255 * icon.opacity);
    draw->PushClipRect(origin,UiPoint(origin.x + size.x,origin.y + size.y));
    draw->AddText(meterFont.get(),fontSize,at,UI_COLOR(style.red,style.green,style.blue,alpha),text.c_str());
    draw->PopClipRect();
}

static void DrawCooldowns(uint64_t now,AddonUiLayer layer) {
    if (!CooldownsVisible(now) || !nativeSkin.Ready() || !meterFont) return;
    const auto display = Ui::GetIO().DisplaySize;
    Ui::SetNextWindowPos(UiPoint());
    Ui::SetNextWindowSize(display);
    if (Ui::Begin(layer == AddonUiLayer::Hotbar ? "##CooldownTimers" : "##EffectTimers",nullptr,Ui::NoInputs | Ui::NoSavedSettings)) {
        auto* draw = Ui::GetWindowDrawList();
        if (layer == AddonUiLayer::Hotbar) for (unsigned i = 0; i < cooldownFrame.count; ++i) {
            const auto& icon = cooldownFrame.icons[i];
            const UiPoint origin(icon.x * display.x,icon.y * display.y), size(icon.width * display.x,icon.height * display.y);
            if (!icon.ticks) {
                if (cooldownReady) nativeSkin.IconReady(draw,origin,size,cooldownHighlights.Strength(i,now) * icon.opacity);
                continue;
            }
            DrawTimerText(draw,icon,display,false);
        }
        if (layer == AddonUiLayer::Effects && effectTimers) for (unsigned i = 0; i < effectFrame.count; ++i) {
            const auto& icon = effectFrame.icons[i];
            nativeSkin.IconEffect(draw,UiPoint(icon.x * display.x,icon.y * display.y),UiPoint(icon.width * display.x,icon.height * display.y),icon.opacity,icon.alignment);
            DrawTimerText(draw,icon,display,true);
        }
    }
    Ui::End();
}

static void DrawLootSettings(UiPoint origin,UiPoint scale) {
    auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    UiDrawList* draw = Ui::GetWindowDrawList();
    Heading(draw,"Hide Gold Labels",point(34,20),scale,280);
    if (OptionRow("Hide Gold:",draftGoldHidden ? "On" : "Off",point(10,58),scale,"Hides gold nameplates in both own-loot and party-loot views. Uses your existing loot key bindings, including hold and toggle modes. Gold remains on the ground and can still be picked up.",point(355,58),origin.x,&goldOptionRect)) draftGoldHidden = !draftGoldHidden;
    BodyText(draw,"Applies to gold labels only. Item labels keep the game's quality filter and ownership rules.",point(34,106),scale,BodyColor,282 * scale.x);
    if (!lootMessage.empty()) BodyText(draw,lootMessage.c_str(),point(34,172),scale,GoldColor,282 * scale.x);
    if (DrawSkinControl("Okay",point(24,232),scale,&okayRect)) {
        if (SaveLootSettings()) { goldHidden = draftGoldHidden; activeAddon = nullptr; }
        else lootMessage = "Settings could not be saved.";
    }
    if (Ui::IsItemHovered()) QueueHelp("Saves Hide Gold for the next game launch and applies it to the next loot-label refresh.",point(355,232),scale,origin.x);
    if (DrawSkinControl("Back",point(185,232),scale,&cancelRect)) activeAddon = nullptr;
    if (Ui::IsItemHovered()) QueueHelp("Returns to Addons without applying changes.",point(355,232),scale,origin.x);
}

static void SelectReport() {
    reportPreview=reportChoices.empty() ? MeterReport{} : reportChoices[reportChoice].reports[reportType];
    reportMessage.clear();
}

static void OpenReport(uint64_t dungeon) {
    reportChoices.clear();
    reportChoices.reserve(22);
    for (unsigned view=0;view<2;++view)
        reportChoices.push_back({view ? "Overall" : "Current fight",{BuildPartyReport(packet.views[view],view!=0),BuildPartyReport(packet.views[view+2],view!=0,false,true)}});
    reportChoice=mode;
    reportType=direction;
    for (const auto& item:historyData.records) {
        const auto& entry=*item;
        if (reportChoices.size()==22) break;
        std::tm time{};
        const std::time_t stamp=static_cast<std::time_t>(entry.started);
        localtime_s(&time,&stamp);
        char date[48]{};
        std::strftime(date,sizeof(date),"%d.%m. %H:%M",&time);
        if (entry.id==dungeon) reportChoice=reportChoices.size();
        reportChoices.push_back({std::string(date)+" | "+entry.title,{BuildPartyReport(entry.view,false,true,false,entry.title),BuildPartyReport(entry.taken,false,true,true,entry.title)}});
    }
    SelectReport();
    reportOpen=true;
}

static int ReportSelector(const char* id,const char* label,const char* value,UiPoint at,UiPoint scale,std::array<RECT,2>& bounds) {
    auto* draw=Ui::GetWindowDrawList();
    draw->AddRectFilled(at,At(at,scale,584,32),UI_COLOR(0,0,0,120));
    Heading(draw,label,At(at,scale,10,8),scale,100,0.72f);
    int step=0;
    Ui::PushID(id);
    for (unsigned arrow=0;arrow<2;++arrow) {
        const UiPoint position=At(at,scale,arrow ? 554.0f : 122.0f,6);
        Ui::SetCursorScreenPos(position);
        if (Ui::InvisibleButton(arrow ? "next" : "previous",UiPoint(20*scale.x,20*scale.y))) step=arrow ? 1 : -1;
        bounds[arrow]=ItemRectangle();
        nativeSkin.Arrow(draw,position,scale,arrow!=0,Ui::IsItemActive());
    }
    Ui::PopID();
    const auto clipped=MeterName(value,scale,397*scale.x);
    MeterText(draw,clipped.c_str(),At(at,scale,149,10),scale,BodyColor,397*scale.x);
    return step;
}

static void DrawReport() {
    if (!reportOpen || addonsOpen || !nativeSkin.Ready()) return;
    const UiPoint display = Ui::GetIO().DisplaySize;
    const UiPoint scale = MeterScale(UiPoint(640,540));
    std::array<float,6> rowY{};
    float afterRows = 151;
    for (unsigned i=0;i<reportPreview.count;++i) {
        rowY[i] = afterRows;
        afterRows += std::max(25.0f,valueFont->CalcTextSizeA(14 * scale.y,FLT_MAX,584 * scale.x,reportPreview.lines[i]).y / scale.y + 7);
    }
    afterRows = std::max(301.0f,afterRows);
    const UiPoint size(640 * scale.x,(afterRows+103) * scale.y);
    Ui::SetNextWindowSize(size);
    Ui::SetNextWindowPos(UiPoint((display.x-size.x)*0.5f,(display.y-size.y)*0.5f));
    if (Ui::Begin("##PartyDamageReport",nullptr,SurfaceFlags | Ui::NoSavedSettings)) {
        const UiPoint origin = Ui::GetWindowPos();
        auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
        auto* draw = Ui::GetWindowDrawList();
        nativeSkin.Frame(draw,origin,size,scale);
        Heading(draw,"Party Report",point(28,20),scale,560);
        Ui::BeginDisabled(reportBusy || reportPending);
        const char* selected=reportChoices.empty() ? "No reports" : reportChoices[reportChoice].label.c_str();
        const int sourceStep=ReportSelector("report-source","Source:",selected,point(28,55),scale,reportSourceRects);
        if (HoverArea(point(28,55),UiPoint(584*scale.x,32*scale.y))) QueueHelp("Selects Current fight, Overall or one of the last 20 dungeon runs. Reports use the snapshot captured when this window opened.",point(645,55),scale,origin.x);
        const int typeStep=ReportSelector("report-type","Damage:",reportType ? "Damage Taken" : "Damage Done",point(28,97),scale,reportTypeRects);
        if (HoverArea(point(28,97),UiPoint(584*scale.x,32*scale.y))) QueueHelp("Selects damage dealt to targets or damage received from attackers. This changes only the report, not the meter's active view.",point(645,97),scale,origin.x);
        Ui::EndDisabled();
        for (unsigned i=0;i<reportPreview.count;++i) BodyText(draw,reportPreview.lines[i],point(28,rowY[i]),scale,i ? BodyColor : GoldColor,584 * scale.x);
        const char* message = !reportPreview.count ? "No damage recorded in the selected view." : !reportParty ? "Join a party to send a report." : reportMessage.empty() ? "Preview is frozen. Send posts these lines to party chat /g." : reportMessage.c_str();
        BodyText(draw,message,point(28,afterRows+8),scale,MutedColor,584 * scale.x);
        const bool canSend = reportPreview.count && reportParty && !reportBusy && !reportPending;
        Ui::BeginDisabled(!canSend);
        if (DrawSkinControl("Send to /g",point(160,afterRows+51),scale,&reportSendRect) && canSend) {
            reportRequest = reportPreview;
            reportRequest.recipient = reportDestination;
            reportPending = true;
        }
        Ui::EndDisabled();
        if (HoverArea(point(160,afterRows+51),UiPoint(141 * scale.x,39 * scale.y))) QueueHelp("Sends one heading and one line per player to party /g, spaced 1.5 seconds apart. A 15-second cooldown prevents repeated reports. Leaving the party or changing maps cancels remaining lines.",point(645,afterRows),scale,origin.x);
        if (DrawSkinControl("Close",point(340,afterRows+51),scale,&reportCloseRect)) reportOpen = false;
        if (!reportChoices.empty() && (sourceStep || typeStep)) {
            if (sourceStep) reportChoice=static_cast<size_t>((static_cast<int>(reportChoice)+sourceStep+static_cast<int>(reportChoices.size()))%static_cast<int>(reportChoices.size()));
            if (typeStep) reportType^=1;
            SelectReport();
        }
        RegisterHitArea();
    }
    Ui::End();
}

static void OpenExtensionSettings() {
    extensionMessage.clear();
    extensionPage=0; extensionReadable=true;
    const auto* extension=activeAddon->extension;
    const auto* advanced=activeAddon->advanced;
    for (unsigned i=0;i<(advanced ? advanced->optionCount : extension->optionCount);++i) {
        extensionDraft[i]=advanced ? AddonRegistry::ReadSetting(*advanced,i) : AddonRegistry::Read(*extension,i);
        if (extensionDraft[i]<0) { extensionReadable=false; extensionMessage="This addon could not read its settings."; }
    }
}

static void DrawAdvancedSettings(UiPoint origin,UiPoint scale) {
    auto point=[origin,scale](float x,float y) { return At(origin,scale,x,y); };
    const auto& settings=*activeAddon->advanced;
    const int pageStep=OptionRow("Settings:",settings.pages[extensionPage],point(10,54),scale,"Choose a settings page. Okay saves and applies all pages. Back or Escape discards changes.",point(355,54),origin.x);
    if (pageStep) extensionPage=(extensionPage+settings.pageCount+pageStep)%settings.pageCount;
    unsigned row=0;
    for (unsigned i=0;i<settings.optionCount;++i) {
        const auto& option=settings.options[i];
        if (option.page!=extensionPage) continue;
        char text[32];
        const int value=extensionDraft[i];
        if (value<0) strcpy_s(text,"Unavailable");
        else if (option.format==ExtensionValueFormat::Toggle) strcpy_s(text,value ? "On" : "Off");
        else if (option.format==ExtensionValueFormat::Percent) sprintf_s(text,"%d%%",value);
        else if (option.format==ExtensionValueFormat::TenthsSeconds) sprintf_s(text,"%d.%d s",value/10,value%10);
        else sprintf_s(text,"%d",value);
        const float y=94+34.0f*row++;
        Ui::PushID(static_cast<int>(i));
        const int step=OptionRow(option.label,text,point(10,y),scale,option.help,point(355,y),origin.x);
        if (step && extensionReadable) {
            extensionDraft[i]=option.format==ExtensionValueFormat::Toggle ? value^1 : std::clamp(value+step*option.step,option.minimum,option.maximum);
            extensionMessage.clear();
        }
        Ui::PopID();
    }
    if (row<7) {
        Ui::GetWindowDrawList()->PushClipRect(point(34,100+34.0f*row),point(316,368));
        BodyText(Ui::GetWindowDrawList(),activeAddon->description,point(34,100+34.0f*row),scale,BodyColor,282*scale.x);
        Ui::GetWindowDrawList()->PopClipRect();
    }
    if (DrawSkinControl("Defaults",point(105,374),scale) && extensionReadable) {
        for (unsigned i=0;i<settings.optionCount;++i) extensionDraft[i]=settings.options[i].initial;
        extensionMessage.clear();
    }
    const char* message=extensionMessage.empty() ? "Okay saves and applies all pages immediately." : extensionMessage.c_str();
    Ui::GetWindowDrawList()->PushClipRect(point(24,415),point(326,447));
    BodyText(Ui::GetWindowDrawList(),message,point(24,415),scale,extensionMessage.empty() ? BodyColor : GoldColor,302*scale.x);
    Ui::GetWindowDrawList()->PopClipRect();
    if (HoverArea(point(24,415),UiPoint(302*scale.x,32*scale.y))) QueueHelp(message,point(355,415),scale,origin.x);
    if (DrawSkinControl("Okay",point(24,452),scale) && extensionReadable) {
        char error[192]{};
        if (AddonRegistry::ApplySettings(settings,extensionDraft,error,sizeof(error))) activeAddon=nullptr;
        else extensionMessage=error[0] ? error : "Could not save settings. Please try again.";
    }
    if (DrawSkinControl("Back",point(185,452),scale)) activeAddon=nullptr;
}

static void DrawExtensionSettings(UiPoint origin,UiPoint scale) {
    auto point=[origin,scale](float x,float y) { return At(origin,scale,x,y); };
    const auto& extension=*activeAddon->extension;
    Heading(Ui::GetWindowDrawList(),extension.name,point(34,20),scale,280);
    if (activeAddon->advanced) { DrawAdvancedSettings(origin,scale); return; }
    for (unsigned i=0;i<extension.optionCount;++i) {
        const float y=58+38.0f*i;
        if (OptionRow(extension.options[i].label,extensionDraft[i]>0 ? "On" : "Off",point(10,y),scale,extension.options[i].help,point(355,y),origin.x) && extensionReadable) { extensionDraft[i]^=1; extensionMessage.clear(); }
    }
    if (!extension.optionCount) BodyText(Ui::GetWindowDrawList(),extension.description,point(34,58),scale,BodyColor,280*scale.x);
    if (!extensionMessage.empty()) BodyText(Ui::GetWindowDrawList(),extensionMessage.c_str(),point(34,211),scale,GoldColor,282*scale.x);
    if (DrawSkinControl("Okay",point(24,232),scale) && extensionReadable) {
        if (!extension.optionCount || AddonRegistry::Apply(extension,extensionDraft)) activeAddon=nullptr;
        else extensionMessage="This addon could not save its settings.";
    }
    if (DrawSkinControl("Back",point(185,232),scale)) activeAddon=nullptr;
}

static void DiscoverAddons(const std::filesystem::path& root) {
    addonRegistry.Discover(root);
    registeredAddons.clear();
    if (addonRegistry.Damage()) registeredAddons.push_back({"damage-meter","Damage Meter","Combat damage, DPS and party reports.",OpenDamageSettings,DrawDamageSettings,nullptr});
    if (addonRegistry.Money()) registeredAddons.push_back({"hide-gold-labels","Hide Gold Labels","Hide gold labels in own and party loot.",OpenLootSettings,DrawLootSettings,nullptr});
    if (addonRegistry.Cooldowns()) registeredAddons.push_back({"cooldown-timers","Cooldown Timers","Skill cooldowns and Buff / Curse durations.",OpenCooldownSettings,DrawCooldownSettings,nullptr});
    if (addonRegistry.Nameplates()) registeredAddons.push_back({"nameplates","Nameplates","Player names, HP / Mana and Posse; pet HP bars.",OpenNameplateSettings,DrawNameplateSettings,nullptr});
    if (addonRegistry.CharacterSheet()) registeredAddons.push_back({"better-character-sheet","Better Character Sheet","Shows Weapon Crit, Magic Crit, Stun Resist and Movement.",OpenCharacterSheetSettings,DrawCharacterSheetSettings,nullptr});
    if (addonRegistry.MythicSounds()) registeredAddons.push_back({"mythic-drop-sounds","Mythic Drop Sounds","Sound and chat alerts for Mythic drops.",OpenMythicSettings,DrawMythicSettings,nullptr});
    for (const auto& extension:addonRegistry.Extensions()) registeredAddons.push_back({extension.id,extension.name,extension.description,OpenExtensionSettings,DrawExtensionSettings,&extension,addonRegistry.Settings(extension)});
}

static void DrawLibrary(UiPoint origin,UiPoint scale,float listHeight) {
    auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    UiDrawList* draw = Ui::GetWindowDrawList();
    Heading(draw,"Addons",point(34,20),scale,280);
    Ui::SetCursorScreenPos(point(20,58));
    if (Ui::BeginChild("addon-list",UiPoint(310 * scale.x,listHeight * scale.y),0,0)) {
        for (const auto& addon : registeredAddons) {
            const UiPoint cursor = Ui::GetCursorScreenPos();
            const UiPoint row(cursor.x+(registeredAddons.size()*48>listHeight ? 0 : 5*scale.x),cursor.y);
            if (SkinControl(addon.id,addon.name,row,UiPoint(300 * scale.x,39 * scale.y),scale,addon.open == OpenDamageSettings ? &libraryEntryRect : addon.open == OpenLootSettings ? &lootEntryRect : nullptr)) {
                activeAddon = &addon;
                addon.open();
            }
            if (Ui::IsItemHovered()) QueueHelp(addon.description,UiPoint(origin.x+355 * scale.x,row.y),scale,origin.x);
            Ui::SetCursorScreenPos(At(cursor,scale,0,47));
            Ui::Dummy(UiPoint(300 * scale.x,scale.y));
        }
    }
    Ui::EndChild();
    if (DrawSkinControl("Back",point(104.5f,68+listHeight),scale,&libraryBackRect)) addonsOpen = false;
    if (Ui::IsItemHovered()) QueueHelp("Returns to the game's Escape menu.",point(355,68+listHeight),scale,origin.x);
}

static void DrawAddons() {
    const UiPoint display = Ui::GetIO().DisplaySize;
    if (menuBounds[2]<=0 || menuBounds[3]<=0 || !nativeSkin.Ready()) return;
    const UiPoint position(menuBounds[0]*display.x,menuBounds[1]*display.y);
    const UiPoint size(menuBounds[2]*display.x,menuBounds[3]*display.y);
    const UiPoint buttonScale(size.x/141,size.y/39);
    const int flags = SurfaceFlags | Ui::NoSavedSettings;
    if (!addonsOpen) {
        Ui::SetNextWindowPos(position);
        Ui::SetNextWindowSize(size);
        if (Ui::Begin("##EscapeAddons",nullptr,flags)) {
            if (DrawSkinControl("Addons",position,buttonScale,&addonsButtonRect)) OpenAddons();
            RegisterHitArea();
        }
        Ui::End();
    }
    if (addonsOpen) {
        const float listHeight = std::clamp(static_cast<float>(registeredAddons.size())*48,48.0f,192.0f);
        const float height = activeAddon ? (activeAddon->advanced ? 506.0f : activeAddon->open == OpenDamageSettings ? 390.0f : activeAddon->open == OpenNameplateSettings && !nameplatePage ? 340.0f : activeAddon->open == OpenMythicSettings ? 365.0f : 289.0f) : 125+listHeight;
        const UiPoint scale = FitScale(buttonScale,UiPoint(350,height));
        const UiPoint panelSize(350 * scale.x,height * scale.y);
        const UiPoint origin(std::floor((display.x-panelSize.x)*0.5f),std::floor((display.y-panelSize.y)*0.5f));
        Ui::SetNextWindowPos(origin);
        Ui::SetNextWindowSize(panelSize);
        if (Ui::Begin("##NativeAddonsOptions",nullptr,flags)) {
            nativeSkin.Frame(Ui::GetWindowDrawList(),origin,panelSize,scale);
            if (activeAddon) activeAddon->settings(origin,scale);
            else DrawLibrary(origin,scale,listHeight);
            RegisterHitArea();
        }
        Ui::End();
    }
}

extern "C" __declspec(dllexport) int __cdecl MeterOverlayStart(const char* iniFile) {
    Lock lock;
    if (context) return 0;
    HMODULE retained = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&MeterOverlayStart), &retained)) return 0;
    shutdownMessage = RegisterWindowMessageW(L"DungeonRunnersDamageMeter.Shutdown.v2");
    if (!shutdownMessage) return 0;
    settingsFile = iniFile ? iniFile : "";
    const auto addonsDirectory=std::filesystem::u8path(settingsFile).parent_path().parent_path();
    if (!LoadSkinData(addonsDirectory/L"Runtime"/L"ui.bin")) return 0;
    DiscoverAddons(addonsDirectory);
    mythicSettings = addonsDirectory / L"MythicDropSounds" / L"settings.ini";
    mythicOptions = {}; mythicPreview = -1; mythicFailed = false;
    mythicBrowse = false; mythicCustomStatus = CustomSoundStatus::None;
    { std::ifstream input(mythicSettings); mythicOptions.Load(input); }
    characterSheetSettings = addonsDirectory / L"BetterCharacterSheet" / L"settings.ini";
    characterSheetEnabled = true; characterSheetFrame = {};
    { unsigned value = 1; std::ifstream input(characterSheetSettings); if (input >> value && value <= 1) characterSheetEnabled = value != 0; }
    nameplateSettings = addonsDirectory / L"Nameplates" / L"settings.ini";
    nameplateOptions = {};
    { std::ifstream input(nameplateSettings); nameplateOptions.Load(input); }
    cooldownSettings = addonsDirectory / L"CooldownTimers" / L"settings.ini";
    cooldownsEnabled = cooldownTenths = cooldownReady = effectTimers = true;
    cooldownFrame = {};
    effectFrame = {};
    cooldownHighlights.Reset();
    {
        unsigned enabled = 1, tenths = 1, ready = 1, effects = 1;
        std::ifstream input(cooldownSettings);
        if (input >> enabled >> tenths && enabled <= 1 && tenths <= 1) {
            cooldownsEnabled = enabled != 0;
            cooldownTenths = tenths != 0;
            if (input >> ready && ready <= 1) cooldownReady = ready != 0;
            if (input >> effects && effects <= 1) effectTimers = effects != 0;
        }
    }
    lootSettings = addonsDirectory / L"HideGoldLabels" / L"settings.ini";
    goldHidden = true;
    for (const auto* directory : {L"HideGoldLabels",L"HideMoneyBoxes",L"LootLabels"}) {
        unsigned savedGold = 1;
        std::ifstream goldInput(addonsDirectory / directory / L"settings.ini");
        if (goldInput >> savedGold && savedGold <= 1) { goldHidden = savedGold != 0; break; }
    }
    reportOpen = reportPending = reportBusy = reportParty = false;
    historyOpen = false; historySelected=0; historyData={};
    reportPreview = reportRequest = {};
    reportChoices.clear(); reportChoice=0; reportType=0;
    reportMessage.clear();
    visible = true;
    measurementEnabled = true;
    worldVisible = false;
    nativeFrame = false;
    nativeLayers = 0;
    addonsOpen = false;
    activeAddon = nullptr;
    logicalUiSize = {};
    consumeEscapeUp = false;
    std::fill(std::begin(menuBounds), std::end(menuBounds), 0.0f);
    shuttingDown = false;
    selectedCharacter = 0; selectedPet = 0;
    mode = 0;
    direction = 0; detailActor=0; detailActorName.clear(); detailByActor=true;
    panelWidth = 330; panelRowHeight = 25;
    meterScalePercent = draftMeterScalePercent = 100;
    meterScaleRect = {};
    meterHotkeyRect={}; hotkeyInput.Reset(); hotkeyMessage.clear();
    visibilityHotkey={}; draftVisibilityHotkey={};
    panelOptionsOpen = false; panelOptionsArea = {};
    panelResizing = panelSizeDirty = panelSizeSaveFailed = false;
    resizeAxes = 0;
    panelPositionPending = false;
    panelResizeRect = panelWidthRect = panelHeightRect = panelLeftRect = panelTopRect = {};
    if (!settingsFile.empty()) {
        std::ifstream input(std::filesystem::u8path(settingsFile + ".prefs"));
        unsigned shown = 1, savedMode = 0;
        if (input >> shown >> savedMode && shown <= 1 && savedMode <= 1) { visible = shown != 0; mode = savedMode; }
        unsigned savedEnabled = 1;
        if (input >> savedEnabled && savedEnabled <= 1) measurementEnabled = savedEnabled != 0;
        unsigned savedDirection=0;
        if (input >> savedDirection && savedDirection<=1) direction=savedDirection;
        unsigned savedScale=100;
        if (input >> savedScale && savedScale>=50 && savedScale<=200 && savedScale%10==0) meterScalePercent=savedScale;
        std::ifstream hotkeyFile(std::filesystem::u8path(settingsFile+".hotkey"));
        HotkeyBinding savedHotkey;
        std::string extra;
        if (hotkeyFile>>savedHotkey.key>>savedHotkey.modifiers && !(hotkeyFile>>extra) && savedHotkey.Valid()) visibilityHotkey=savedHotkey;
        std::ifstream scaleInput(std::filesystem::u8path(settingsFile + ".scale"));
        std::string format;
        if (scaleInput >> format) {
            float width = 330, height = 25;
            if (format == "size") {
                if (scaleInput >> width >> height && std::isfinite(width) && std::isfinite(height) && width >= 240 && width <= 1200 && height >= 18 && height <= 160) { panelWidth=width; panelRowHeight=height; }
            } else {
                std::istringstream legacy(format);
                float zoom = 1;
                if (legacy >> zoom && legacy.eof() && std::isfinite(zoom) && zoom >= 0.6f && zoom <= 2) { panelWidth=std::max(240.0f,330*zoom); panelRowHeight=std::max(18.0f,25*zoom); }
            }
        }
    }
    actions = 0;
    frames = 0;
    stopped = 0;
    packet = {}; pendingPacket = {}; ++detailRevision;
    packet.magic = MeterMagic;
    packet.version = MeterVersion;
    return 1;
}

extern "C" __declspec(dllexport) void __cdecl MeterOverlayMenu(float x, float y, float width, float height) {
    Lock lock;
    if (!worldVisible || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > 1 || y + height > 1) {
        std::fill(std::begin(menuBounds), std::end(menuBounds), 0.0f);
        addonsOpen = false;
        activeAddon = nullptr;
        addonsButtonRect = {};
        return;
    }
    menuBounds[0] = x; menuBounds[1] = y; menuBounds[2] = width; menuBounds[3] = height;
}

extern "C" void __cdecl MeterOverlayUiSize(int width, int height) {
    Lock lock;
    logicalUiSize = width >= 320 && height >= 200 && width <= 16384 && height <= 16384 ? UiPoint(static_cast<float>(width),static_cast<float>(height)) : UiPoint();
}

extern "C" __declspec(dllexport) int __cdecl MeterOverlayUpdate(const void* input, unsigned size) {
    if (!input || size != sizeof(MeterPacket)) return 0;
    const MeterPacket& next = *static_cast<const MeterPacket*>(input);
    if (next.magic != MeterMagic || next.version != MeterVersion) return 0;
    for (const MeterView& view:next.views) if (!ValidMeterView(view)) return 0;
    if (next.views[0].petCount || next.views[1].petCount) return 0;
    Lock lock;
    try { pendingPacket=next; std::swap(packet,pendingPacket); }
    catch (...) { return 0; }
    ++detailRevision;
    packet.status[sizeof(packet.status)-1]=0;
    lastUpdate = GetTickCount64();
    return 1;
}

extern "C" void __cdecl MeterOverlayLayer(IDirect3DDevice9* device,AddonUiLayer layer) {
    Lock lock;
    if (shuttingDown) { ShutdownUnlocked(); InterlockedExchange(&stopped, 1); return; }
    if (!worldVisible) { hitCount = 0; return; }
    if (!device) return;
    if (FAILED(device->TestCooperativeLevel())) { nativeFrame = false; hitCount = 0; return; }
    if (layer != AddonUiLayer::Meter && (!nativeFrame || graphics != device)) return;
    if (graphics && graphics != device) ShutdownUnlocked();
    if (!context && !InitializeGraphics(device)) return;
    if (!skinAttempted) {
        skinAttempted=true;
        if (nativeSkin.Load(device)) {
            meterFont=std::make_unique<UiFont>();
            if (!meterFont->LoadAtlas(nativeSkin.FontTexture(),SkinAdvance.data(),SkinAdvance.size(),valueFont)) { meterFont.reset(); nativeSkin.Release(); }
        }
    }
    const uint64_t now = GetTickCount64();
    Ui::SetCurrentContext(context);
    if (layer == AddonUiLayer::Meter) {
        if (nativeFrame) Ui::EndFrame();
        Ui::NewFrame();
        nativeFrame = true;
        nativeLayers = 0;
        hitCount = 0;
    }
    const unsigned bit = 1u << static_cast<unsigned>(layer);
    if (nativeLayers & bit) return;
    nativeLayers |= bit;
    if (layer == AddonUiLayer::Menu && menuBounds[2] <= 0) return;
    Ui::SetInputEnabled(InputAllowed(Ui::GetIO().MousePos,layer == AddonUiLayer::Menu));
    if (layer == AddonUiLayer::Meter) {
        help.text.clear();
        if (visible) DrawPanel(); else panelOptionsOpen=false;
        DrawHistory();
        DrawReport();
        DrawHelp();
    } else if (layer == AddonUiLayer::Menu) { help.text.clear(); DrawAddons(); DrawHelp(); }
    else if (layer == AddonUiLayer::CharacterSheet) DrawCharacterSheet();
    else DrawCooldowns(now,layer);
    Ui::Flush();
    Ui::SetInputEnabled(true);
}

extern "C" void __cdecl MeterOverlayEndFrame() {
    Lock lock;
    if (context && nativeFrame) { Ui::SetCurrentContext(context); Ui::EndFrame(); InterlockedIncrement(&frames); }
    nativeFrame = false;
}

extern "C" void __cdecl MeterOverlayInputTest(AddonInputTest test) { Lock lock; nativeInputTest = test; }
extern "C" void __cdecl MeterOverlayHotkeyTest(AddonHotkeyTest test) { Lock lock; nativeHotkeyTest = test; }

extern "C" __declspec(dllexport) void __cdecl MeterOverlayRender(IDirect3DDevice9* device) {
    Lock lock;
    if (shuttingDown) { ShutdownUnlocked(); InterlockedExchange(&stopped,1); return; }
    if (!device || !worldVisible || FAILED(device->BeginScene())) return;
    for (auto layer : {AddonUiLayer::Meter,AddonUiLayer::Hotbar,AddonUiLayer::Effects,AddonUiLayer::CharacterSheet,AddonUiLayer::Menu}) MeterOverlayLayer(device,layer);
    MeterOverlayEndFrame();
    device->EndScene();
}

extern "C" bool __cdecl MeterOverlayAddonsOpen() {
    Lock lock;
    return addonsOpen;
}

extern "C" bool __cdecl MeterOverlayHideGold() { Lock lock; return addonRegistry.Money() && goldHidden; }
extern "C" void __cdecl MeterOverlayCharacterInputTest(AddonInputTest test) { Lock lock; characterInputTest = test; }
extern "C" bool __cdecl MeterOverlayCharacterSheetEnabled() { Lock lock; return addonRegistry.CharacterSheet() && characterSheetEnabled; }
extern "C" void __cdecl MeterOverlayCharacterSheet(const CharacterSheetFrame* input) {
    Lock lock;
    characterSheetFrame = input && worldVisible && ValidCharacterSheet(*input) ? *input : CharacterSheetFrame{};
}
extern "C" unsigned __cdecl MeterOverlayNameplates() { Lock lock; return addonRegistry.Nameplates() ? nameplateOptions.Mask() : 0; }
extern "C" bool __cdecl MeterOverlayCooldownsEnabled() { Lock lock; return addonRegistry.Cooldowns() && cooldownsEnabled; }
extern "C" void __cdecl MeterOverlayCooldowns(const CooldownFrame* input) {
    Lock lock;
    if (input && ValidTimerFrame(*input) && worldVisible) {
        cooldownFrame = *input;
        cooldownHighlights.Update(cooldownFrame,GetTickCount64(),cooldownsEnabled && cooldownReady);
    } else { cooldownFrame = {}; cooldownHighlights.Reset(); }
}
extern "C" bool __cdecl MeterOverlayEffectsEnabled() { Lock lock; return addonRegistry.Cooldowns() && cooldownsEnabled && effectTimers; }
extern "C" void __cdecl MeterOverlayEffects(const EffectFrame* input) {
    Lock lock;
    if (input && ValidTimerFrame(*input) && worldVisible) effectFrame = *input;
    else effectFrame = {};
}
extern "C" void __cdecl MeterOverlaySharedHistory(const DungeonHistorySnapshot* input) {
    if (!input || input->records.size()>20 || !std::memchr(input->status,0,sizeof(input->status))) return;
    Lock lock;
    for (const auto& record:input->records) {
        if (!record) return;
        if (std::find(historyData.records.begin(),historyData.records.end(),record)==historyData.records.end() && !ValidDungeonRecord(*record)) return;
    }
    historyData=*input;
    ++detailRevision;
}
extern "C" void __cdecl MeterOverlayHistory(const DungeonHistoryView* input) {
    if (!input || input->records.size()>20 || !std::memchr(input->status,0,sizeof(input->status))) return;
    DungeonHistorySnapshot shared;
    std::memcpy(shared.status,input->status,sizeof(shared.status));
    shared.records.reserve(input->records.size());
    for (const auto& record:input->records) {
        if (!ValidDungeonRecord(record)) return;
        shared.records.push_back(std::make_shared<const DungeonRecord>(record));
    }
    MeterOverlaySharedHistory(&shared);
}
extern "C" bool __cdecl MeterOverlayTakeReport(MeterReport* output) {
    Lock lock;
    if (!output || !reportPending) return false;
    *output = reportRequest;
    reportPending = false;
    return true;
}

extern "C" int __cdecl MeterOverlayMythicSettings(MythicSettings* output,bool failed,unsigned* previewVolume = nullptr,HWND* browse = nullptr,CustomSoundStatus status = CustomSoundStatus::None,bool imported = false) {
    Lock lock;
    mythicFailed = failed;
    mythicCustomStatus = status;
    if (imported) {
        auto saved = mythicOptions;
        saved.sound = MythicCustomSound;
        if (SaveMythicSettings(saved)) {
            mythicOptions = saved;
            draftMythicOptions.sound = MythicCustomSound;
            mythicMessage.clear();
        } else mythicMessage = "Custom sound loaded. Settings could not be saved.";
    }
    *output = mythicOptions;
    output->enabled = output->enabled && addonRegistry.MythicSounds();
    output->announcements = output->announcements && addonRegistry.MythicSounds();
    const int preview = addonRegistry.MythicSounds() && (!failed || mythicPreview == int(MythicCustomSound)) ? mythicPreview : -1;
    if (browse) *browse = addonRegistry.MythicSounds() && mythicBrowse && activeAddon && activeAddon->open == OpenMythicSettings && draftMythicOptions.sound == MythicCustomSound ? gameWindow : nullptr;
    mythicBrowse = false;
    if (previewVolume) *previewVolume = mythicPreviewVolume;
    mythicPreview = -1;
    return preview;
}
extern "C" void __cdecl MeterOverlayReportState(bool party,bool busy,const char* message,const ReportContext* destination) {
    Lock lock;
    reportParty = party;
    reportBusy = busy;
    reportMessage = message ? message : "";
    reportDestination = destination ? *destination : ReportContext{};
}

extern "C" __declspec(dllexport) bool __cdecl MeterOverlayEnabled() {
    Lock lock;
    return addonRegistry.Damage() && measurementEnabled;
}

extern "C" __declspec(dllexport) void __cdecl MeterOverlayWorld(bool shown) {
    Lock lock;
    worldVisible = shown;
    if (!shown) { characterSheetFrame = {}; cooldownFrame = {}; effectFrame = {}; cooldownHighlights.Reset(); }
    if (!shown) {
        hotkeyInput.recording=false; hotkeyMessage.clear();
        EndPanelResize(); hitCount = 0; dragging = false;
        addonsOpen = historyOpen = panelOptionsOpen = false;
        activeAddon = nullptr; addonsButtonRect = {}; consumeEscapeUp = false;
        selectedCharacter = 0; selectedPet = 0;
        reportOpen = reportPending = false; reportPreview = reportRequest = {};
        std::fill(std::begin(menuBounds),std::end(menuBounds),0.0f);
        if (context) { Ui::SetCurrentContext(context); Ui::ClearInput(); if (nativeFrame) Ui::EndFrame(); }
        nativeFrame = false;
    }
}

extern "C" __declspec(dllexport) void __cdecl MeterOverlayInvalidate() {
    Lock lock;
    if (context) { Ui::SetCurrentContext(context); Ui::ClearInput(); }
}

extern "C" __declspec(dllexport) void __cdecl MeterOverlayStop() {
    Lock lock;
    EndPanelResize();
    shuttingDown = true;
    visible = false;
    addonsOpen = false;
    hitCount = 0;
    if (!context) InterlockedExchange(&stopped, 1);
    else if (gameWindow) PostMessageW(gameWindow, shutdownMessage, 0, 0);
}

extern "C" __declspec(dllexport) unsigned __cdecl MeterOverlayStatus() {
    Lock lock;
    const unsigned command = static_cast<unsigned>(InterlockedExchange(&actions, 0));
    return command | (context ? 0x100 : 0) | (stopped ? 0x200 : 0) | (frames ? 0x400 : 0);
}
