#pragma once
#include "native_meter.h"
#include "addon_features.h"
#include "native_ui.h"
#include <cctype>
#include <cstdlib>
#include <functional>

class NativeReader {
    uintptr_t image;
    bool (*read)(uintptr_t, void*, size_t);
    const char* (*label)(const char*);
public:
    NativeReader(uintptr_t base, bool (*reader)(uintptr_t, void*, size_t), const char* (*lookup)(const char*)) : image(base), read(reader), label(lookup) {}
    template<class T> bool Read(uintptr_t address, T& value) const { return address >= 0x10000 && read(address, &value, sizeof(value)); }
    uintptr_t Pointer(uintptr_t address) const { uint32_t result = 0; Read(address, result); return result; }
    bool KindOf(uintptr_t node,uintptr_t classGlobal) const {
        const uintptr_t function = Pointer(Pointer(node)+8);
        if (function < image+0x1000 || function >= image+0x430000) return false;
        uint8_t opcode = 0, end = 0;
        uint32_t address = 0, index = 0, mask = 0;
        if (!Read(function,opcode) || opcode != 0xa1 || !Read(function+1,address) ||
            address < image+0x500000 || address >= image+0x540000 || !Read(function+5,end) || end != 0xc3) return false;
        const uintptr_t type = Pointer(address), wanted = Pointer(classGlobal);
        if (!type || !wanted) return false;
        if (type == wanted) return true;
        return Read(wanted+0x98,index) && index < 768 && Read(type+0x38+(index/32)*4,mask) && (mask & (1u << (index%32)));
    }
    uint8_t CriticalSource(uintptr_t producer,uint8_t damageClass) const {
        if (damageClass == 4) return 0;
        const uintptr_t type = producer ? Pointer(producer) : 0;
        if (type == image+0x481f18) return 2;
        if (type == image+0x485ec0) return 1;
        if (type && KindOf(producer,image+0x530a40)) return 2;
        if (type && KindOf(producer,image+0x530aac)) return 1;
        return damageClass == 1 || damageClass == 2 ? 1 : 0;
    }
    std::string String(uintptr_t address, uint32_t limit) const {
        const uintptr_t block = Pointer(address);
        uint32_t size = 0;
        if (!Read(block, size) || !size || size > limit) return {};
        std::string result(size, '\0');
        if (!read(block + 4, &result[0], size) || result.find('\0') != std::string::npos) return {};
        return result;
    }
    uint32_t OwnerId(uintptr_t unit) const {
        const uintptr_t type = Pointer(unit);
        if (type != image + 0x46de00 && type != image + 0x472940) return 0;
        const uintptr_t player = Pointer(unit + 0x14);
        uint32_t id = 0;
        if (player && Pointer(player) == image + 0x49b468) Read(player + 0x98, id);
        return id;
    }
    uint32_t PlayerId(uintptr_t unit) const {
        return Pointer(unit) == image + 0x46de00 ? OwnerId(unit) : 0;
    }
    std::pair<uint64_t,std::string> Identity(uintptr_t unit) const {
        const uint32_t playerId=PlayerId(unit);
        if (playerId) {
            std::string name=String(Pointer(unit+0x14)+0x10,128);
            if (name.empty()) name="Player "+std::to_string(playerId);
            return {playerId,name};
        }
        if (!unit) return {UINT64_MAX,"Environment"};
        std::string name;
        const uintptr_t table=Pointer(unit);
        if (table==image+0x472940) name=String(unit+0x330,256);
        const uintptr_t definition=Pointer(unit+0x5c);
        uint8_t namingFlags=0;
        if (name.empty() && table==image+0x472940 && definition && Read(definition+0x1a8,namingFlags) && !(namingFlags&1)) {
            name=String(definition+0x70,512);
            if (name.find(',')!=std::string::npos) {
                uint8_t level=0; Read(unit+0x314,level);
                size_t begin=0; std::string selected;
                const auto trim=[](std::string value) {
                    const auto first=value.find_first_not_of(" \t\r\n");
                    if (first==std::string::npos) return std::string();
                    return value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);
                };
                while (begin<name.size()) {
                    const size_t split=name.find(',',begin);
                    if (split==std::string::npos) break;
                    const size_t end=name.find(',',split+1);
                    const std::string rank=trim(name.substr(begin,split-begin));
                    char* tail=nullptr; const long minimum=std::strtol(rank.c_str(),&tail,10);
                    if (tail==rank.c_str() || *tail || minimum<0 || minimum>65535 || (begin ? minimum : 1)>level) break;
                    selected=trim(name.substr(split+1,end==std::string::npos ? end : end-split-1));
                    if (end==std::string::npos) break;
                    begin=end+1;
                }
                name=selected;
            }
        }
        if (name.empty()) name="Unidentified creature";
        uint64_t key=14695981039346656037ull;
        for (unsigned char c:name) { key^=c; key*=1099511628211ull; }
        return {key|0x8000000000000000ull,name};
    }
    bool IsGold(uintptr_t item) const { return item && Pointer(item) == image + 0x48f690; }
    bool InWorld() const {
        const uintptr_t ui = Pointer(image + 0x5314b0);
        uint32_t flags = 0, id = 0;
        if (!ui || !Read(ui + 0xb4,flags) || !(flags & 8)) return false;
        const uintptr_t zone = Pointer(ui + 0x1b4);
        if (!zone || Pointer(zone) != image + 0x4ab1b8 || !Pointer(zone + 0xf0)) return false;
        const uintptr_t player = Pointer(zone + 0xf8);
        return player && Pointer(player) == image + 0x49b468 && Pointer(player + 0x80) && Read(player + 0x98,id) && id;
    }
    uintptr_t UiForeground() const {
        const uintptr_t ui = Pointer(image + 0x5314b0);
        const uintptr_t foreground = ui ? Pointer(ui + 0x184) : 0;
        return foreground && foreground != ui && Pointer(foreground + 0x14) == ui ? foreground : 0;
    }
    bool OverlayInput(uintptr_t control,bool menuLayer) const {
        if (!InWorld()) return false;
        const uintptr_t ui = Pointer(image + 0x5314b0);
        if (Pointer(ui + 0x180)) return false;
        const uintptr_t chat = Pointer(ui + 0x21c);
        uint8_t dragging = 0;
        if (chat && (!Read(chat + 0x2ec,dragging) || dragging)) return false;
        if (control == ui) return true;
        const uintptr_t menu = menuLayer ? Pointer(ui + 0x1dc) : 0;
        const uintptr_t foreground = UiForeground();
        for (unsigned depth = 0; control && control != ui && depth < 32; ++depth) {
            if (menu && control == menu) return true;
            if (control == foreground) return false;
            const uintptr_t parent = Pointer(control + 0x14);
            if (parent == ui) {
                for (unsigned count = 0; foreground && control && count < 256; ++count) {
                    if (Pointer(control + 0x14) != ui) return false;
                    const uintptr_t next = Pointer(control + 0x20);
                    if (next == foreground) return true;
                    if (next == control) return false;
                    control = next;
                }
                return false;
            }
            control = parent;
        }
        return false;
    }
    bool ZoneIdentity(std::string& key,uint32_t& seed) const {
        key.clear(); seed = 0;
        const uintptr_t ui = Pointer(image+0x5314b0), zone = ui ? Pointer(ui+0x1b4) : 0;
        if (!zone || Pointer(zone) != image+0x4ab1b8 || !Pointer(zone+0xf0) || !Read(zone+0xc8,seed)) return false;
        key = String(zone+0xc4,128);
        std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return !key.empty();
    }
    bool PartyChat(ReportContext& context) const {
        context = {};
        const uintptr_t ui = Pointer(image + 0x5314b0);
        uint32_t flags = 0;
        if (!ui || !Read(ui + 0xb4,flags) || !(flags & 8)) return false;
        const uintptr_t zone = Pointer(ui + 0x1b4), control = Pointer(ui + 0x21c);
        const uintptr_t self = zone ? Pointer(zone + 0xf8) : 0;
        const uintptr_t chat = control ? Pointer(control + 0x1b0) : 0;
        const uintptr_t group = Pointer(image + 0x530d3c);
        if (!self || Pointer(self) != image + 0x49b468 || !chat || Pointer(chat) != image + 0x4ac3a0 || !group) return false;
        const uintptr_t connection = Pointer(chat + 0x94);
        const uintptr_t begin = Pointer(group + 0xe0), end = Pointer(group + 0xe4);
        if (!connection || !begin || end < begin || end-begin < 8 || end-begin > 20 || (end-begin)%4) return false;
        context.identity = {ui,zone,self,group,chat,connection,Pointer(self + 0x98)};
        unsigned count = 0;
        bool containsSelf = false;
        for (uintptr_t at = begin; at != end; at += 4) {
            const uintptr_t member = Pointer(at), id = member ? Pointer(member + 0xc) : 0;
            if (!id) { context = {}; return false; }
            containsSelf |= id == context.identity[6];
            for (unsigned i = 0; i < count; ++i) if (context.identity[7+i] == id) { context = {}; return false; }
            context.identity[7+count++] = id;
        }
        if (!containsSelf) { context = {}; return false; }
        return true;
    }
    std::pair<std::string, std::string> Skill(uintptr_t effect) const {
        if (!effect) return {"direct", "Direct damage"};
        std::vector<uintptr_t> nodes{effect}, seen;
        std::string fallback;
        for (size_t index = 0; index < nodes.size() && seen.size() < 64; ++index) {
            const uintptr_t node = nodes[index];
            if (!node || std::find(seen.begin(), seen.end(), node) != seen.end()) continue;
            seen.push_back(node);
            const uintptr_t definition = Pointer(node + 0x58);
            std::string path = definition ? String(definition + 0x18, 512) : "";
            if (!path.empty()) {
                if (fallback.empty()) fallback = path;
                std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                for (size_t dot = path.rfind('.'); dot != std::string::npos; dot = path.rfind('.')) {
                    if (const char* name = label(path.c_str())) return {path, name};
                    path.resize(dot);
                }
            }
            if (nodes.size() < 128) { nodes.push_back(Pointer(node + 0x14)); nodes.push_back(Pointer(node + 0x54)); }
        }
        if (!fallback.empty()) return {fallback, fallback};
        return {"unattributed", "Unattributed effect"};
    }
    bool Party(uint32_t& selfId, std::vector<MeterMember>& members) const {
        const uintptr_t ui = Pointer(image + 0x5314b0);
        const uintptr_t zone = ui ? Pointer(ui + 0x1b4) : 0;
        const uintptr_t self = zone ? Pointer(zone + 0xf8) : 0;
        if (!self || Pointer(self) != image + 0x49b468 || !Read(self + 0x98, selfId) || !selfId) return false;
        members = {{selfId, String(self + 0x10, 128)}};
        const uintptr_t group = Pointer(image + 0x530d3c);
        if (group) {
            const uintptr_t begin = Pointer(group + 0xe0), end = Pointer(group + 0xe4);
            if (end < begin || end - begin > 20 || (end - begin) % 4) return false;
            for (uintptr_t at = begin; at != end; at += 4) {
                const uintptr_t member = Pointer(at);
                uint32_t id = 0;
                if (!member || !Read(member + 0xc, id) || !id) return false;
                auto existing = std::find_if(members.begin(), members.end(), [id](const auto& item) { return item.id == id; });
                if (existing == members.end()) members.push_back({id, String(member + 0x10, 128)});
            }
        }
        if (members.size() > MeterPlayers) return false;
        for (auto& member : members) if (member.name.empty()) member.name = "Player " + std::to_string(member.id);
        return true;
    }
    uintptr_t MenuFrame() const {
        const uintptr_t ui = Pointer(image + 0x5314b0);
        const uintptr_t menu = ui ? Pointer(ui + 0x1dc) : 0;
        const uintptr_t frame = menu ? Pointer(menu + 0x18) : 0;
        if (!frame || Pointer(menu + 0x14) != ui || Pointer(frame + 0x14) != menu || String(frame + 0x10, 128) != "FrameMedium") return 0;
        if (Pointer(Pointer(frame) + 0xb8) != image + 0x280100) return 0;
        return frame;
    }
    bool Menu(std::array<float, 4>& bounds, bool (*layout)(uintptr_t, uintptr_t, uintptr_t) = nullptr) const {
        bounds = {};
        const uintptr_t ui = Pointer(image + 0x5314b0);
        const uintptr_t menu = ui ? Pointer(ui + 0x1dc) : 0;
        uint32_t flags = 0;
        if (!ui || !Read(ui + 0xb4, flags) || !(flags & 8) || !menu || !Read(menu + 0xb4, flags) || !(flags & 8)) return false;
        uintptr_t exit = 0, back = 0;
        std::vector<uintptr_t> nodes{Pointer(menu + 0x18)}, seen;
        while (!nodes.empty() && seen.size() < 512) {
            const uintptr_t node = nodes.back(); nodes.pop_back();
            if (!node || std::find(seen.begin(), seen.end(), node) != seen.end()) continue;
            seen.push_back(node);
            const auto name = String(node + 0x10, 128);
            if (name == "Exit") exit = node;
            if (name == "Return") back = node;
            if (exit && back) break;
            if (nodes.size() < 1024) { nodes.push_back(Pointer(node + 0x20)); nodes.push_back(Pointer(node + 0x18)); }
        }
        std::array<int64_t, 4> a{}, b{};
        int32_t width = 0, height = 0;
        if (!exit || !back || !Rect(exit, ui, a) || !Rect(back, ui, b) || !Read(ui + 0xf8, width) || !Read(ui + 0xfc, height) || width < 320 || height < 200) return false;
        const uintptr_t frame = Pointer(back + 0x14);
        if (!frame || Pointer(exit + 0x14) != frame || Pointer(frame + 0x14) != menu || String(frame + 0x10, 128) != "FrameMedium") return false;
        if (a[0] != b[0] || a[2] != 141 || a[3] != 39 || b[0] < 0 || b[0] + b[2] > width || a[1] < 0) return false;
        int32_t menuHeight = 0, frameHeight = 0, backY = 0;
        if (!Read(menu + 0xfc, menuHeight) || !Read(frame + 0xfc, frameHeight) || !Read(back + 0xf4, backY)) return false;
        if (menuHeight == 240 && frameHeight == 240 && backY == 191 && b[2] == 141 && b[3] == 39) {
            if (b[1] - a[1] - a[3] != 17 || b[1] + b[3] + 39 > height) return false;
            if (layout) layout(menu, frame, back);
            return false;
        }
        if (menuHeight != 279 || frameHeight != 279 || backY != 230 || b[2] != 141 || b[3] != 39 || b[1] + b[3] > height) return false;
        const int64_t gap = b[1] - a[1] - a[3];
        if (gap != 56) return false;
        bounds = {static_cast<float>(b[0]) / width, static_cast<float>(a[1] + a[3]) / height, static_cast<float>(b[2]) / width, static_cast<float>(b[3]) / height};
        for (float value : bounds) if (value < 0 || value > 1) { bounds = {}; return false; }
        if (bounds[0] + bounds[2] > 1 || bounds[1] + bounds[3] > 1) { bounds = {}; return false; }
        return true;
    }
private:
    bool Rect(uintptr_t node, uintptr_t ui, std::array<int64_t, 4>& result) const {
        int32_t w = 0, h = 0;
        if (!Read(node + 0xf8, w) || !Read(node + 0xfc, h) || w <= 0 || h <= 0) return false;
        result = {0, 0, w, h};
        std::vector<uintptr_t> seen;
        while (node && node != ui && seen.size() < 16) {
            if (std::find(seen.begin(), seen.end(), node) != seen.end()) return false;
            seen.push_back(node);
            int32_t x = 0, y = 0;
            if (!Read(node + 0xf0, x) || !Read(node + 0xf4, y)) return false;
            result[0] += x; result[1] += y;
            node = Pointer(node + 0x14);
        }
        return node == ui;
    }
};
