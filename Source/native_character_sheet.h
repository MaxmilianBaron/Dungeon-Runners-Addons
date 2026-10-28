#pragma once
#include "native_reader.h"
#include "character_sheet.h"
#include "native_catalog.generated.h"

class NativeCharacterSheet {
public:
    using Geometry = std::array<int32_t,4>;
    using Setter = bool (*)(uintptr_t,const Geometry&);
private:
    struct Node {
        uintptr_t address = 0, parent = 0, type = 0, name = 0;
        Geometry before{}, after{};
    };
    uintptr_t sheet = 0, panel = 0, sampledUnit = 0;
    uintptr_t visual = 0, visualParent = 0, drawVisual = 0;
    std::array<Node,25> changes{};
    std::array<Node,4> anchors{};
    std::array<Node,5> resistanceLabels{}, resistanceValues{};
    unsigned count = 0;
    uint64_t nextLayout = 0, nextSample = 0;
    CharacterSheetFrame stats;

    static bool ReadNode(const NativeReader& r,uintptr_t node,Node& out) {
        out.address = node;
        out.parent = r.Pointer(node + 0x14);
        out.type = r.Pointer(node);
        out.name = r.Pointer(node + 0x10);
        if (!r.Read(node + 0xf0,out.before)) return false;
        out.after = out.before;
        return out.before[0] >= -4096 && out.before[0] <= 16384 && out.before[1] >= -4096 && out.before[1] <= 16384 &&
            out.before[2] > 0 && out.before[2] <= 16384 && out.before[3] > 0 && out.before[3] <= 16384;
    }

    static bool Matches(const NativeReader& r,const Node& node,const Geometry& expected) {
        Geometry value{};
        return node.address && r.Pointer(node.address) == node.type && r.Pointer(node.address + 0x14) == node.parent &&
            r.Pointer(node.address + 0x10) == node.name && r.Read(node.address + 0xf0,value) && value == expected;
    }

    bool Discover(const NativeReader& r,uintptr_t image,uintptr_t root) {
        struct Branch { uintptr_t node, parent; };
        std::array<Branch,256> pending{};
        std::array<uintptr_t,256> seen{};
        unsigned pendingCount = 1, visited = 0, labels = 0, dividers = 0, backgrounds = 0;
        pending[0] = {r.Pointer(root + 0x18),root};
        count = 0; anchors = {}; resistanceLabels = {}; resistanceValues = {}; panel = visual = visualParent = 0;
        while (pendingCount) {
            const auto branch = pending[--pendingCount];
            if (!branch.node) continue;
            if (visited == seen.size() || r.Pointer(branch.node + 0x14) != branch.parent ||
                std::find(seen.begin(),seen.begin() + visited,branch.node) != seen.begin() + visited) return false;
            seen[visited++] = branch.node;
            if (pendingCount + 2 > pending.size()) return false;
            pending[pendingCount++] = {r.Pointer(branch.node + 0x20),branch.parent};
            const auto type = r.Pointer(branch.node);
            if (type != image + 0x4ba0c0 && type != image + 0x449ee8) continue;
            pending[pendingCount++] = {r.Pointer(branch.node + 0x18),branch.node};
            const auto name = r.String(branch.node + 0x10,64);
            Node node;
            if (!ReadNode(r,branch.node,node)) return false;
            bool change = false;
            if (name == "bottomSide") panel = branch.node;
            const char* names[] = {"DefenseRatingLabel","DefenseRating","BlockingLabel","Blocking"};
            for (unsigned i = 0; i < anchors.size(); ++i) if (name == names[i]) {
                if (anchors[i].address || type != image + 0x449ee8) return false;
                anchors[i] = node;
            }
            if (type == image + 0x449ee8 && (name.find("Resist") != std::string::npos || name.find("Bonus") != std::string::npos)) {
                if (node.before[1] < 143 || node.before[1] > 225) return false;
                node.after[1] += CharacterSheetExtraHeight; ++labels; change = true;
                const char* resists[] = {"FireResist","IceResist","PoisonResist","ShadowResist","DivineResist"};
                for (unsigned i = 0; i < resistanceLabels.size(); ++i) {
                    if (name == std::string(resists[i]) + "Label") {
                        if (resistanceLabels[i].address) return false;
                        resistanceLabels[i] = node;
                    } else if (name == resists[i]) {
                        if (resistanceValues[i].address) return false;
                        resistanceValues[i] = node;
                    }
                }
            }
            if (type == image + 0x4ba0c0 && name == "Dividor" && node.before == Geometry{26,460,368,26}) {
                node.after[1] += CharacterSheetExtraHeight; ++dividers; change = true;
            }
            if (type == image + 0x4ba0c0 && ((name == "VisualLayer" && node.before == Geometry{2,-4,390,610}) ||
                (name == "SummaryPanel" && node.before == Geometry{0,-2,390,610}) || (name == "top" && node.before == Geometry{0,0,390,650}))) {
                if (name == "VisualLayer") {
                    visual = r.Pointer(branch.node + 0x18);
                    visualParent = branch.node;
                    uint8_t stretch = 1;
                    Geometry source{};
                    if (!visual || r.Pointer(visual) != image + 0x4bbfe0 || r.Pointer(visual + 0x14) != visualParent ||
                        !r.Read(visual + 0x88,stretch) || stretch || !r.Read(visual + 0x8c,source) || source != Geometry{}) return false;
                }
                node.after[2] = 400; node.after[3] += CharacterSheetExtraHeight; ++backgrounds; change = true;
            }
            if (change) {
                if (count == changes.size()) return false;
                changes[count++] = node;
            }
        }
        for (const auto& node : anchors) if (!node.address || node.parent != panel) return false;
        for (unsigned i = 0; i < resistanceLabels.size(); ++i)
            if (!resistanceLabels[i].address || !resistanceValues[i].address || resistanceLabels[i].parent != panel || resistanceValues[i].parent != panel) return false;
        const uintptr_t tab = r.Pointer(root + 0x14), viewport = r.Pointer(tab + 0x14);
        Node container;
        if (count + 1 != changes.size() || r.Pointer(tab) != image + 0x461388 ||
            r.Pointer(viewport) != image + 0x4ba0c0 || r.String(viewport + 0x10,64) != "Left" ||
            !ReadNode(r,viewport,container) || container.before[0] != 0 || container.before[2] != 440 || container.before[3] != 630) return false;
        container.after[3] = std::max(container.before[3],606 + CharacterSheetExtraHeight);
        changes[count++] = container;
        return panel && visual && labels == 20 && dividers == 1 && backgrounds == 3 && count == changes.size();
    }

