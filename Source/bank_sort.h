#pragma once
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <set>
#include <string>
#include <tuple>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include "bank_catalog.generated.h"

struct BankPosition {
    int page = -1, x = 0, y = 0;
    bool operator==(const BankPosition& b) const { return page == b.page && x == b.x && y == b.y; }
};
inline bool BankPositionLess(const BankPosition& a,const BankPosition& b) {
    return std::tie(a.page,a.x,a.y) < std::tie(b.page,b.x,b.y);
}
struct BankPage {
    uint32_t id = 0;
    int width = 0, height = 0;
    bool operator==(const BankPage& b) const { return id == b.id && width == b.width && height == b.height; }
};
struct BankItem {
    uint32_t id = 0;
    unsigned quantity = 0, width = 0, height = 0, slot = 0, quality = 0;
    bool exchange = false;
    std::string icon, definition;
    BankPosition position;
    bool operator==(const BankItem& b) const {
        return id == b.id && quantity == b.quantity && width == b.width && height == b.height && slot == b.slot && quality == b.quality && exchange == b.exchange && icon == b.icon && definition == b.definition && position == b.position;
    }
};
struct BankLayout {
    std::vector<BankPage> pages;
    std::vector<BankItem> items;
    uint32_t active = 0;
    bool operator==(const BankLayout& b) const { return pages == b.pages && items == b.items && active == b.active; }
    int Find(uint32_t id) const {
        for (size_t i = 0; i < items.size(); ++i) if (items[i].id == id) return static_cast<int>(i);
        return -1;
    }
};
class BankGrid {
    std::vector<std::vector<int>> cells;
    std::vector<BankPage> pages;
public:
    bool Build(const BankLayout& layout) {
        pages = layout.pages; cells.clear();
        if (pages.empty() || pages.size() > 16 || layout.items.size() > 1024) return false;
        size_t total = 0;
        std::set<uint32_t> ids, pageIds;
        for (const auto& p : pages) {
            if (p.id > 255 || !pageIds.insert(p.id).second || p.width < 1 || p.height < 1 || p.width > 32 || p.height > 32) return false;
            total += p.width * p.height;
            if (total > 4096) return false;
            cells.emplace_back(p.width * p.height,-1);
        }
        unsigned held = 0;
        for (size_t i = 0; i < layout.items.size(); ++i) {
            const auto& item = layout.items[i];
            if (!item.id || !ids.insert(item.id).second || !item.quantity || item.quantity > 255 || !item.width || !item.height || item.width > 32 || item.height > 32 || item.icon.size() > 256 || item.definition.size() > 256) return false;
            if (item.position.page == -1) {
                if (layout.active != item.id || ++held > 1) return false;
                continue;
            }
            if (!Fits(item,item.position)) return false;
            Fill(item,item.position,static_cast<int>(i));
        }
        return held == (layout.active ? 1u : 0u);
    }
    bool Inside(const BankItem& item,BankPosition p) const {
        return p.page >= 0 && p.page < static_cast<int>(pages.size()) && p.x >= 0 && p.y >= 0 && p.x + static_cast<int>(item.width) <= pages[p.page].width && p.y + static_cast<int>(item.height) <= pages[p.page].height;
    }
    std::vector<int> Collisions(const BankItem& item,BankPosition p,int ignore = -1) const {
        std::vector<int> result;
        if (!Inside(item,p)) return {-2};
        for (unsigned y = 0; y < item.height; ++y) for (unsigned x = 0; x < item.width; ++x) {
            const int i = cells[p.page][(p.y + y) * pages[p.page].width + p.x + x];
            if (i != -1 && i != ignore && std::find(result.begin(),result.end(),i) == result.end()) result.push_back(i);
        }
        return result;
    }
    bool Fits(const BankItem& item,BankPosition p,int ignore = -1) const {
        if (!Inside(item,p)) return false;
        for (unsigned y = 0; y < item.height; ++y) for (unsigned x = 0; x < item.width; ++x) {
            const int i = cells[p.page][(p.y + y) * pages[p.page].width + p.x + x];
            if (i != -1 && i != ignore) return false;
        }
        return true;
    }
    void Fill(const BankItem& item,BankPosition p,int value) {
        for (unsigned y = 0; y < item.height; ++y) for (unsigned x = 0; x < item.width; ++x) cells[p.page][(p.y + y) * pages[p.page].width + p.x + x] = value;
    }
};
struct BankStep {
    uint32_t item = 0;
    bool take = false;
    BankPosition destination;
};
inline bool ApplyBankStep(BankLayout& layout,const BankStep& step) {
    BankGrid grid;
    if (!grid.Build(layout)) return false;
    const int i = layout.Find(step.item);
    if (i < 0) return false;
    auto& item = layout.items[i];
    if (step.take) {
        if (layout.active || item.position.page < 0) return false;
        item.position = {}; layout.active = item.id;
        return true;
    }
    if (layout.active != item.id || item.position.page != -1 || !grid.Inside(item,step.destination)) return false;
    const auto collisions = grid.Collisions(item,step.destination);
    if (collisions.size() > 1) return false;
    if (!collisions.empty()) {
        auto& other = layout.items[collisions.front()];
        if (!item.exchange || !other.exchange || item.width != other.width || item.height != other.height || !(other.position == step.destination)) return false;
        other.position = {}; layout.active = other.id;
    } else layout.active = 0;
    item.position = step.destination;
    return true;
}
inline unsigned BankCategory(unsigned slot) {
    if (slot == 3 || slot == 4) return 1;
    if (slot == 1) return 2;
    if (slot == 11) return 3;
    if (slot == 10) return 4;
    if (slot == 2 || slot == 5 || slot == 6 || slot == 7 || slot == 8) return 5;
    return 6;
}
inline std::string BankLower(std::string key) {
    for (auto& c : key) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '/' || c == '\\') c = '.';
    }
    return key;
}
inline const BankCatalogEntry* BankMetadata(const BankItem& item) {
    const auto row = FindBankDefinition(item.definition.c_str());
    return row && row->slot == item.slot && row->width == item.width && row->height == item.height && row->icon == BankLower(item.icon) ? row : nullptr;
}
inline unsigned BankCategory(const BankItem& item) {
    if (const auto row = BankMetadata(item)) return row->category;
    const auto icon = BankLower(item.icon);
    if (BankCategory(item.slot) == 6 && (icon.find("scroll") != std::string::npos || icon.find("potion") != std::string::npos || icon.find("questitem") != std::string::npos)) return 0;
    return BankCategory(item.slot);
}
inline unsigned BankRole(const BankItem& item) {
    if (const auto row = BankMetadata(item)) return row->role;
    const auto icon = BankLower(item.icon);
    for (const auto word : {"crystal","plate","scale"}) if (icon.find(word) != std::string::npos) return 0;
    for (const auto word : {"leather","chain","splint"}) if (icon.find(word) != std::string::npos) return 1;
    for (const auto word : {"cloth","ghost","padded"}) if (icon.find(word) != std::string::npos) return 2;
    return 3;
}
inline std::string BankKind(const BankItem& item) {
    if (const auto row = BankMetadata(item)) return row->kind;
    const auto icon = BankLower(item.icon);
    if (BankCategory(item) == 0) {
        if (icon.find("questitem") != std::string::npos) return "2-quest";
        if (icon.find("potion") != std::string::npos) return "1-potion";
        return "0-scroll";
    }
    return item.slot == 11 ? "shield" : "";
}
inline std::string BankVisualFamily(const BankItem& item) {
    if (const auto row = BankMetadata(item)) if (*row->family) return row->family;
    std::string key = BankLower(item.icon);
    if (BankCategory(item) == 5) {
        std::string family;
        for (size_t start = 0; start < key.size();) {
            const auto end = key.find_first_of("_./\\",start);
            const auto token = key.substr(start,end == std::string::npos ? end : end-start);
            if (!token.empty() && token != "icon" && token != "body" && token != "boot" && token != "boots" && token != "glove" && token != "gloves" &&
                token != "helm" && token != "helmet" && token != "hat" && token != "shoulder" && token != "shoulders") {
                if (!family.empty()) family += '_';
                family += token;
            }
            if (end == std::string::npos) break;
            start = end+1;
        }
        if (!family.empty()) key = std::move(family);
    }
    return key;
}
struct BankPlan {
    std::vector<BankStep> steps;
    std::string error;
    BankLayout result;
};
inline unsigned BankEquipmentOrder(unsigned slot) {
    switch (slot) {
        case 5: return 0;
        case 6: return 1;
        case 8: return 2;
        case 2: return 3;
        case 7: return 4;
        default: return slot + 5;
    }
}
struct BankBlock {
    struct Member { int item, x, y; };
    std::vector<Member> members;
    unsigned width = 0, height = 0, category = 0, role = 0;
};
inline std::vector<BankBlock> BankBlocks(const BankLayout& source,const std::vector<int>& order,bool grouped) {
    std::vector<BankBlock> blocks;
    for (size_t n = 0; n < order.size();) {
        const auto& first = source.items[order[n]];
        if (!grouped || BankCategory(first) != 5) {
            blocks.push_back({{{order[n++],0,0}},first.width,first.height,BankCategory(first),0});
            continue;
        }
        std::array<std::vector<int>,5> slots;
        const auto family = BankVisualFamily(first);
        const auto role = BankRole(first);
        do {
            const int i = order[n++];
            slots[BankEquipmentOrder(source.items[i].slot)].push_back(i);
        } while (n < order.size() && BankCategory(source.items[order[n]]) == 5 && BankRole(source.items[order[n]]) == role && BankVisualFamily(source.items[order[n]]) == family);
        size_t copies = 0;
        for (const auto& slot : slots) copies = std::max(copies,slot.size());
        for (size_t copy = 0; copy < copies; ++copy) {
            BankBlock block; block.category = 5; block.role = role;
            unsigned left = 0, right = 0, leftHeight = 0, rightHeight = 0;
            for (unsigned slot = 0; slot < slots.size(); ++slot) if (copy < slots[slot].size()) {
                const auto& item = source.items[slots[slot][copy]];
                if (slot < 2) left = std::max(left,item.width);
                else right = std::max(right,item.width);
            }
            for (unsigned slot = 0; slot < slots.size(); ++slot) if (copy < slots[slot].size()) {
                const int i = slots[slot][copy];
                auto& height = slot < 2 ? leftHeight : rightHeight;
                block.members.push_back({i,slot < 2 ? 0 : static_cast<int>(left),static_cast<int>(height)});
                height += source.items[i].height;
            }
            block.width = left + right; block.height = std::max(leftHeight,rightHeight);
            blocks.push_back(std::move(block));
        }
    }
    return blocks;
}
inline bool BankSortTarget(const BankLayout& source,const std::vector<int>& order,int currentPage,bool allPages,int mode,BankLayout& target) {
    BankGrid grid;
    if (!grid.Build(source)) return false;
    if (mode == 5) {
        target = source;
        std::set<std::pair<unsigned,unsigned>> shapes;
        for (int i : order) shapes.insert({source.items[i].width,source.items[i].height});
        for (const auto& shape : shapes) {
            std::vector<BankPosition> positions;
            for (int i : order) if (source.items[i].width == shape.first && source.items[i].height == shape.second) positions.push_back(source.items[i].position);
            std::sort(positions.begin(),positions.end(),BankPositionLess);
            size_t n = 0;
            for (int i : order) if (source.items[i].width == shape.first && source.items[i].height == shape.second) target.items[i].position = positions[n++];
        }
        return true;
    }
    for (int i : order) grid.Fill(source.items[i],source.items[i].position,-1);
    target = source;
    auto blocks = BankBlocks(source,order,mode != 0 && mode != 4);
    if (mode >= 3) std::stable_sort(blocks.begin(),blocks.end(),[](const auto& a,const auto& b) {
        return std::make_tuple(a.category,a.role,32-a.height,32-a.width) < std::make_tuple(b.category,b.role,32-b.height,32-b.width);
    });
    if (mode == 7) std::stable_sort(blocks.begin(),blocks.end(),[](const auto& a,const auto& b) {
        return std::make_tuple(a.category,a.category == 5 && a.members.size() < 5,a.role) < std::make_tuple(b.category,b.category == 5 && b.members.size() < 5,b.role);
    });
    int page = allPages ? 0 : currentPage, left = 0, right = 0;
    int frontPage = -1, armorPage = -1;
    unsigned category = 99, role = 99;
    size_t budget = 500000;
    for (const auto& originalBlock : blocks) {
        const auto& block = originalBlock;
        if (mode == 2 && (category != block.category || (block.category == 5 && role != block.role))) left = right;
        category = block.category; role = block.role;
        bool placed = false;
        const int start = mode == 2 ? page : (allPages ? 0 : currentPage);
        std::vector<int> candidates;
        if (allPages && mode >= 6 && block.category == 5) {
            if (armorPage < 0) armorPage = frontPage+1;
            for (int p = armorPage; p < static_cast<int>(source.pages.size()); ++p) candidates.push_back(p);
            if (mode == 7) for (int p = 0; p < armorPage; ++p) candidates.push_back(p);
        } else for (int p = start; p < static_cast<int>(source.pages.size()); ++p) candidates.push_back(p);
        std::vector<BankBlock> variants{block};
        if (mode >= 6 && block.category == 5 && block.members.size() > 1) {
            auto vertical = block; vertical.width = vertical.height = 0;
            for (auto& member : vertical.members) {
                member.x = 0; member.y = int(vertical.height);
                vertical.width = std::max(vertical.width,source.items[member.item].width);
                vertical.height += source.items[member.item].height;
            }
            auto wide = block;
            if (wide.members.size() == 5 && std::all_of(wide.members.begin(),wide.members.end(),[&](const auto& m) { const auto& i=source.items[m.item]; return i.width==2 && i.height==(i.slot==6?4u:2u); })) {
                unsigned n = 0;
                for (auto& member : wide.members) {
                    if (source.items[member.item].slot == 6) { member.x = member.y = 0; }
                    else { member.x = 2+int(n%2)*2; member.y = int(n/2)*2; ++n; }
                }
                wide.width = 6; wide.height = 4; variants.push_back(std::move(wide));
            }
            variants.push_back(std::move(vertical));
        }
        for (const auto& shape : variants) {
          if (placed) break;
          for (int p : candidates) {
            if (placed) break;
            if (!allPages && p != currentPage) break;
            const auto& size = source.pages[p];
            const int begin = mode == 2 && p == page ? left : 0;
            for (int x = begin; !placed && x + static_cast<int>(shape.width) <= size.width; ++x) {
                for (int y = 0; !placed && y + static_cast<int>(shape.height) <= size.height; ++y) {
                    bool fits = true;
                    for (const auto& member : shape.members) {
                        if (!budget) return false;
                        --budget;
                        if (!grid.Fits(source.items[member.item],{p,x+member.x,y+member.y})) { fits = false; break; }
                    }
                    if (!fits) continue;
                    for (const auto& member : shape.members) {
                        const BankPosition position{p,x+member.x,y+member.y};
                        target.items[member.item].position = position;
                        grid.Fill(source.items[member.item],position,member.item);
                    }
                    if (p != page) { page = p; left = right = 0; }
                    right = std::max(right,x+static_cast<int>(shape.width));
                    if (block.category < 5) frontPage = std::max(frontPage,p);
                    placed = true;
                }
            }
          }
        }
        if (!placed) return false;
    }
    return true;
}
inline bool BankLaneTarget(const BankLayout& source,const std::vector<int>& order,BankLayout& target,bool liftArmor) {
    BankLayout reserved=target;
    reserved.items.clear();
    std::vector<BankBlock> front;
    for (int i : order) {
        const auto& item=source.items[i];
        if (BankCategory(item)==5) reserved.items.push_back(target.items[i]);
        else {
            if (item.width>2 || item.height>32) return false;
            front.push_back({{{i,0,0}},item.width,item.height,BankCategory(item),0});
        }
    }
    for (int p=0;p<int(source.pages.size());++p) {
        if (source.pages[p].width%2) return false;
        int top=source.pages[p].height;
        for (const auto& item:reserved.items) if (item.position.page==p) top=std::min(top,item.position.y);
        if (liftArmor) for (auto& item:reserved.items) if (item.position.page==p) item.position.y-=top;
    }
    BankGrid grid;
    if (!grid.Build(reserved)) return false;
    for (const auto& item:reserved.items) target.items[source.Find(item.id)].position=item.position;
    struct Lane {int page,x,y,height;};
    std::vector<Lane> lanes;
    BankItem probe;probe.width=2;probe.height=1;
    for (int p=0;p<int(source.pages.size());++p) for (int x=0;x<source.pages[p].width;x+=2) {
        for (int y=0;y<source.pages[p].height;) {
            if (!grid.Fits(probe,{p,x,y})) {++y;continue;}
            const int first=y;
            while (y<source.pages[p].height && grid.Fits(probe,{p,x,y})) ++y;
            lanes.push_back({p,x,first,y-first});
        }
    }
    std::vector<BankBlock> blocks;
    std::vector<bool> grouped(front.size(),false);
    for (size_t n=0;n<front.size();++n) {
        if (grouped[n]) continue;
        auto block=front[n];
        if (block.width==1) {
            const auto height=block.height;
            const size_t limit=height==1?4:2;
            for (size_t next=n+1;next<front.size() && block.members.size()<limit;++next) {
                const bool weaponPair=block.category>=3 && block.category<=4 && front[next].category>=3 && front[next].category<=4;
                if (grouped[next] || (front[next].category!=block.category && !weaponPair) || front[next].width!=1 || front[next].height!=height) continue;
                block.members.push_back({front[next].members[0].item,int(block.members.size()%2),int(block.members.size()/2)*int(height)});
                grouped[next]=true;
            }
            block.width=2;block.height=height*unsigned((block.members.size()+1)/2);
        }
        blocks.push_back(std::move(block));
    }
    std::array<unsigned,33> remaining{},capacity{};
    for (const auto& block:blocks) ++remaining[block.height];
    for (const auto& lane:lanes) ++capacity[lane.height];
    std::unordered_set<std::string> impossible;
    size_t budget=250000;
    size_t effort=20000000;
    const auto fits=[&](auto&& self,std::array<unsigned,33>& sizes,std::array<unsigned,33>& space)->bool {
        if (!budget) return false;
        --budget;
        unsigned height=0,area=0,free=0,kinds=0;
        for (unsigned h=1;h<=32;++h) {if (sizes[h]) {height=h;++kinds;} area+=h*sizes[h];free+=h*space[h];}
        if (!height) return true;
        if (area>free) return false;
        unsigned minimum=1;
        while (!sizes[minimum]) ++minimum;
        unsigned usable=0;
        for (unsigned h=minimum;h<=32;++h) usable+=h*space[h];
        if (area>usable) return false;
        if (kinds==1) {
            unsigned count=0;for (unsigned h=height;h<=32;++h) count+=(h/height)*space[h];
            return count>=sizes[height];
        }
        if (minimum>=2 && height<=4 && sizes[3]<=128 && sizes[4]<=128) {
            const unsigned columns=sizes[4]+1;
            std::vector<int> best((sizes[3]+1)*columns,-1),next(best.size(),-1);
            best[0]=int(space[2]);
            for (unsigned h=3;h<=32;++h) for (unsigned n=0;n<space[h];++n) {
                std::fill(next.begin(),next.end(),-1);
                for (unsigned a=0;a<=sizes[3];++a) for (unsigned b=0;b<=sizes[4];++b) {
                    const int count=best[a*columns+b];
                    if (count<0) continue;
                    for (unsigned three=0;three<=std::min(sizes[3]-a,h/3);++three)
                        for (unsigned four=0;four<=std::min(sizes[4]-b,(h-three*3)/4);++four) {
                            if (!effort) return false;
                            --effort;
                            auto& result=next[(a+three)*columns+b+four];
                            result=std::max(result,std::min(int(sizes[2]),count+int((h-three*3-four*4)/2)));
                        }
                }
                best.swap(next);
            }
            return best.back()>=int(sizes[2]);
        }
        std::string key;
        key.reserve(128);
        for (unsigned h=1;h<=32;++h) {key.push_back(char(sizes[h]&255));key.push_back(char(sizes[h]>>8));key.push_back(char(space[h]&255));key.push_back(char(space[h]>>8));}
        if (impossible.count(key)) return false;
        --sizes[height];
        for (unsigned h=height;h<=32;++h) if (space[h]) {
            --space[h];if (h>height) ++space[h-height];
            const bool result=self(self,sizes,space);
            ++space[h];if (h>height) --space[h-height];
            if (result) {++sizes[height];return true;}
        }
        ++sizes[height];impossible.insert(std::move(key));return false;
    };
    if (!fits(fits,remaining,capacity)) return false;
    unsigned spare=0;
    for (unsigned h=1;h<=32;++h) spare+=h*capacity[h];
    for (unsigned h=1;h<=32;++h) spare-=h*remaining[h];
    for (auto lane=lanes.rbegin();lane!=lanes.rend() && spare;++lane) {
        for (int remove=std::min(int(spare),lane->height);remove>0;--remove) {
            const int left=lane->height-remove;
            --capacity[lane->height];if (left) ++capacity[left];
            if (fits(fits,remaining,capacity)) {lane->height=left;spare-=unsigned(remove);break;}
            ++capacity[lane->height];if (left) --capacity[left];
        }
    }
    for (const auto& block:blocks) {
        --remaining[block.height];
        std::vector<size_t> candidates;
        for (size_t n=0;n<lanes.size();++n) if (lanes[n].height>=int(block.height)) candidates.push_back(n);
        std::sort(candidates.begin(),candidates.end(),[&](size_t a,size_t b) {const auto& x=lanes[a];const auto& y=lanes[b];return std::tie(x.page,x.x,x.y)<std::tie(y.page,y.x,y.y);});
        bool placed=false;
        for (size_t n:candidates) {
            auto& lane=lanes[n];
            --capacity[lane.height];const int left=lane.height-int(block.height);if (left) ++capacity[left];
            const bool possible=fits(fits,remaining,capacity);
            if (!possible) {++capacity[lane.height];if (left) --capacity[left];continue;}
            for (const auto& member:block.members) target.items[member.item].position={lane.page,lane.x+member.x,lane.y+member.y};
            lane.height=left;lane.y+=int(block.height);placed=true;break;
        }
        if (!placed) return false;
    }
    std::set<std::tuple<unsigned,std::string,unsigned,unsigned>> groups;
    for (int i:order) if (BankCategory(source.items[i])!=5) groups.insert({BankCategory(source.items[i]),BankKind(source.items[i]),source.items[i].width,source.items[i].height});
    for (const auto& group:groups) {
        std::vector<int> members;std::vector<BankPosition> positions;
        for (int i:order) if (std::make_tuple(BankCategory(source.items[i]),BankKind(source.items[i]),source.items[i].width,source.items[i].height)==group) {members.push_back(i);positions.push_back(target.items[i].position);}
        std::sort(positions.begin(),positions.end(),BankPositionLess);
        for (size_t n=0;n<members.size();++n) target.items[members[n]].position=positions[n];
    }
    return grid.Build(target);
}
inline bool BankReservedTarget(const BankLayout& source,const std::vector<int>& order,BankLayout& target,bool lanes=false,bool liftArmor=true) {
    BankGrid grid;
    if (!grid.Build(source)) return false;
    for (int i : order) grid.Fill(source.items[i],source.items[i].position,-1);
    target = source;
    auto blocks = BankBlocks(source,order,true);
    std::vector<BankBlock> armor,front;
    for (const auto& block : blocks) (block.category == 5 ? armor : front).push_back(block);
    std::reverse(armor.begin(),armor.end());
    std::stable_sort(armor.begin(),armor.end(),[](const auto& a,const auto& b) { return (a.members.size()<5) < (b.members.size()<5); });
    std::stable_sort(front.begin(),front.end(),[](const auto& a,const auto& b) { return std::make_tuple(6-a.category,32-a.height,32-a.width) < std::make_tuple(6-b.category,32-b.height,32-b.width); });
    std::vector<BankBlock> compact;
    for (size_t n=0;n<front.size();) {
        auto block=front[n++];
        if (block.width==1 && block.members.size()==1) {
            const auto height=block.height;
            const size_t limit=height==1?4:2;
            while (n<front.size() && block.members.size()<limit && front[n].category==block.category && front[n].width==1 && front[n].height==height && front[n].members.size()==1) {
                block.members.push_back({front[n++].members[0].item,int(block.members.size()%2),int(block.members.size()/2)*int(height)});
            }
            block.width=block.members.size()>1?2:1;
            block.height=height*unsigned((block.members.size()+1)/2);
        }
        compact.push_back(std::move(block));
    }
    front=std::move(compact);
    size_t budget = 500000;
    auto place = [&](const BankBlock& block,int p,int x,int y) {
        if (x<0 || y<0 || x+int(block.width)>source.pages[p].width || y+int(block.height)>source.pages[p].height) return false;
        for (const auto& member : block.members) {
            if (!budget) return false;
            --budget;
            if (!grid.Fits(source.items[member.item],{p,x+member.x,y+member.y})) return false;
        }
        for (const auto& member : block.members) {
            const BankPosition position{p,x+member.x,y+member.y};
            target.items[member.item].position = position;
            grid.Fill(source.items[member.item],position,member.item);
        }
        return true;
    };
    for (const auto& block : armor) {
        std::vector<BankBlock> variants{block};
        auto vertical = block; vertical.width = vertical.height = 0;
        for (auto& member : vertical.members) {
            member.x=0; member.y=int(vertical.height);
            vertical.width=std::max(vertical.width,source.items[member.item].width);
            vertical.height+=source.items[member.item].height;
        }
        auto wide = block;
        if (wide.members.size()==5 && std::all_of(wide.members.begin(),wide.members.end(),[&](const auto& m) {const auto& i=source.items[m.item];return i.width==2 && i.height==(i.slot==6?4u:2u);})) {
            unsigned n=0;
            for (auto& member : wide.members) {
                if (source.items[member.item].slot==6) member.x=member.y=0;
                else {member.x=2+int(n%2)*2;member.y=int(n/2)*2;++n;}
            }
            wide.width=6;wide.height=4;variants.push_back(std::move(wide));
        }
        variants.push_back(std::move(vertical));
        bool placed=false;
        for (int p=int(source.pages.size())-1;p>=0 && !placed;--p) for (const auto& shape : variants) {
            for (int x=source.pages[p].width-int(shape.width);x>=0 && !placed;--x)
                for (int y=source.pages[p].height-int(shape.height);y>=0 && !placed;--y) placed=place(shape,p,x,y);
            if (placed) break;
        }
        if (!placed) return false;
    }
    if (lanes) return BankLaneTarget(source,order,target,liftArmor);
    for (const auto& block : front) {
        bool placed=false;
        for (int p=int(source.pages.size())-1;p>=0 && !placed;--p)
            for (int x=source.pages[p].width-int(block.width);x>=0 && !placed;--x)
                for (int y=source.pages[p].height-int(block.height);y>=0 && !placed;--y) placed=place(block,p,x,y);
        if (!placed) return false;
    }
    for (int p = 0; p < int(source.pages.size()); ++p) {
        int top = source.pages[p].height;
        bool hasArmor = false;
        for (int i : order) if (target.items[i].position.page == p) {
            top = std::min(top,target.items[i].position.y);
            hasArmor = hasArmor || BankCategory(target.items[i]) == 5;
        }
        if (!hasArmor && top > 0) for (int i : order) if (target.items[i].position.page == p) target.items[i].position.y -= top;
    }
    return true;
}
inline bool BankSingleLaneTarget(const BankLayout& source,const std::vector<int>& order,int page,BankLayout& target) {
    BankLayout local;local.pages.push_back(source.pages[page]);
    std::vector<int> localOrder;
    for (int i:order) {
        localOrder.push_back(int(local.items.size()));local.items.push_back(source.items[i]);local.items.back().position.page=0;
    }
    BankLayout sorted;
    if (!BankReservedTarget(local,localOrder,sorted,true,false)) return false;
    target=source;
    for (size_t n=0;n<order.size();++n) {target.items[order[n]].position=sorted.items[n].position;target.items[order[n]].position.page=page;}
    return true;
}
inline BankPlan BankPlanMoves(const BankLayout& source,const BankLayout& target,const std::vector<int>& order) {
    BankPlan plan;
    BankGrid grid;
    size_t budget = 5000000;
    BankLayout work = source;
    BankPosition buffer;
    unsigned bufferWidth=0,bufferHeight=0;
    auto overlapsBuffer = [&](const BankItem& item,BankPosition p) {
        return buffer.page>=0 && p.page==buffer.page && p.x<buffer.x+int(bufferWidth) && p.x+int(item.width)>buffer.x &&
            p.y<buffer.y+int(bufferHeight) && p.y+int(item.height)>buffer.y;
    };
    auto heldForBuffer = [&](int i,BankPosition p) {
        return overlapsBuffer(work.items[i],p) && work.items[i].width*work.items[i].height<bufferWidth*bufferHeight;
    };
    auto add = [&](BankStep step) {
        if (plan.steps.size() >= 8192 || !ApplyBankStep(work,step)) return false;
        plan.steps.push_back(step); return true;
    };
    auto move = [&](int i,BankPosition p) {
        BankGrid free;
        const bool moved=free.Build(work) && free.Fits(work.items[i],p,i) && add({work.items[i].id,true,{}}) && add({work.items[i].id,false,p});
        if (moved && overlapsBuffer(work.items[i],p) && work.items[i].width*work.items[i].height>=bufferWidth*bufferHeight) buffer={};
        return moved;
    };
    std::set<std::tuple<int,int,int,unsigned,unsigned>> prepared;
    auto consolidate = [&]() {
        BankGrid finalGrid;
        if (!finalGrid.Build(target) || !grid.Build(work)) return false;
        std::set<std::pair<unsigned,unsigned>> shapes;
        for (int i:order) if (!(work.items[i].position==target.items[i].position)) shapes.insert({work.items[i].width,work.items[i].height});
        for (auto shape=shapes.rbegin();shape!=shapes.rend();++shape) {
            BankItem probe;probe.width=shape->first;probe.height=shape->second;
            std::vector<std::tuple<bool,unsigned,size_t,int,int,int>> regions;
            for (int p=0;p<int(work.pages.size());++p) for (int y=0;y<work.pages[p].height;++y) for (int x=0;x<work.pages[p].width;++x) {
                const BankPosition region{p,x,y};
                const auto key=std::make_tuple(p,x,y,probe.width,probe.height);
                if (!budget) return false;
                --budget;
                if (prepared.count(key) || !grid.Inside(probe,region)) continue;
                const auto blockers=grid.Collisions(probe,region);
                if (blockers.empty() || std::any_of(blockers.begin(),blockers.end(),[&](int i) {
                    return i<0 || work.items[i].width*work.items[i].height>=probe.width*probe.height || std::find(order.begin(),order.end(),i)==order.end();
                })) continue;
                unsigned area=0;
                for (int i:blockers) area+=work.items[i].width*work.items[i].height;
                regions.emplace_back(!finalGrid.Fits(probe,region),area,blockers.size(),p,y,x);
            }
            std::sort(regions.begin(),regions.end());
            for (const auto& candidate:regions) {
                const int p=std::get<3>(candidate),y=std::get<4>(candidate),x=std::get<5>(candidate);
                const auto key=std::make_tuple(p,x,y,probe.width,probe.height);
                const auto blockers=grid.Collisions(probe,{p,x,y});
                const auto checkpoint=work;const auto savedBuffer=buffer;const auto count=plan.steps.size();
                bool cleared=true;
                for (int i:blockers) {
                    bool placed=false;
                    for (int page=0;page<int(work.pages.size()) && !placed;++page)
                        for (int row=0;row<work.pages[page].height && !placed;++row)
                            for (int column=0;column<work.pages[page].width && !placed;++column) {
                                if (!budget) {cleared=false;break;}
                                --budget;
                                const auto& item=work.items[i];
                                if (page==p && column<x+int(probe.width) && column+int(item.width)>x && row<y+int(probe.height) && row+int(item.height)>y) continue;
                                const BankPosition destination{page,column,row};
                                if (grid.Fits(item,destination)) {placed=move(i,destination);if (placed) grid.Build(work);}
                            }
                    if (!placed) {cleared=false;break;}
                }
                if (cleared) {prepared.insert(key);buffer={p,x,y};bufferWidth=probe.width;bufferHeight=probe.height;return true;}
                work=checkpoint;buffer=savedBuffer;plan.steps.resize(count);grid.Build(work);
                if (!budget) return false;
            }
        }
        return false;
    };
    for (size_t pass = 0; pass < order.size() && !(work == target); ++pass) {
      bool progress = false;
      for (auto i : order) {
        const auto wanted = target.items[i].position;
        if (work.items[i].position == wanted) continue;
        if (heldForBuffer(i,wanted)) continue;
        if (!grid.Build(work)) { plan.error = "Bank layout is invalid."; break; }
        if (!grid.Fits(work.items[i],wanted,i)) continue;
        if (!move(i,wanted)) { plan.error = "Bank move could not be planned."; break; }
        progress = true;
      }
      if (!plan.error.empty() || work == target) break;
      if (progress) continue;
      for (auto i : order) {
        const auto wanted = target.items[i].position;
        if (work.items[i].position == wanted) continue;
        if (heldForBuffer(i,wanted)) continue;
        const auto checkpoint = work;
        const auto savedBuffer = buffer;
        const auto stepCount = plan.steps.size();
        if (!grid.Build(work)) { plan.error = "Bank layout is invalid."; break; }
        auto blockers = grid.Collisions(work.items[i],wanted,i);
        std::stable_sort(blockers.begin(),blockers.end(),[&](int a,int b) { return work.items[a].width*work.items[a].height > work.items[b].width*work.items[b].height; });
        for (int b : blockers) {
            if (b < 0) { plan.error = "Bank destination is invalid."; break; }
            bool placed = false;
            const auto final = target.items[b].position;
            if (!(final == work.items[b].position) && !heldForBuffer(b,final) && grid.Fits(work.items[b],final)) placed = move(b,final);
            if (placed) { grid.Build(work); continue; }
            BankPosition scratch;
            uint64_t best = UINT64_MAX;
            std::set<std::pair<unsigned,unsigned>> shapes;
            for (size_t k = 0; k < work.items.size(); ++k) if (!(work.items[k].position == target.items[k].position)) shapes.insert({work.items[k].width,work.items[k].height});
            for (size_t p = 0; p < work.pages.size(); ++p) {
                for (int y = 0; y < work.pages[p].height; ++y) for (int x = 0; x < work.pages[p].width; ++x) {
                    if (!budget) { plan.steps.clear(); plan.error = "Bank layout is too complex to sort."; return plan; }
                    --budget;
                    const BankPosition position{static_cast<int>(p),x,y};
                    const auto& item = work.items[b];
                    const bool overlaps = position.page == wanted.page && x < wanted.x + static_cast<int>(work.items[i].width) && x + static_cast<int>(item.width) > wanted.x && y < wanted.y + static_cast<int>(work.items[i].height) && y + static_cast<int>(item.height) > wanted.y;
                    if (overlaps || !grid.Fits(item,position)) continue;
                    uint64_t score = 0;
                    for (const auto& shape : shapes) {
                        BankItem probe; probe.width = shape.first; probe.height = shape.second;
                        const uint64_t area = probe.width*probe.height;
                        for (int py = std::max(0,y-int(probe.height)+1); py < y+int(item.height); ++py)
                            for (int px = std::max(0,x-int(probe.width)+1); px < x+int(item.width); ++px)
                                if (grid.Fits(probe,{int(p),px,py})) score += area*area*area;
                    }
                    if (score < best) { best = score; scratch = position; }
                }
                if (!plan.error.empty()) break;
            }
            if (scratch.page >= 0) { placed = move(b,scratch); if (placed) grid.Build(work); }
            if (!placed) { plan.error = "Free some bank space to rearrange larger items."; break; }
        }
        if (!plan.error.empty()) { work = checkpoint; buffer=savedBuffer; plan.steps.resize(stepCount); plan.error.clear(); continue; }
        if (!move(i,wanted)) { plan.error = "Bank move could not be planned."; break; }
        progress = true;
        break;
      }
      if (!plan.error.empty()) break;
      if (!progress && !consolidate()) { plan.error = "Free some bank space to rearrange larger items."; break; }
    }
    if (!plan.error.empty() || !(work == target)) {
        plan.steps.clear();
        if (plan.error.empty()) plan.error = "Bank layout could not be sorted.";
        return plan;
    }
    plan.result = std::move(work);
    return plan;
}
inline BankPlan PlanBankSort(const BankLayout& source,int currentPage,bool allPages) {
    BankPlan plan;
    BankGrid grid;
    if (source.active) { plan.error = "Put down the item on your cursor first."; return plan; }
    if (!grid.Build(source) || currentPage < 0 || currentPage >= static_cast<int>(source.pages.size())) { plan.error = "Bank contents are unavailable."; return plan; }
    std::vector<int> order;
    for (size_t i = 0; i < source.items.size(); ++i) if (allPages || source.items[i].position.page == currentPage) order.push_back(static_cast<int>(i));
    using Key = std::tuple<unsigned,unsigned,std::string,std::string,unsigned,std::string,unsigned,unsigned,unsigned,std::string,unsigned,bool>;
    std::vector<Key> keys(source.items.size());
    for (int i : order) {
        const auto& item = source.items[i];
        const auto category = BankCategory(item);
        keys[i] = std::make_tuple(category,category == 5 ? BankRole(item) : 0,BankKind(item),BankVisualFamily(item),BankEquipmentOrder(item.slot),BankLower(item.icon),255-item.quality,item.width,item.height,item.definition,item.quantity,item.exchange);
    }
    std::stable_sort(order.begin(),order.end(),[&](int a,int b) {
        if (keys[a]!=keys[b]) return keys[a]<keys[b];
        const auto& x=source.items[a];const auto& y=source.items[b];
        return std::tie(x.position.page,x.position.x,x.position.y,x.id)<std::tie(y.position.page,y.position.x,y.position.y,y.id);
    });
    BankLayout target = source;
    plan.error = "Not enough space for the sorted layout. Free some bank space.";
    const std::vector<int> modes=allPages ? std::vector<int>{6,7,9,8,2,1,3,0,4,5} : std::vector<int>{1,9,2,3,0,4,5};
    for (int mode : modes) {
        if (mode >= 6 && mode!=9 && !allPages) continue;
        const bool packed=mode==9 && !allPages ? BankSingleLaneTarget(source,order,currentPage,target) :
            mode==8 || mode==9 ? BankReservedTarget(source,order,target,mode==9) : BankSortTarget(source,order,currentPage,allPages,mode,target);
        if (!packed) continue;
        for (size_t begin=0;begin<order.size();) {
            size_t end=begin+1;
            while (end<order.size() && keys[order[begin]]==keys[order[end]]) ++end;
            std::vector<BankPosition> positions;
            std::vector<int> remaining;
            for (size_t n=begin;n<end;++n) positions.push_back(target.items[order[n]].position);
            std::sort(positions.begin(),positions.end(),BankPositionLess);
            for (size_t n=begin;n<end;++n) {
                const int i=order[n];const auto found=std::find(positions.begin(),positions.end(),source.items[i].position);
                if (found!=positions.end()) {target.items[i].position=*found;positions.erase(found);}
                else remaining.push_back(i);
            }
            for (size_t n=0;n<remaining.size();++n) target.items[remaining[n]].position=positions[n];
            begin=end;
        }
        auto movement = order;
        std::stable_sort(movement.begin(),movement.end(),[&](int a,int b) {
            return std::tie(source.items[a].height,source.items[a].width) > std::tie(source.items[b].height,source.items[b].width);
        });
        auto result = BankPlanMoves(source,target,movement);
        if (result.error.empty()) return result;
        std::stable_sort(movement.begin(),movement.end(),[&](int a,int b) {
            return std::make_pair(source.items[a].width*source.items[a].height,source.items[a].height) > std::make_pair(source.items[b].width*source.items[b].height,source.items[b].height);
        });
        result = BankPlanMoves(source,target,movement);
        return result.error.empty() ? result : BankPlanMoves(source,target,order);
    }
    return plan;
}

