#pragma once
#include "native_reader.h"
#include "move_layout.h"
#include <vector>

class NativeMoveLayout {
public:
    using Geometry = std::array<int32_t,4>;
    using Setter = bool (*)(uintptr_t,const Geometry&,unsigned,bool);
    using Layout = bool (*)(uintptr_t);
private:
    struct Entry {
        uintptr_t node = 0, parent = 0, type = 0;
        Geometry base{}, applied{};
        bool captured = false, changed = false;
    };
    std::array<Entry,9> entries{};
    struct ScaledPart {
        uintptr_t node = 0, parent = 0, type = 0;
        Geometry base{}, applied{};
    };
    std::array<std::vector<ScaledPart>,9> scaledParts;
    uintptr_t root = 0, playerRoot = 0, playerPanel = 0;
    MoveLayoutSettings settings;
    MoveLayoutFrame frame;
    bool enabled = false;

    static bool GeometryOf(const NativeReader& r,uintptr_t node,Geometry& value,bool empty = false) {
        if (!node || !r.Read(node+0xf0,value)) return false;
        return std::abs(int64_t(value[0])) <= 16384 && std::abs(int64_t(value[1])) <= 16384 && value[2] >= (empty ? 0 : 1) && value[2] <= 16384 && value[3] >= (empty ? 0 : 1) && value[3] <= 16384;
    }
    static bool Origin(const NativeReader& r,uintptr_t node,uintptr_t ui,int32_t& x,int32_t& y,bool& visible) {
        x = y = 0; visible = true;
        std::array<uintptr_t,24> seen{};
        unsigned count = 0;
        while (node && node != ui) {
            uint32_t flags = 0;
            Geometry geometry;
            if (count == seen.size() || std::find(seen.begin(),seen.begin()+count,node) != seen.begin()+count || !GeometryOf(r,node,geometry) || !r.Read(node+0xb4,flags)) return false;
            seen[count++] = node;
            x += geometry[0]; y += geometry[1]; visible = visible && (flags&8);
            node = r.Pointer(node+0x14);
        }
        return node == ui && std::abs(int64_t(x)) <= 32768 && std::abs(int64_t(y)) <= 32768;
    }
    static uintptr_t Find(const NativeReader& r,uintptr_t root,const char* name) {
        std::array<uintptr_t,256> queue{};
        if (!root) return 0;
        queue[0] = root;
        unsigned count = 1;
        for (unsigned i = 0; i < count; ++i) {
            const auto node = queue[i];
            if (r.String(node+0x10,96) == name) return node;
            for (auto child = r.Pointer(node+0x18); child; child = r.Pointer(child+0x20)) {
                if (count == queue.size() || r.Pointer(child+0x14) != node || std::find(queue.begin(),queue.begin()+count,child) != queue.begin()+count) return 0;
                queue[count++] = child;
            }
        }
        return 0;
    }
    static bool Control(const NativeReader& r,uintptr_t node,uintptr_t image) {
        const auto type = r.Pointer(node);
        return type >= image+0x430000 && type < image+0x530000 && r.Pointer(type+0xa4) == image+0x27fc60 && r.Pointer(type+0xac) == image+0x27fed0;
    }
    void Select(const NativeReader& r,uintptr_t image,unsigned index,uintptr_t node) {
        auto& entry = entries[index];
        if (!Control(r,node,image)) node = 0;
        const auto parent = node ? r.Pointer(node+0x14) : 0, type = node ? r.Pointer(node) : 0;
        if (entry.node != node || entry.parent != parent || entry.type != type) { entry = {}; entry.node = node; entry.parent = parent; entry.type = type; }
    }
    static MoveRect Absolute(const Geometry& geometry,int32_t x,int32_t y) {
        return {float(x+geometry[0]),float(y+geometry[1]),float(geometry[2]),float(geometry[3])};
    }
    static Geometry Local(MoveRect rect,int32_t x,int32_t y) {
        return {int32_t(std::lround(rect.x))-x,int32_t(std::lround(rect.y))-y,int32_t(std::lround(rect.width)),int32_t(std::lround(rect.height))};
    }
    void Apply(unsigned index,const Geometry& desired,const Geometry& current,Setter setter) {
        auto& entry = entries[index];
        if (desired == current) return;
        const bool resized = desired[2] != current[2] || desired[3] != current[3];
        if (setter(entry.node,desired,index,resized)) entry.applied = desired;
    }
    void ScaleChildren(const NativeReader& r,uintptr_t image,unsigned index,Setter setter,Layout layout) {
        auto& entry = entries[index];
        if (!entry.changed || !entry.captured || entry.base[2] <= 0 || entry.base[3] <= 0 ||
            (entry.base[2] == entry.applied[2] && entry.base[3] == entry.applied[3])) return;
        const float sx = float(entry.applied[2])/entry.base[2], sy = float(entry.applied[3])/entry.base[3];
        if (sx < .1f || sy < .1f || sx > 8 || sy > 8) return;
        std::array<uintptr_t,384> queue{};
        queue[0] = entry.node;
        unsigned count = 1;
        std::array<ScaledPart,384> parts{};
        unsigned partCount = 0;
        bool valid = true;
        for (unsigned i = 0; i < count && valid; ++i) {
            const auto node = queue[i];
            if (Control(r,node,image)) {
                uint32_t flags = 0;
                Geometry rect;
                if (!r.Read(node+0xb4,flags) || ((flags&0x40) && !layout(node)) || !GeometryOf(r,node,rect,true)) { valid = false; break; }
                auto applied = rect;
                if (i) {
                    for (unsigned axis = 0; axis < 4; ++axis) applied[axis] = int32_t(std::lround(rect[axis]*(axis%2 ? sy : sx)));
                    parts[partCount++] = {node,r.Pointer(node+0x14),r.Pointer(node),rect,applied};
                }
            }
            for (auto child = r.Pointer(node+0x18); child; child = r.Pointer(child+0x20)) {
                if (count == queue.size() || r.Pointer(child+0x14) != node || std::find(queue.begin(),queue.begin()+count,child) != queue.begin()+count) { valid = false; break; }
                queue[count++] = child;
            }
        }
        setter(entry.node,entry.applied,MovePanelCount,true);
        if (!valid) return;
        auto& saved = scaledParts[index];
        saved.clear();
        saved.push_back({entry.node,entry.parent,entry.type,entry.base,entry.applied});
        for (unsigned i = 0; i < partCount; ++i) {
            const auto& part = parts[i];
            if (part.base != part.applied && setter(part.node,part.applied,MovePanelCount,true)) saved.push_back(part);
        }
    }
public:
    uintptr_t DrawTarget(unsigned index) const {
        const unsigned entry = index < 3 ? index : index+2;
        return index < 6 && (enabled || entries[entry].changed) ? entries[entry].node : 0;
    }
    int BeginDraw(const NativeReader& r,uintptr_t node,Setter setter,Geometry& natural,Geometry& displayed) {
        for (unsigned i : {0u,1u,2u,5u,6u,7u}) {
            const auto& entry = entries[i];
            if (entry.node != node || !entry.changed || !entry.captured || entry.base[2] < 1 || entry.base[3] < 1 ||
                r.Pointer(node) != entry.type || r.Pointer(node+0x14) != entry.parent) continue;
            natural = entry.applied; natural[2] = entry.base[2]; natural[3] = entry.base[3];
            displayed = entry.applied;
            if (natural == displayed || !setter(node,natural,MovePanelCount,true)) return -1;
            return static_cast<int>(i);
        }
        return -1;
    }
    void EndDraw(const NativeReader& r,uintptr_t image,unsigned index,Setter setter,Layout layout) {
        if (index < 3 || (index >= 5 && index <= 7)) ScaleChildren(r,image,index,setter,layout);
    }
    void Reset() { entries = {}; for (auto& parts : scaledParts) parts.clear(); root = playerRoot = playerPanel = 0; frame = {}; enabled = false; }
    void BeginFrame(const NativeReader& r,uintptr_t image,uintptr_t control,Setter setter) {
        if (!root || control != root || r.Pointer(image+0x5314b0) != root || !setter) return;
        for (auto& parts : scaledParts) {
            for (const auto& part : parts) {
                if (r.Pointer(part.node) != part.type || r.Pointer(part.node+0x14) != part.parent || !Control(r,part.node,image)) continue;
                int32_t x = 0, y = 0; bool visible = false;
                if (!Origin(r,part.parent,root,x,y,visible)) continue;
                Geometry current;
                if (!GeometryOf(r,part.node,current,true)) continue;
                auto restored = current;
                for (unsigned axis = 0; axis < 4; ++axis) if (current[axis] == part.applied[axis]) restored[axis] = part.base[axis];
                if (restored != current) setter(part.node,restored,MovePanelCount,true);
            }
            parts.clear();
        }
    }
    const MoveLayoutFrame& Frame() const { return frame; }
    void Prepare(const NativeReader& r,uintptr_t image,const MoveLayoutSettings& value,bool installed,bool world) {
        if (!installed && std::none_of(entries.begin(),entries.end(),[](const Entry& e) { return e.changed; })) { Reset(); return; }
        const auto ui = r.Pointer(image+0x5314b0);
        if (root != ui) { Reset(); root = ui; }
        int32_t width = 0, height = 0;
        if (!ui || !r.Read(ui+0xf8,width) || !r.Read(ui+0xfc,height) || width < 320 || height < 200 || width > 16384 || height > 16384) { Reset(); return; }
        settings = value; enabled = installed && value.enabled && world;
        frame.width = float(width); frame.height = float(height); frame.visible = {};
        const auto status = r.Pointer(ui+0x274);
        if (status != playerRoot || !playerPanel) { playerRoot = status; playerPanel = Find(r,status,"PlayerHealthAndMana"); }
        Select(r,image,0,playerPanel);
        Select(r,image,1,r.Pointer(ui+0x25c));
        Select(r,image,2,r.Pointer(ui+0x1f8));
        Select(r,image,3,r.Pointer(ui+0x21c));
        const auto map = r.Pointer(ui+0x290);
        const bool smallMap = map && r.Pointer(map+0x14) == r.Pointer(ui+0x184) && !r.Pointer(map+0x11c);
        Select(r,image,4,smallMap ? map : 0);
        Select(r,image,5,r.Pointer(ui+0x29c));
        Select(r,image,6,r.Pointer(ui+0x2a0));
        Select(r,image,7,r.Pointer(ui+0x27c));
        Select(r,image,8,r.Pointer(r.Pointer(ui+0x21c)+0x2e8));
    }
    void BeforeChildren(const NativeReader& r,uintptr_t image,uintptr_t parent,Setter setter,Layout layout) {
        if (!root || !parent || !setter || !layout) return;
        for (unsigned index = 0; index < entries.size(); ++index) {
            auto& entry = entries[index];
            if (!entry.node || entry.parent != parent || r.Pointer(entry.node+0x14) != parent || r.Pointer(entry.node) != entry.type || !Control(r,entry.node,image)) continue;
            int32_t x = 0, y = 0;
            bool visible = false;
            if (!Origin(r,parent,root,x,y,visible)) continue;
            uint32_t flags = 0;
            if (!r.Read(entry.node+0xb4,flags)) continue;
            const unsigned owner = index < MovePanelCount ? index : index == 8 ? 3 : 4;
            const bool custom = enabled && settings.panels[owner].custom && (owner != 4 || entries[4].node);
            if ((custom || entry.changed) && (flags&0x40) && !layout(entry.node)) continue;
            Geometry current{};
            if (!GeometryOf(r,entry.node,current)) continue;
            if (!entry.captured || (entry.changed && current != entry.applied)) {
                if (!entry.changed || current[0] != entry.applied[0]) entry.base[0] = current[0];
                if (!entry.changed || current[1] != entry.applied[1]) entry.base[1] = current[1];
                if (!entry.changed || current[2] != entry.applied[2]) entry.base[2] = current[2];
                if (!entry.changed || current[3] != entry.applied[3]) entry.base[3] = current[3];
                entry.captured = true;
            } else if (!entry.changed) entry.base = current;
            Geometry desired = current;
            if (custom && index < MovePanelCount) {
                const auto bounds = ResolveMoveRect(index,settings.panels[index],Absolute(entry.base,x,y),frame.width,frame.height);
                if (bounds.Valid()) desired = Local(bounds,x,y);
            } else if (custom && entries[owner].captured) {
                const auto& anchor = entries[owner];
                int32_t ax = 0, ay = 0; bool av = false;
                if (Origin(r,anchor.parent,root,ax,ay,av)) {
                    const auto before = Absolute(anchor.base,ax,ay);
                    const auto after = ResolveMoveRect(owner,settings.panels[owner],before,frame.width,frame.height);
                    auto bounds = Absolute(entry.base,x,y);
                    if (index == 8) { bounds.x = after.x+after.width; bounds.y = after.y+(after.height-bounds.height)*0.5f; }
                    else {
                        const float sx = after.width/before.width, sy = after.height/before.height;
                        bounds.x = after.x+(bounds.x-before.x)*sx;
                        bounds.y = after.y+(bounds.y-before.y)*sy;
                        bounds.width *= sx; bounds.height *= sy;
                    }
                    if (bounds.Valid()) desired = Local(bounds,x,y);
                }
            } else if (entry.changed) desired = entry.base;
            Apply(index,desired,current,setter);
            entry.changed = custom;
            if (!entry.changed) entry.applied = {};
            else if (desired == current) entry.applied = current;
            if (index < MovePanelCount) {
                frame.natural[index] = Absolute(entry.base,x,y);
                Geometry actual;
                if (GeometryOf(r,entry.node,actual)) frame.panels[index] = Absolute(actual,x,y);
                frame.visible[index] = visible && (flags&8);
            }
        }
    }
};