    bool Active(const NativeReader& r,uintptr_t root) const {
        if (!sheet || sheet != root || count != changes.size()) return false;
        for (const auto& node : changes) if (!Matches(r,node,node.after)) return false;
        for (const auto& node : anchors) if (!Matches(r,node,node.before)) return false;
        return true;
    }

    void RefreshViewportPosition(const NativeReader& r,uintptr_t root) {
        if (!sheet || sheet != root || count != changes.size()) return;
        auto& node = changes.back();
        int32_t y = 0;
        if (!r.Read(node.address + 0xf4,y) || y == node.after[1] || y < -4096 || y > 16384) return;
        auto expected = node.after;
        expected[1] = y;
        if (Matches(r,node,expected)) node.before[1] = node.after[1] = y;
    }

    void Restore(const NativeReader& r,uintptr_t root,Setter setter) {
        if (sheet && root == sheet) {
            for (unsigned i = count; i; --i) {
                const auto& node = changes[i-1];
                if (Matches(r,node,node.after)) setter(node.address,node.before);
            }
        }
        sheet = panel = sampledUnit = visual = visualParent = drawVisual = 0; count = 0; nextSample = 0; stats = {};
    }

    static bool Origin(const NativeReader& r,uintptr_t node,uintptr_t ui,int64_t& x,int64_t& y,float& opacity) {
        x = y = 0; opacity = 1;
        std::array<uintptr_t,24> seen{};
        unsigned count = 0;
        while (node) {
            if (count == seen.size() || std::find(seen.begin(),seen.begin() + count,node) != seen.begin() + count) return false;
            seen[count++] = node;
            uint32_t flags = 0; float alpha = 0;
            if (!r.Read(node + 0xb4,flags) || !(flags & 8) || !r.Read(node + 0x14c,alpha) || !std::isfinite(alpha) || alpha <= 0 || alpha > 1) return false;
            opacity *= alpha;
            if (node == ui) return opacity > 0;
            int32_t dx = 0, dy = 0;
            if (!r.Read(node + 0xf0,dx) || !r.Read(node + 0xf4,dy) || std::abs(int64_t(dx)) > 16384 || std::abs(int64_t(dy)) > 16384) return false;
            x += dx; y += dy;
            node = r.Pointer(node + 0x14);
        }
        return false;
    }