inline BankPlan PlanSelectedBankSort(const BankLayout& source,const std::vector<unsigned>& pageIds) {
    BankPlan failed;
    BankGrid grid;
    if (source.active || !grid.Build(source)) { failed.error="Put down the item on your cursor first, then try again."; return failed; }
    std::set<unsigned> selected(pageIds.begin(),pageIds.end());
    if (selected.empty() || selected.size()!=pageIds.size()) { failed.error="Select at least one available page."; return failed; }
    BankLayout local;
    std::vector<int> pages,items;
    for (size_t i=0;i<source.pages.size();++i) if (selected.erase(source.pages[i].id)) { pages.push_back(static_cast<int>(i)); local.pages.push_back(source.pages[i]); }
    if (!selected.empty()) { failed.error="A selected page is unavailable."; return failed; }
    for (size_t i=0;i<source.items.size();++i) {
        const auto found=std::find(pages.begin(),pages.end(),source.items[i].position.page);
        if (found==pages.end()) continue;
        items.push_back(static_cast<int>(i)); local.items.push_back(source.items[i]);
        local.items.back().position.page=static_cast<int>(found-pages.begin());
    }
    auto plan=PlanBankSort(local,0,local.pages.size()>1);
    if (!plan.error.empty()) return plan;
    for (auto& step:plan.steps) if (step.destination.page>=0) step.destination.page=pages[step.destination.page];
    auto result=source;
    for (size_t i=0;i<items.size();++i) {
        result.items[items[i]].position=plan.result.items[i].position;
        result.items[items[i]].position.page=pages[plan.result.items[i].position.page];
    }
    plan.result=std::move(result);
    return plan;
}

