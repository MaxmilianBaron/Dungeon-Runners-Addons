#include <windows.h>
#include <d3d9.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include "native_patch.h"
#include "native_reader.h"
#include "native_nameplates.h"
#include "native_hotkeys.h"
#include "native_cooldowns.h"
#include "native_character_sheet.h"
#include "native_mythic_sounds.h"
#include "native_hooks.h"
#include "native_chat.h"
#include "native_archive.h"
#include "native_history.h"
#include "addon_api.h"
#include "native_catalog.generated.h"

extern "C" int __cdecl MeterOverlayStart(const char*);
extern "C" int __cdecl MeterOverlayUpdate(const void*, unsigned);
extern "C" void __cdecl MeterOverlayMenu(float, float, float, float);
extern "C" void __cdecl MeterOverlayUiSize(int, int);
extern "C" void __cdecl MeterOverlayLayer(IDirect3DDevice9*,AddonUiLayer);
extern "C" void __cdecl MeterOverlayEndFrame();
extern "C" void __cdecl MeterOverlayInputTest(AddonInputTest);
extern "C" void __cdecl MeterOverlayHotkeyTest(AddonHotkeyTest);
extern "C" void __cdecl MeterOverlayInvalidate();
extern "C" unsigned __cdecl MeterOverlayStatus();
extern "C" bool __cdecl MeterOverlayEnabled();
extern "C" bool __cdecl MeterOverlayAddonsOpen();
extern "C" void __cdecl MeterOverlayWorld(bool);
extern "C" bool __cdecl MeterOverlayHideGold();
extern "C" unsigned __cdecl MeterOverlayNameplates();
extern "C" bool __cdecl MeterOverlayTakeReport(MeterReport*);
extern "C" void __cdecl MeterOverlayReportState(bool,bool,const char*,const ReportContext*);
extern "C" void __cdecl MeterOverlaySharedHistory(const DungeonHistorySnapshot*);
extern "C" bool __cdecl MeterOverlayCooldownsEnabled();
extern "C" void __cdecl MeterOverlayCooldowns(const CooldownFrame*);
extern "C" bool __cdecl MeterOverlayEffectsEnabled();
extern "C" void __cdecl MeterOverlayEffects(const EffectFrame*);
extern "C" bool __cdecl MeterOverlayCharacterSheetEnabled();
extern "C" void __cdecl MeterOverlayCharacterSheet(const CharacterSheetFrame*);
extern "C" void __cdecl MeterOverlayCharacterInputTest(AddonInputTest);
extern "C" int __cdecl MeterOverlayMythicSettings(MythicSettings*,bool,unsigned*,HWND*,CustomSoundStatus,bool);

static INIT_ONCE bootstrap = INIT_ONCE_STATIC_INIT;
static volatile LONG ready = 0;
static volatile LONG collecting = 0;
static uintptr_t image = 0;
static std::recursive_mutex stateGate;
static NativeMeter meter;
static MeterPacket snapshot{};
static DungeonHistory history;
static DungeonHistorySnapshot historySnapshot;
static uint64_t nextHistory = 0;
static uint64_t nextRoster = 0;
static uint64_t nextSnapshot = 0;
static bool inWorld = false;
static uintptr_t hiddenMenuFrame = 0;
static bool uiPrepared = false;
static NativeCharacterSheet characterSheet;
static NativeMythicSounds mythicSounds;
static volatile LONG hideGold = 0;
static volatile LONG nameplateMask = 0;
static SRWLOCK nameplateGate = SRWLOCK_INIT;
static NameplateIndex nameplateIndex;
static PartyReportQueue partyReports;
static const char* reportStatus = "";
struct PendingHit { uintptr_t unit, damage; int32_t hp; bool valid; };
static thread_local PendingHit pending{};