    static uintptr_t Child(const NativeReader& r,uintptr_t parent,uintptr_t type,int32_t slot = -1) {
        uintptr_t node = r.Pointer(parent + 0x18);
        std::array<uintptr_t,256> seen{};
        unsigned count = 0;
        while (node) {
            if (count == seen.size() || r.Pointer(node + 0x14) != parent || std::find(seen.begin(),seen.begin() + count,node) != seen.begin() + count) return 0;
            seen[count++] = node;
            if (slot < 0 ? r.Pointer(node) == type : r.Pointer(node + 0x68) == static_cast<uintptr_t>(slot)) return node;
            node = r.Pointer(node + 0x20);
        }
        return 0;
    }

    void SampleMagicSkills(const NativeReader& r,uintptr_t image,uintptr_t unit,int32_t base,int32_t bonus) {
        const uintptr_t ui = r.Pointer(image+0x5314b0), list = r.Pointer(ui+0x22c), skills = r.Pointer(list+0x184);
        if (r.Pointer(list) != image+0x463c90 || r.Pointer(list+0x180) != unit || !skills || r.Pointer(skills+0x14) != unit) return;
        std::array<uintptr_t,256> seen{};
        std::array<bool,10> slots{};
        unsigned visited = 0, lines = 0;
        std::string text;
        char reference[32]{};
        std::snprintf(reference,sizeof(reference),"%.2f%%",SheetCriticalProbability(stats.magicCritical));
        for (uintptr_t node = r.Pointer(skills+0x18); node; node = r.Pointer(node+0x20)) {
            if (visited == seen.size() || r.Pointer(node+0x14) != skills || std::find(seen.begin(),seen.begin()+visited,node) != seen.begin()+visited) return;
            seen[visited++] = node;
            uint32_t slot = UINT32_MAX;
            if (!r.KindOf(node,image+0x5309ec) || !r.Read(node+0x68,slot) || slot < 100 || slot-100 >= slots.size() || slots[slot-100]) continue;
            slots[slot-100] = true;
            const auto skill = r.Skill(node);
            const auto end = std::end(SkillCriticals);
            auto effect = std::lower_bound(std::begin(SkillCriticals),end,skill.first.c_str(),[](const auto& row,const char* key) { return std::strcmp(row.path,key) < 0; });
            while (effect != end && skill.first == effect->path) {
                int32_t threshold = 0;
                if (!SheetMagicCritical(base,bonus,stats.pvp,threshold,effect->multiplier)) return;
                char chance[32]{};
                std::snprintf(chance,sizeof(chance),"%.2f%%",SheetCriticalProbability(threshold));
                if (!std::strcmp(chance,reference)) { ++effect; continue; }
                const char* component = std::strchr(effect->element,' ');
                char line[160]{};
                std::snprintf(line,sizeof(line),"%s%s%s%s: %s\n",skill.second.c_str(),component ? " (" : "",component ? component+1 : "",component ? ")" : "",chance);
                if (++lines > 12) { if (text.size()+3 < stats.magicSkills.size()) text += "..."; break; }
                if (text.size()+std::strlen(line) >= stats.magicSkills.size()-1) return;
                text += line;
                ++effect;
            }
            if (lines > 12) break;
        }
        if (!text.empty() && text.back() == '\n') text.pop_back();
        std::memcpy(stats.magicSkills.data(),text.data(),text.size());
    }