struct BankSortCommand {
    unsigned action=0;
    std::vector<unsigned> pages;
};

class BankSortSession {
    BankLayout current, expected;
    std::vector<BankStep> steps;
    size_t index = 0;
    uint64_t sentAt = 0, nextSend = 0;
    bool busy = false, pending = false, cancel = false, recovering = false, sendFailed = false;
    BankPosition returnPosition;
    std::set<int> recoveryPages;
    std::string message;
    bool Recover(const BankLayout& observed,uint64_t now) {
        if (recovering || !(observed == current) || !current.active) return false;
        const int item = current.Find(current.active);
        BankGrid grid;
        if (item < 0 || !grid.Build(current)) return false;
        auto destination = returnPosition;
        if (!grid.Fits(current.items[item],destination)) {
            destination = {};
            for (size_t p = 0; destination.page < 0 && p < current.pages.size(); ++p) {
                if (!recoveryPages.count(static_cast<int>(p))) continue;
                for (int y = 0; destination.page < 0 && y < current.pages[p].height; ++y)
                    for (int x = 0; destination.page < 0 && x < current.pages[p].width; ++x)
                        if (grid.Fits(current.items[item],{static_cast<int>(p),x,y})) destination = {static_cast<int>(p),x,y};
            }
        }
        if (destination.page < 0) return false;
        steps.resize(index+1); steps[index] = {current.active,false,destination};
        cancel = recovering = true; pending = sendFailed = false; nextSend = now;
        message = "Returning item before stopping...";
        return true;
    }
    bool MatchUpdate(const BankLayout& observed,bool& complete) {
        if (observed.pages != expected.pages || observed.items.size() > expected.items.size()) return false;
        BankGrid grid;
        if (!grid.Build(observed)) return false;
        std::vector<int> matched(expected.items.size(),-1);
        std::vector<bool> used(observed.items.size(),false);
        std::unordered_map<uint32_t,int> observedIndex,expectedIndex;
        observedIndex.reserve(observed.items.size()); expectedIndex.reserve(expected.items.size());
        for (size_t i=0;i<observed.items.size();++i) observedIndex.emplace(observed.items[i].id,static_cast<int>(i));
        for (size_t i=0;i<expected.items.size();++i) expectedIndex.emplace(expected.items[i].id,static_cast<int>(i));
        const auto observedItem=[&](uint32_t id) { const auto entry=observedIndex.find(id); return entry==observedIndex.end() ? -1 : entry->second; };
        complete = true;
        for (size_t i = 0; i < expected.items.size(); ++i) {
            const auto& before = current.items[i];
            const auto& after = expected.items[i];
            int found = -1;
            if (before == after) {
                found = observedItem(before.id);
                if (found < 0 || !(observed.items[found] == before)) return false;
            } else {
                for (size_t j = 0; j < observed.items.size(); ++j) {
                    auto item = observed.items[j];
                    if (used[j] || !(item.position == after.position)) continue;
                    item.id = after.id;
                    if (item == after) { found = static_cast<int>(j); break; }
                }
                if (found < 0) {
                    complete = false;
                    found = observedItem(before.id);
                    if (found >= 0 && !(observed.items[found] == before)) return false;
                }
            }
            if (found >= 0) {
                if (used[found]) return false;
                used[found] = true; matched[i] = found;
            }
        }
        if (std::find(used.begin(),used.end(),false) != used.end()) return false;
        if (!complete) return true;
        const auto active = expected.active ? matched[expected.Find(expected.active)] : -1;
        if (observed.active != (active >= 0 ? observed.items[active].id : 0)) return false;
        for (size_t s = index+1; s < steps.size(); ++s) {
            const auto i = expectedIndex.find(steps[s].item);
            if (i == expectedIndex.end()) return false;
            steps[s].item = observed.items[matched[i->second]].id;
        }
        expected = observed;
        return true;
    }
public:
    bool Busy() const { return busy; }
    size_t Completed() const { return index; }
    size_t Total() const { return steps.size(); }
    const std::string& Message() const { return message; }
    void Stop(const char* reason) { busy = pending = false; message = reason; }
    void Cancel() { cancel = true; }
    bool Start(const BankLayout& layout,int page,bool all,uint64_t now) {
        if (busy) return false;
        return StartPrepared(layout,PlanBankSort(layout,page,all),now);
    }
    bool StartPrepared(const BankLayout& layout,BankPlan plan,uint64_t now) {
        if (busy) return false;
        message = plan.error;
        if (!message.empty()) return false;
        steps = std::move(plan.steps); index = 0; current = layout;
        recoveryPages.clear();
        for (const auto& step:steps) {
            if (step.destination.page>=0) recoveryPages.insert(step.destination.page);
            const auto item=current.Find(step.item);
            if (item>=0 && current.items[item].position.page>=0) recoveryPages.insert(current.items[item].position.page);
        }
        busy = !steps.empty(); pending = cancel = recovering = sendFailed = false; returnPosition = {}; nextSend = now;
        message = busy ? "Sorting..." : "Already sorted.";
        return true;
    }
    const BankStep* Next(const BankLayout& observed,uint64_t now,bool canSend = true) {
        if (!busy) return nullptr;
        if (pending) {
            bool complete = observed == expected;
            if (!complete && !(observed == current) && !MatchUpdate(observed,complete)) { Stop("Stopped: bank contents changed. Check the item on your cursor."); return nullptr; }
            if (complete) { current = expected; pending = sendFailed = false; ++index; nextSend = now; }
            else {
                if (!sendFailed && now - sentAt < 5000) return nullptr;
                if (!Recover(observed,now)) { Stop("Stopped: move was not confirmed. Check the item on your cursor."); return nullptr; }
            }
        }
        if (!(observed == current)) { Stop("Stopped: bank contents changed."); return nullptr; }
        if ((cancel && !current.active) || index == steps.size()) { Stop(recovering ? "Sorting stopped. Item returned." : cancel ? "Sorting cancelled." : "Sorted."); return nullptr; }
        if (now < nextSend || !canSend) return nullptr;
        expected = current;
        if (!ApplyBankStep(expected,steps[index])) { Stop("Stopped: invalid bank move."); return nullptr; }
        if (steps[index].take) returnPosition = current.items[current.Find(steps[index].item)].position;
        return &steps[index];
    }
    void Sent(bool success,uint64_t now) {
        if (!success && !current.active) { Stop("Stopped: bank move was unavailable."); return; }
        pending = true; sendFailed = !success; sentAt = now;
    }
};

struct BankSortFrame {
    bool visible = false, busy = false, available = false;
    float x = 0, y = 0, width = 0, height = 0;
    float footerX = 0, footerY = 0, footerWidth = 0, footerHeight = 0;
    unsigned completed = 0, total = 0;
    std::vector<unsigned> pages;
    std::string message;
};