static bool CopyMemoryChecked(uintptr_t address, void* destination, size_t length) {
    if (address < 0x10000 || length > 4096 || address > UINTPTR_MAX - length) return false;
    __try { std::memcpy(destination, reinterpret_cast<const void*>(address), length); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static const char* LookupLabel(const char* path) {
    const auto first = std::begin(SkillLabels), end = std::end(SkillLabels);
    const auto found = std::lower_bound(first, end, path, [](const CatalogLabel& row, const char* key) { return std::strcmp(row.path, key) < 0; });
    return found != end && std::strcmp(found->path, path) == 0 ? found->label : nullptr;
}

class Hash {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
public:
    explicit Hash(const wchar_t* name) {
        if (BCryptOpenAlgorithmProvider(&algorithm, name, nullptr, 0) < 0 || BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
            if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
            algorithm = nullptr;
            throw std::runtime_error("Hash initialization failed");
        }
    }
    ~Hash() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
    void Add(const unsigned char* bytes, ULONG size) { if (BCryptHashData(hash, const_cast<PUCHAR>(bytes), size, 0) < 0) throw std::runtime_error("Hash input failed"); }
    std::string Finish(ULONG size) {
        unsigned char digest[32]{};
        if (size > sizeof(digest) || BCryptFinishHash(hash, digest, size, 0) < 0) throw std::runtime_error("Hash finalization failed");
        std::string result;
        const char* hex = "0123456789abcdef";
        for (ULONG i = 0; i < size; ++i) { result += hex[digest[i] >> 4]; result += hex[digest[i] & 15]; }
        return result;
    }
};

static std::string FileHash(const std::filesystem::path& path, const wchar_t* algorithm, ULONG size) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Required file cannot be read");
    Hash hash(algorithm);
    unsigned char block[32768];
    while (input.read(reinterpret_cast<char*>(block), sizeof(block)) || input.gcount()) hash.Add(block, static_cast<ULONG>(input.gcount()));
    if (!input.eof()) throw std::runtime_error("File hash read failed");
    return hash.Finish(size);
}

static bool VerifyCatalog(const std::filesystem::path& directory) {
    if (FileHash(directory / L"game.pki", BCRYPT_SHA1_ALGORITHM, 20) != CatalogPkiSha1) return false;
    std::ifstream input(directory / L"game.pkg", std::ios::binary);
    if (!input) return false;
    Hash hash(BCRYPT_SHA256_ALGORITHM);
    unsigned char block[32768];
    for (const auto& range : SkillRanges) {
        input.seekg(static_cast<std::streamoff>(range.offset));
        for (uint32_t left = range.length; left;) {
            const auto size = std::min<uint32_t>(left, sizeof(block));
            if (!input.read(reinterpret_cast<char*>(block), size)) return false;
            hash.Add(block, size);
            left -= size;
        }
    }
    return hash.Finish(32) == CatalogPayloadSha256;
}

static void Fault(const char* text) {
    std::lock_guard<std::recursive_mutex> lock(stateGate);
    meter.Fail(text);
    history.Pause(true);
    InterlockedExchange(&collecting, 0);
    nextSnapshot = 0;
}

static bool ExpandMenu(uintptr_t menu, uintptr_t frame, uintptr_t back) {
    NativeReader reader(image, CopyMemoryChecked, LookupLabel);
    const auto size = image + 0x27fed0;
    const auto location = image + 0x27fc60;
    if (reader.Pointer(reader.Pointer(menu) + 0xac) != size || reader.Pointer(reader.Pointer(frame) + 0xac) != size || reader.Pointer(reader.Pointer(back) + 0xa4) != location) return false;
    int32_t menuWidth = 0, frameWidth = 0, backX = 0;
    if (!reader.Read(menu + 0xf8, menuWidth) || !reader.Read(frame + 0xf8, frameWidth) || !reader.Read(back + 0xf0, backX) || menuWidth != 172 || frameWidth != 172 || backX != 15) return false;
    const int32_t dimension[] = {172,279}, point[] = {15,230};
    using Setter = void (__thiscall*)(void*, const int32_t*, bool);
    reinterpret_cast<Setter>(size)(reinterpret_cast<void*>(menu), dimension, true);
    reinterpret_cast<Setter>(size)(reinterpret_cast<void*>(frame), dimension, true);
    reinterpret_cast<Setter>(location)(reinterpret_cast<void*>(back), point, true);
    return true;
}

static bool SetSheetGeometry(uintptr_t node,const NativeCharacterSheet::Geometry& bounds) {
    NativeReader reader(image,CopyMemoryChecked,LookupLabel);
    const uintptr_t type = reader.Pointer(node);
    if ((type != image + 0x4ba0c0 && type != image + 0x449ee8) || reader.Pointer(type + 0xa4) != image + 0x27fc60 || reader.Pointer(type + 0xac) != image + 0x27fed0) return false;
    using Setter = void (__thiscall*)(void*,const int32_t*,bool);
    __try {
        reinterpret_cast<Setter>(image + 0x27fc60)(reinterpret_cast<void*>(node),bounds.data(),false);
        reinterpret_cast<Setter>(image + 0x27fed0)(reinterpret_cast<void*>(node),bounds.data() + 2,false);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

extern "C" void __fastcall CharacterSheetVisualDraw(uintptr_t visual,uintptr_t,uintptr_t event,SheetQuad area) {
    using Draw = void (__thiscall*)(void*,void*,SheetQuad);
    const auto original = reinterpret_cast<Draw>(CharacterSheetVisualOriginal);
    NativeReader reader(image,CopyMemoryChecked,LookupLabel);
    if (!characterSheet.DrawVisual(reader,image,visual,event,area,original)) original(reinterpret_cast<void*>(visual),reinterpret_cast<void*>(event),area);
}

static bool SafeSendParty(uintptr_t chat,const char* text) {
    __try { return SendNativePartyLine(image + 0x1ffca0,chat,text); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void UpdateReports(const NativeReader& reader,bool shown,uint64_t now) {
    std::lock_guard<std::recursive_mutex> lock(stateGate);
    ReportContext destination;
    const bool available = shown && reader.PartyChat(destination);
    MeterReport request;
    if (MeterOverlayTakeReport(&request)) {
        if (!available) reportStatus = "Join a party to send a report.";
        else if (!(request.recipient == destination)) reportStatus = "Report cancelled: party changed before submission.";
        else if (!partyReports.Start(request,destination,now)) reportStatus = "Please wait before sending another report.";
        else reportStatus = "Sending to party /g...";
    }
    if (partyReports.Pending()) {
        if (!available) { partyReports.Cancel(); reportStatus = "Report cancelled: party or connection unavailable."; }
        else {
            const char* line = partyReports.Next(destination,now);
            if (!partyReports.Pending()) reportStatus = "Report cancelled: party or character changed.";
            else if (line) {
                const bool sent = SafeSendParty(destination.identity[4],line);
                partyReports.Sent(sent,now);
                if (!sent) reportStatus = "Report stopped: chat submission failed.";
                else if (!partyReports.Pending()) reportStatus = "Report submitted to party /g.";
            }
        }
    }
    MeterOverlayReportState(available,partyReports.Pending() || partyReports.CoolingDown(now),reportStatus,&destination);
}

extern "C" uintptr_t __cdecl LootLabelDispatch(const HookRegisters* registers) {
    const DWORD error = GetLastError();
    uintptr_t next = reinterpret_cast<uintptr_t>(LootLabelOriginal);
    if (InterlockedCompareExchange(&ready,0,0) && InterlockedCompareExchange(&hideGold,0,0)) {
        NativeReader reader(image,CopyMemoryChecked,LookupLabel);
        if (reader.IsGold(registers->esi)) next = image + 0x86f17;
    }
    SetLastError(error);
    return next;
}

static uintptr_t VisibleControlAt(uintptr_t root,const int32_t* point) {
    using HitTest = uintptr_t (__thiscall*)(void*,const int32_t*,bool);
    __try { return reinterpret_cast<HitTest>(image + 0x281540)(reinterpret_cast<void*>(root),point,true); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static bool OverlayInput(float x,float y,bool menuLayer) {
    NativeReader reader(image,CopyMemoryChecked,LookupLabel);
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1 || !reader.InWorld()) return false;
    const uintptr_t root = reader.Pointer(image + 0x533e20);
    int32_t width = 0,height = 0;
    if (!root || !reader.Read(root + 0xf8,width) || !reader.Read(root + 0xfc,height) || width < 1 || height < 1 || width > 16384 || height > 16384) return false;
    const int32_t point[] = {static_cast<int32_t>(x * width),static_cast<int32_t>(y * height)};
    return reader.OverlayInput(VisibleControlAt(root,point),menuLayer);
}

static AddonHotkeyState OverlayHotkey(unsigned key,unsigned modifiers,bool activation) {
    NativeReader reader(image,CopyMemoryChecked,LookupLabel);
    return NativeHotkeys(reader,image).Check(key,modifiers,activation);
}

static bool CharacterInput(float x,float y,bool) {
    NativeReader reader(image,CopyMemoryChecked,LookupLabel);
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x > 1 || y > 1 || !reader.InWorld()) return false;
    const uintptr_t ui = reader.Pointer(image + 0x5314b0), root = reader.Pointer(image + 0x533e20), sheet = reader.Pointer(ui + 0x270);
    int32_t width = 0, height = 0;
    if (!root || !sheet || !reader.Read(root + 0xf8,width) || !reader.Read(root + 0xfc,height) || width < 1 || height < 1 || width > 16384 || height > 16384) return false;
    const int32_t point[] = {static_cast<int32_t>(x * width),static_cast<int32_t>(y * height)};
    uintptr_t node = VisibleControlAt(root,point);
    for (unsigned depth = 0; node && depth < 24; ++depth) {
        if (node == sheet) return true;
        node = reader.Pointer(node + 0x14);
    }
    return false;
}

static void EnablePlayerPlate(uintptr_t stack) {
    __try {
        auto* options = reinterpret_cast<uint32_t*>(stack + 0x30);
        options[0] = options[1] = 0x01010101;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

extern "C" uintptr_t __cdecl NameplateDispatch(unsigned kind,const HookRegisters* registers) {
    const DWORD error = GetLastError();
    void* originals[] = {NameplateOptionsOriginal,NameplateCreateOriginal,NameplateBarsOriginal,NameplateNameOriginal,NameplatePosseOriginal,NameplateRetireOriginal};
    uintptr_t next = kind < std::size(originals) ? reinterpret_cast<uintptr_t>(originals[kind]) : 0;
    const unsigned mask = static_cast<unsigned>(InterlockedCompareExchange(&nameplateMask,0,0));
    if (registers && InterlockedCompareExchange(&ready,0,0) && TryAcquireSRWLockExclusive(&nameplateGate)) {
        NativeReader reader(image,CopyMemoryChecked,LookupLabel);
        const uintptr_t stack = registers->esp + 8;
        if (kind == 5) nameplateIndex.Forget(registers->edi);
        else if (kind == 1) {
            const auto unit = reader.Pointer(stack + 8);
            nameplateIndex.Remember(registers->eax,mask ? NativeNameplates(reader,image).Classify(unit) : NameplateKind::Other);
        } else if (mask & 1u) {
            if (kind == 0) {
                const auto type = NativeNameplates(reader,image).Classify(registers->ebx);
                nameplateIndex.Remember(reader.Pointer(registers->edi + 0xf4),type);
                if (type == NameplateKind::Player || type == NameplateKind::Self) EnablePlayerPlate(stack);
            } else if (kind >= 2 && kind <= 4) {
                const auto part = static_cast<NameplatePart>(kind - 2);
                if (!NameplateVisible(mask,nameplateIndex.Find(registers->esi),part)) {
                    const uintptr_t targets[] = {0x288e94,0x288ecc,0x288f04};
                    next = image + targets[kind - 2];
                }
            }
        }
        ReleaseSRWLockExclusive(&nameplateGate);
    }
    SetLastError(error);
    return next;
}

static void RefreshUi(const NativeReader& reader) {
    const uint64_t now = GetTickCount64();
    const uintptr_t ui = reader.Pointer(image + 0x5314b0);
    const bool shown = reader.InWorld();
    const unsigned actions = MeterOverlayStatus();
    const bool enabled = MeterOverlayEnabled();
    MythicSettings soundSettings;
    unsigned previewVolume = 100;
    HWND browse = nullptr;
    bool soundFailed = false, soundImported = false;
    CustomSoundStatus soundStatus;
    {
        std::lock_guard<std::recursive_mutex> lock(stateGate);
        soundFailed = mythicSounds.Failed(); soundImported = mythicSounds.TakeCustomImport(); soundStatus = mythicSounds.CustomStatus();
    }
    const int preview = MeterOverlayMythicSettings(&soundSettings,soundFailed,&previewVolume,&browse,soundStatus,soundImported);
    {
        std::lock_guard<std::recursive_mutex> lock(stateGate);
        if (browse) mythicSounds.Browse(browse);
        mythicSounds.Service(reader,image,soundSettings,preview,shown,now,previewVolume);
    }
    InterlockedExchange(&hideGold,MeterOverlayHideGold() ? 1 : 0);
    InterlockedExchange(&nameplateMask,static_cast<LONG>(MeterOverlayNameplates()));
    UpdateReports(reader,shown,now);
    bool updated = false;
    {
        std::lock_guard<std::recursive_mutex> lock(stateGate);
        if (actions & 1) { meter.Reset(); nextSnapshot = nextRoster = 0; }
        if (actions & 2) { history.Finish("Finished manually",now); nextHistory=0; }
        meter.Enable(enabled);
        if (!enabled) history.Pause(true);
        if (!shown && inWorld) { meter.Zone(); history.LeaveZone(); nextRoster = 0; }
        inWorld = shown;
        if (shown && now >= nextRoster) {
            uint32_t self = 0;
            std::vector<MeterMember> members;
            const bool rosterReady = reader.Party(self, members) && meter.Roster(self, members);
            if (!rosterReady) meter.Zone();
            std::string zoneKey;
            uint32_t seed = 0;
            if (rosterReady && meter.Enabled() && reader.ZoneIdentity(zoneKey,seed)) {
                const auto end=std::end(DungeonZones);
                const auto zone=std::lower_bound(std::begin(DungeonZones),end,zoneKey,[](const CatalogZone& item,const std::string& key) { return item.key<key; });
                if (zone!=end && zoneKey==zone->key) history.Context(zoneKey,zone->family,zone->title,seed,self,members,now);
                else history.Pause(true);
            } else history.Pause();
            nextRoster = now + (rosterReady ? 500 : 100);
        }
        InterlockedExchange(&collecting, shown && meter.HasRoster() && meter.Enabled() ? 1 : 0);
        if (now >= nextSnapshot) {
            meter.Snapshot(snapshot, now);
            updated = true;
            nextSnapshot = now + 200;
        }
        history.Save(now);
        if (now >= nextHistory) { history.Share(historySnapshot,now); MeterOverlaySharedHistory(&historySnapshot); nextHistory=now+1000; }
    }
    MeterOverlayWorld(shown);
    characterSheet.Prepare(reader,image,MeterOverlayCharacterSheetEnabled(),shown,now,SetSheetGeometry);
    InterlockedExchange(reinterpret_cast<volatile LONG*>(&CharacterSheetVisualTarget),static_cast<LONG>(characterSheet.VisualTarget()));
    int32_t uiWidth = 0, uiHeight = 0;
    if (shown && reader.Read(ui + 0xf8, uiWidth) && reader.Read(ui + 0xfc, uiHeight)) MeterOverlayUiSize(uiWidth, uiHeight);
    else MeterOverlayUiSize(0, 0);
    CooldownFrame cooldowns;
    if (shown && MeterOverlayCooldownsEnabled()) NativeCooldowns(reader,image).Sample(cooldowns);
    MeterOverlayCooldowns(&cooldowns);
    if (updated) MeterOverlayUpdate(&snapshot, sizeof(snapshot));
    std::array<float, 4> rect{};
    if (shown) reader.Menu(rect, ExpandMenu);
    MeterOverlayMenu(rect[0], rect[1], rect[2], rect[3]);
}

static void SyncMenuFrame(const NativeReader& reader) {
    const uintptr_t frame = reader.MenuFrame();
    uint32_t frameFlags = 0;
    const bool frameVisible = !MeterOverlayAddonsOpen();
    if (hiddenMenuFrame != frame) hiddenMenuFrame = 0;
    if (frame && reader.Read(frame + 0xb4, frameFlags) && (!frameVisible || hiddenMenuFrame == frame)) {
        using Visibility = void (__thiscall*)(void*, bool);
        if (((frameFlags & 8) != 0) != frameVisible) {
            reinterpret_cast<Visibility>(image + 0x280100)(reinterpret_cast<void*>(frame), frameVisible);
            hiddenMenuFrame = frameVisible ? 0 : frame;
        } else if (frameVisible) hiddenMenuFrame = 0;
    }
}

static void Observe(unsigned kind, const HookRegisters& registers) {
    NativeReader reader(image, CopyMemoryChecked, LookupLabel);
    if (kind == 8 || kind == 9) {
        std::lock_guard<std::recursive_mutex> lock(stateGate);
        if (kind == 8) mythicSounds.Drop(reader,image,registers.ebx,GetTickCount64());
        else if (reader.PlayerId(registers.ebx)) mythicSounds.InventoryDrop(reader,registers.esi,GetTickCount64());
        return;
    }
    if (kind == 0) {
        pending = {};
        if (!InterlockedCompareExchange(&collecting, 0, 0)) return;
        pending.unit = registers.ebx;
        pending.damage = registers.ebp;
        pending.valid = reader.Read(pending.unit + 0x2f0, pending.hp);
        if (!pending.valid) Fault("Measurement stopped: HP sample unavailable");
        return;
    }
    if (kind == 1) {
        const auto before = pending;
        pending = {};
        if (!before.valid || !InterlockedCompareExchange(&collecting, 0, 0)) return;
        if (before.unit != registers.ebx || before.damage != registers.ebp) { Fault("Measurement stopped: unmatched HP commit"); return; }
        MeterHit hit;
        hit.before = before.hp;
        if (!reader.Read(before.unit + 0x2f0, hit.after) || !reader.Read(before.damage + 0x38, hit.amount) || !reader.Read(before.damage + 0x40, hit.element) || !reader.Read(before.damage + 0x3e, hit.damageClass)) { Fault("Measurement stopped: damage unavailable"); return; }
        uint8_t damageFlags = 0;
        hit.criticalKnown = reader.Read(before.damage + 0x41,damageFlags);
        hit.critical = hit.criticalKnown && (damageFlags & 1);
        const uintptr_t producer = reader.Pointer(before.damage + 0x30);
        hit.criticalSource = reader.CriticalSource(producer,hit.damageClass);
        const auto source = reader.Pointer(before.damage + 0x2c);
        if (source == before.unit) return;
        hit.sourceId = reader.OwnerId(source);
        hit.targetId = reader.PlayerId(before.unit);
        if (!hit.targetId) hit.targetPetOwnerId = reader.OwnerId(before.unit);
        if (!hit.sourceId && !hit.targetId && !hit.targetPetOwnerId) return;
        hit.pet = hit.sourceId && !reader.PlayerId(source);
        const auto attacker=reader.Identity(source), target=reader.Identity(before.unit);
        hit.sourceKey=attacker.first; hit.sourceName=attacker.second;
        hit.targetKey=target.first; hit.targetName=target.second;
        const auto skill = reader.Skill(reader.Pointer(before.damage + 0x30));
        hit.skillKey = skill.first;
        hit.skillName = skill.second;
        std::lock_guard<std::recursive_mutex> lock(stateGate);
        const uint64_t now = GetTickCount64();
        meter.Hit(hit, now);
        history.Hit(hit, now);
        if (!meter.Enabled()) InterlockedExchange(&collecting, 0);
        return;
    }
    if (kind == 2) {
        std::lock_guard<std::recursive_mutex> lock(stateGate);
        mythicSounds.Reset(reader,image);
        meter.Zone();
        history.LeaveZone();
        inWorld = false;
        uiPrepared = false;
        nextRoster = nextSnapshot = 0;
        pending = {};
        InterlockedExchange(&collecting, 0);
        reportStatus = partyReports.Pending() ? "Report cancelled by map transition." : "";
        partyReports.Cancel();
        MeterOverlayWorld(false);
        return;
    }
    if (kind == 3) {
        if (!(registers.eax & 255)) Fault("Client reported a sync error; reset meter after recovery");
        return;
    }
    if (kind == 5) { MeterOverlayInvalidate(); return; }
    if (kind == 4) {
        if (!uiPrepared || !reader.InWorld()) RefreshUi(reader);
        MeterOverlayEndFrame();
        SyncMenuFrame(reader);
        uiPrepared = false;
        return;
    }
    const uintptr_t ui = reader.Pointer(image + 0x5314b0);
    if (!ui) return;
    if (kind == 6) {
        const uintptr_t control = reader.Pointer(registers.esp + 8);
        if (control == ui) {
            RefreshUi(reader);
            uiPrepared = true;
            return;
        }
        if (!uiPrepared || !control || control != reader.UiForeground()) return;
        const uintptr_t graphics = reader.Pointer(image + 0x533a44);
        const auto device = reinterpret_cast<IDirect3DDevice9*>(reader.Pointer(graphics + 0x1c));
        MeterOverlayLayer(device,AddonUiLayer::Meter);
    } else if (kind == 7 && uiPrepared) {
        AddonUiLayer layer;
        if (registers.edi == reader.Pointer(ui + 0x22c)) layer = AddonUiLayer::Hotbar;
        else if (registers.edi == reader.Pointer(ui + 0x26c)) layer = AddonUiLayer::Effects;
        else if (registers.edi == reader.Pointer(ui + 0x1dc)) layer = AddonUiLayer::Menu;
        else if (registers.edi == reader.Pointer(ui + 0x270)) layer = AddonUiLayer::CharacterSheet;
        else return;
        if (!reader.InWorld()) return;
        if (layer == AddonUiLayer::CharacterSheet) {
            const auto frame = MeterOverlayCharacterSheetEnabled() ? characterSheet.Frame(reader,image,GetTickCount64()) : CharacterSheetFrame{};
            MeterOverlayCharacterSheet(&frame);
        }
        if (layer == AddonUiLayer::Effects) {
            EffectFrame effects;
            if (MeterOverlayEffectsEnabled()) NativeCooldowns(reader,image).SampleEffects(effects);
            MeterOverlayEffects(&effects);
        }
        const uintptr_t graphics = reader.Pointer(image + 0x533a44);
        const auto device = reinterpret_cast<IDirect3DDevice9*>(reader.Pointer(graphics + 0x1c));
        MeterOverlayLayer(device,layer);
    }
}

extern "C" void __cdecl MeterDispatch(unsigned kind, const HookRegisters* registers) {
    const DWORD error = GetLastError();
    if (InterlockedCompareExchange(&ready, 0, 0)) {
        try { Observe(kind, *registers); }
        catch (...) { try { Fault("Measurement stopped: observer failure"); } catch (...) { InterlockedExchange(&collecting, 0); } }
    }
    SetLastError(error);
}

struct HookSpec { uintptr_t rva; const char* bytes; void* handler; void** original; };
static HookSpec hooks[] = {
    {0x281000, "6aff68387c7c0064a100000000506489250000000083ec10", reinterpret_cast<void*>(MeterUiBegin), &MeterUiBeginOriginal},
    {0x280fc5, "8b4b342b4b30c1f90285c976168b43348bd02b5330f7c2fc", reinterpret_cast<void*>(MeterUiControl), &MeterUiControlOriginal},
    {0x10bfd8, "8b83f00200008b4d383bc1760a2bc18983f0020000", reinterpret_cast<void*>(MeterBefore), &MeterBeforeOriginal},
    {0x10bffc, "8a453e3c040f84080100008b8b34010000", reinterpret_cast<void*>(MeterCommit), &MeterCommitOriginal},
    {0x1fc510, "6aff64a10000000068589d7d005064892500000000", reinterpret_cast<void*>(MeterLoading), &MeterLoadingOriginal},
    {0x1ddafa, "8b8c243c0800005e5d64890d000000005b", reinterpret_cast<void*>(MeterValidate), &MeterValidateOriginal},
    {0x2e113a, "8b461c8b088b51446a006a006a006a0050ffd2", reinterpret_cast<void*>(MeterPresent), &MeterPresentOriginal},
    {0x2e4060, "515355568bf033db57389e29050000", reinterpret_cast<void*>(MeterResources), &MeterResourcesOriginal},
    {0x86bf0, "8b8bfc0000008bbbf80000008b54241083ec088bc4c70096", reinterpret_cast<void*>(LootLabelHook), &LootLabelOriginal},
    {0xbb22a, "8b4424308844241188642410c64424120184c0750484e474", reinterpret_cast<void*>(NameplateOptionsHook), &NameplateOptionsOriginal},
    {0x288a81, "8b74242c890685c0740c83c004ba01000000f00fc110c744", reinterpret_cast<void*>(NameplateCreateHook), &NameplateCreateOriginal},
    {0x288e5b, "8d4c241c51518bc48964243c893085f6740c8d5604b80100", reinterpret_cast<void*>(NameplateBarsHook), &NameplateBarsOriginal},
    {0x288e94, "8d4c241c51518bc48964243c893085f6740c8d5604b80100", reinterpret_cast<void*>(NameplateNameHook), &NameplateNameOriginal},
    {0x288ecc, "8d44241c50518bc48964243c893085f6740c8d4e04ba0100", reinterpret_cast<void*>(NameplatePosseHook), &NameplatePosseOriginal},
    {0x2888e0, "895c24248d47048bcbf00fc108750a8b178b026a018bcfff", reinterpret_cast<void*>(NameplateRetireHook), &NameplateRetireOriginal},
    {0x28f270, "558bec83e4f864a1000000006aff6880077d00508b4508", reinterpret_cast<void*>(CharacterSheetVisualHook), &CharacterSheetVisualOriginal},
    {0x18ac2e, "c68301010000016a01e8a4f5ffff", reinterpret_cast<void*>(MythicDropHook), &MythicDropOriginal},
    {0x18a081, "8b839c0000005081c3900000005356", reinterpret_cast<void*>(MythicInventoryHook), &MythicInventoryOriginal},
};

static bool Matches(const HookSpec& hook) {
    const size_t count = std::strlen(hook.bytes) / 2;
    unsigned char actual[64]{};
    if (count > sizeof(actual) || !CopyMemoryChecked(image + hook.rva, actual, count)) return false;
    const auto hex = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    for (size_t i = 0; i < count; ++i) if (actual[i] != ((hex(hook.bytes[i * 2]) << 4) | hex(hook.bytes[i * 2 + 1]))) return false;
    return true;
}

static bool InstallHooks() {
    for (const auto& hook : hooks) if (!Matches(hook)) return false;
    if (!Matches({0x27fed0, "8b91fc0000008b4424043b5004", nullptr, nullptr}) || !Matches({0x27fc60, "8b91f00000008b4424043b10", nullptr, nullptr})) return false;
    if (!Matches({0x86f17,"8b44241083c008894424103b4424300f8514fcffff8b7424",nullptr,nullptr}) ||
        !Matches({0x1ffca0,"64a1000000006aff68636f7c0050648925000000008b8394",nullptr,nullptr}) ||
        !Matches({0x22c260,"518b0085c074048b08eb0233c985c0740583c004eb05b806",nullptr,nullptr})) return false;
    if (!Matches({0x281540,"83ec14535556578bf98b87b4000000c1e803897c2414a801",nullptr,nullptr})) return false;
    if (!Matches({0x288f04,"c7842428010000ffffffff85f674168d560483c8fff00fc1",nullptr,nullptr})) return false;
    static NativePatchSet patches;
    const size_t lengths[] = {7,6,6,5,8,7,5,6,6,8,6,6,6,6,7,6,7,6};
    std::array<NativePatchSpec,std::size(hooks)> specs{};
    for (size_t i=0;i<specs.size();++i) specs[i]={reinterpret_cast<void*>(image+hooks[i].rva),hooks[i].handler,hooks[i].original,hooks[i].bytes,lengths[i]};
    InterlockedExchange(&ready, 1);
    if (patches.Install(specs.data(),specs.size())) return true;
    InterlockedExchange(&ready, 0);
    return false;
}

static BOOL CALLBACK InitializeAddon(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t executable[32768]{};
    const DWORD chars = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
    if (!chars || chars >= std::size(executable)) return TRUE;
    const std::filesystem::path path(executable);
    if (_wcsicmp(path.filename().c_str(), L"DungeonRunners.exe") != 0) return TRUE;
    std::filesystem::path status;
    try {
        const auto directory = path.parent_path();
        const auto addon = directory / L"Addons" / L"DamageMeter";
        std::filesystem::create_directories(addon);
        status = addon / L"status.txt";
        const auto archive = ArchiveMeterReports(directory, addon / L"reports", [](const auto& file) { return FileHash(file, BCRYPT_SHA256_ALGORITHM, 32); });
        const auto digest = FileHash(path, BCRYPT_SHA256_ALGORITHM, 32);
        if (digest != "634f2c6789d9dc69df22c61e0d221aebe4bcc4bd13f8d36c4cd78edc31cf196b" && digest != "f16f47302fa58ea30f4509363df877a3601adc85f99cdcbc3d27e9ce84ae1da8") throw std::runtime_error("Unsupported client image; addon disabled");
        if (!VerifyCatalog(directory)) throw std::runtime_error("Skill data identity differs; addon disabled");
        image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto settings = (addon / L"ui.ini").u8string();
        mythicSounds.Configure(directory / L"Addons" / L"MythicDropSounds");
        history.Open(addon / L"history.bin");
        if (!MeterOverlayStart(settings.c_str())) throw std::runtime_error("UI initialization failed; addon disabled");
        MeterOverlayWorld(false);
        MeterOverlayInputTest(OverlayInput);
        MeterOverlayHotkeyTest(OverlayHotkey);
        MeterOverlayCharacterInputTest(CharacterInput);
        InterlockedExchange(&nameplateMask,static_cast<LONG>(MeterOverlayNameplates()));
        if (!InstallHooks()) throw std::runtime_error("Native hook validation failed; addon disabled");
        std::ofstream output(status, std::ios::trunc);
        output << "Addons loaded inside DungeonRunners.exe\nClient SHA256: " << digest << "\nHooks: " << std::size(hooks) << "\nSkill labels: " << std::size(SkillLabels) << "\nZone definitions: " << std::size(DungeonZones) << "\n";
        HMODULE ownModule = nullptr;
        wchar_t ownPath[32768]{};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&InitializeAddon), &ownModule) && GetModuleFileNameW(ownModule, ownPath, static_cast<DWORD>(std::size(ownPath))))
            output << "Addon SHA256: " << FileHash(ownPath, BCRYPT_SHA256_ALGORITHM, 32) << "\nAddon base: " << std::hex << reinterpret_cast<uintptr_t>(ownModule) << std::dec << '\n';
        SYSTEMTIME now{};
        GetSystemTime(&now);
        output << "Process: " << GetCurrentProcessId() << "\nStarted UTC: " << now.wYear << '-' << now.wMonth << '-' << now.wDay << 'T' << now.wHour << ':' << now.wMinute << ':' << now.wSecond << "\n";
        output << "Crash archive: copied=" << archive.copied << " existing=" << archive.existing << " skipped=" << archive.skipped << " errors=" << archive.errors << "\n";
    } catch (const std::exception& error) {
        if (!status.empty()) { std::ofstream output(status, std::ios::trunc); output << error.what() << '\n'; }
    } catch (...) {}
    return TRUE;
}

extern "C" BOOL WINAPI DungeonRunnersAddonsInitialize(uint32_t version) {
    if (version != DungeonRunnersAddonsApiVersion) return FALSE;
    try { InitOnceExecuteOnce(&bootstrap, InitializeAddon, nullptr, nullptr); } catch (...) { return FALSE; }
    return InterlockedCompareExchange(&ready, 0, 0) ? TRUE : FALSE;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_DETACH) { InterlockedExchange(&ready, 0); InterlockedExchange(&collecting, 0); }
    return TRUE;
}