    void Sample(const NativeReader& r,uintptr_t image,uintptr_t unit) {
        stats = {};
        uint32_t team = 0;
        const uintptr_t player = r.Pointer(unit + 0x14);
        const bool contextValid = player && r.Read(player + 0xa4,team);
        stats.pvp = team != 0;
        uint16_t level = 0;
        int32_t generalResist = 0, magicResist = 0, immunity = 0;
        uint8_t unitFlags = 0;
        const uintptr_t unitDefinition = r.Pointer(unit + 0x5c);
        if (contextValid && unitDefinition && r.Read(unit + 0x314,level) && level &&
            r.Read(unit + 0x144,generalResist) && r.Read(unit + 0x2ac,magicResist) &&
            r.Read(unit + 0x140,immunity) && r.Read(unitDefinition + 0x13c,unitFlags)) {
            const uintptr_t offsets[] = {0x25c,0x26c,0x27c,0x28c,0x29c};
            for (unsigned i = 0; i < stats.resistances.size(); ++i) {
                int32_t elemental = 0;
                auto& resistance = stats.resistances[i];
                resistance.valid = r.Read(unit + offsets[i],elemental) &&
                    SheetDamageResist(elemental,generalResist,magicResist,level,stats.pvp,false,(unitFlags & 2) != 0,immunity > 0,resistance.weapon) &&
                    SheetDamageResist(elemental,generalResist,magicResist,level,stats.pvp,true,(unitFlags & 2) != 0,immunity > 0,resistance.magic);
            }
        }
        int32_t stunResist = 0;
        if (contextValid && r.Read(unit + 0x110,stunResist))
            stats.stunResistValid = SheetStunResist(stunResist,stats.pvp,stats.stunResist);
        int32_t magicBase = 0, magicBonus = 0;
        if (contextValid && r.Read(unit + 0x10c,magicBase) && r.Read(unit + 0x170,magicBonus))
            stats.magicCriticalValid = SheetMagicCritical(magicBase,magicBonus,stats.pvp,stats.magicCritical);
        if (stats.magicCriticalValid) SampleMagicSkills(r,image,unit,magicBase,magicBonus);
        const uintptr_t manipulators = Child(r,unit,image + 0x4710e8);
        const uintptr_t weapon = manipulators ? Child(r,manipulators,0,10) : 0;
        const uintptr_t type = r.Pointer(weapon);
        if (type == image + 0x493490 || type == image + 0x493748) {
            const uintptr_t definition = r.Pointer(weapon + 0x5c);
            uint8_t weaponType = 0;
            int32_t base = 0, melee = 0, ranged = 0, oneHand = 0, twoHand = 0;
            if (contextValid && definition && r.Read(definition + 0xd5,weaponType) && weaponType <= 13 &&
                r.Read(unit + 0x104,base) && r.Read(unit + 0x188,melee) && r.Read(unit + 0x1ac,ranged) &&
                r.Read(unit + 0x1d0,oneHand) && r.Read(unit + 0x1e8,twoHand))
                stats.criticalValid = SheetCritical(base,melee,ranged,oneHand,twoHand,weaponType,stats.critical);
        }
        const uintptr_t behavior = r.Pointer(unit + 0x308), definition = r.Pointer(unit + 0x5c);
        const uintptr_t mover = behavior ? behavior + 0x84 : 0;
        int32_t speed = 0;
        uint8_t baseSpeed = 0;
        if (mover && r.Pointer(mover) == image + 0x4790e8 && r.Pointer(mover + 0xc) == unit && definition &&
            r.Read(definition + 0xba,baseSpeed) && baseSpeed && r.Read(mover + 0x40,speed) && speed >= 0) {
            const double percent = speed * 100.0 / (baseSpeed * 256.0);
            if (percent <= 10000) { stats.movement = percent; stats.movementValid = true; }
        }
    }

public:
    void Prepare(const NativeReader& r,uintptr_t image,bool enabled,bool world,uint64_t now,Setter setter) {
        drawVisual = 0;
        const uintptr_t ui = r.Pointer(image + 0x5314b0), root = ui ? r.Pointer(ui + 0x270) : 0;
        if (!root || r.Pointer(root) != image + 0x448cc0) { sheet = panel = sampledUnit = 0; count = 0; stats = {}; return; }
        RefreshViewportPosition(r,root);
        if (!enabled || !world) { Restore(r,root,setter); nextLayout = 0; return; }
        int64_t x = 0, y = 0; float opacity = 0;
        if (!Origin(r,root,ui,x,y,opacity)) { Restore(r,root,setter); nextLayout = 0; return; }
        if (Active(r,root)) { drawVisual = visual; return; }
        Restore(r,root,setter);
        if (now < nextLayout) return;
        nextLayout = now + 1000;
        if (!Discover(r,image,root)) { count = 0; return; }
        for (const auto& node : changes) if (!Matches(r,node,node.before)) { count = 0; return; }
        sheet = root;
        for (const auto& node : changes) if (!setter(node.address,node.after)) { Restore(r,root,setter); return; }
        drawVisual = visual;
    }

    uintptr_t VisualTarget() const { return drawVisual; }

    bool VisualSnapshot(const NativeReader& r,uintptr_t image,uintptr_t target,std::array<uint32_t,39>& visualCopy) const {
        return target && target == drawVisual && r.Pointer(target) == image + 0x4bbfe0 &&
            r.Pointer(target + 0x14) == visualParent && r.Pointer(visualParent + 0x18) == target &&
            r.Read(target,visualCopy) && (visualCopy[0x88 / 4] & 0xff) == 0 && visualCopy[0x80 / 4] != 0 &&
            visualCopy[0x8c / 4] == 0 && visualCopy[0x90 / 4] == 0 && visualCopy[0x94 / 4] == 0 && visualCopy[0x98 / 4] == 0;
    }

    template<class Draw> bool DrawVisual(const NativeReader& r,uintptr_t image,uintptr_t target,uintptr_t event,SheetQuad area,Draw draw) const {
        std::array<uint32_t,39> visualCopy{};
        std::array<SheetVisualPart,3> parts{};
        if (!SheetVisualParts(area,parts) || !VisualSnapshot(r,image,target,visualCopy)) return false;
        reinterpret_cast<uint8_t*>(visualCopy.data())[0x88] = 1;
        for (const auto& part : parts) {
            std::memcpy(visualCopy.data() + 0x8c / 4,&part.source,sizeof(part.source));
            draw(visualCopy.data(),reinterpret_cast<void*>(event),part.destination);
        }
        return true;
    }

    CharacterSheetFrame Frame(const NativeReader& r,uintptr_t image,uint64_t now) {
        CharacterSheetFrame output;
        const uintptr_t ui = r.Pointer(image + 0x5314b0);
        if (!ui || !Active(r,r.Pointer(ui + 0x270))) return output;
        const uintptr_t unit = r.Pointer(sheet + 0x17c), zone = r.Pointer(ui + 0x1b4), player = zone ? r.Pointer(zone + 0xf8) : 0;
        if (!unit || !r.PlayerId(unit) || r.Pointer(unit + 0x14) != player) return output;
        int32_t width = 0, height = 0;
        int64_t x = 0, y = 0; float opacity = 0;
        if (!r.Read(ui + 0xf8,width) || !r.Read(ui + 0xfc,height) || width < 320 || height < 200 || width > 16384 || height > 16384 ||
            !Origin(r,panel,ui,x,y,opacity)) return output;
        if (unit != sampledUnit || now >= nextSample) { Sample(r,image,unit); sampledUnit = unit; nextSample = now + 100; }
        output = stats; output.visible = true; output.opacity = opacity;
        for (unsigned i = 0; i < anchors.size(); ++i) {
            const auto& b = anchors[i].before;
            output.cells[i] = {float(x + b[0]) / width,float(y + b[1] + 22) / height,float(b[2]) / width,float(b[3]) / height};
            if (i == 1) { output.cells[i].x -= 40.0f / width; output.cells[i].w += 40.0f / width; }
            if (i == 2) output.cells[i].w = 110.0f / width;
        }
        output.section = {float(x - 9) / width,float(y + 104) / height,348.0f / width,float(27 + CharacterSheetExtraHeight) / height};
        for (unsigned i = 0; i < anchors.size(); ++i) {
            output.cells[i+4] = output.cells[i];
            output.cells[i+4].y += 22.0f / height;
        }
        for (unsigned i = 0; i < output.resistances.size(); ++i) {
            const auto& label = resistanceLabels[i].after;
            const auto& value = resistanceValues[i].after;
            const int32_t left = std::min(label[0],value[0]), top = std::min(label[1],value[1]);
            const int32_t right = std::max(label[0] + label[2],value[0] + value[2]);
            int32_t bottom = std::max(label[1] + label[3],value[1] + value[3]);
            if (i + 1 < output.resistances.size()) bottom = std::min(bottom,resistanceLabels[i+1].after[1]);
            output.resistances[i].bounds = {float(x + left) / width,float(y + top) / height,float(right - left) / width,float(bottom - top) / height};
        }
        if (!ValidCharacterSheet(output)) return {};
        return output;
    }
};
