#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <istream>
#include <ostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

struct LoadoutSlot { unsigned id, type; const char* name; };
inline constexpr std::array<LoadoutSlot,10> LoadoutSlots{{
    {10,10,"Main hand"},{11,11,"Off hand"},{5,5,"Head"},{6,6,"Chest"},{8,8,"Shoulders"},
    {2,2,"Hands"},{7,7,"Feet"},{1,1,"Amulet"},{3,3,"Ring 1"},{4,3,"Ring 2"}
}};
inline int LoadoutSlotIndex(unsigned id) {
    for (size_t i=0;i<LoadoutSlots.size();++i) if (LoadoutSlots[i].id==id) return static_cast<int>(i);
    return -1;
}
struct LoadoutRef {
    std::string key, name;
    unsigned type=0;
    bool twoHanded=false;
    bool Fits(size_t slot) const {
        return slot<LoadoutSlots.size() && !key.empty() &&
            (type==LoadoutSlots[slot].type || (slot==1 && type==10 && !twoHanded));
    }
};
struct Loadout {
    std::string name;
    std::array<LoadoutRef,10> slots{};
    bool Valid() const {
        if (name.empty() || name.size()>80 || name.find_first_of("\r\n\t")!=std::string::npos) return false;
        bool any=false;
        for (size_t i=0;i<slots.size();++i) {
            const auto& ref=slots[i];
            if (ref.key.empty()) continue;
            if (!ref.Fits(i) || ref.key.size()>8192 || ref.name.empty() || ref.name.size()>256) return false;
            any=true;
        }
        return any && !(slots[0].twoHanded && !slots[1].key.empty());
    }
};
inline bool WriteLoadouts(std::ostream& out,const std::string& owner,const std::vector<Loadout>& sets) {
    if (owner.empty() || owner.size()>256 || sets.size()>12) return false;
    for (const auto& set:sets) if (!set.Valid()) return false;
    out<<"Loadouts 1 "<<std::quoted(owner)<<' '<<sets.size()<<'\n';
    for (const auto& set:sets) {
        out<<std::quoted(set.name)<<'\n';
        for (const auto& ref:set.slots) out<<ref.type<<' '<<ref.twoHanded<<' '<<std::quoted(ref.name)<<' '<<std::quoted(ref.key)<<'\n';
    }
    return out.good();
}
inline bool ReadLoadouts(std::istream& in,const std::string& owner,std::vector<Loadout>& sets) {
    std::string bytes;
    char block[4096];
    while (in.read(block,sizeof(block)) || in.gcount()) {
        bytes.append(block,static_cast<size_t>(in.gcount()));
        if (bytes.size()>1024*1024) return false;
    }
    if (in.bad()) return false;
    std::istringstream input(bytes);
    std::string header, savedOwner;
    unsigned version=0,count=0;
    if (!(input>>header>>version>>std::quoted(savedOwner)>>count) || header!="Loadouts" || version!=1 || savedOwner!=owner || count>12) return false;
    std::vector<Loadout> loaded(count);
    for (auto& set:loaded) {
        if (!(input>>std::quoted(set.name))) return false;
        for (auto& ref:set.slots) {
            unsigned two=0;
            if (!(input>>ref.type>>two>>std::quoted(ref.name)>>std::quoted(ref.key)) || two>1) return false;
            ref.twoHanded=two!=0;
            if (ref.key.empty() && (ref.type || two || !ref.name.empty())) return false;
        }
        if (!set.Valid()) return false;
    }
    input>>std::ws;
    if (!input.eof()) return false;
    sets=std::move(loaded);
    return true;
}

struct LoadoutPosition {
    int inventory=-1, x=0, y=0, slot=-1;
    bool operator==(const LoadoutPosition& b) const { return inventory==b.inventory && x==b.x && y==b.y && slot==b.slot; }
};
struct LoadoutItem {
    LoadoutRef ref;
    LoadoutPosition at;
    int width=0,height=0;
    bool equippable=false;
};
struct LoadoutBag { int id=0,width=0,height=0; bool bank=false; };
struct LoadoutSnapshot {
    std::vector<LoadoutBag> bags;
    std::vector<LoadoutItem> items;
    LoadoutItem held;
    bool bankOpen=false;
    int Equipped(size_t slot) const {
        for (size_t i=0;i<items.size();++i) if (items[i].at.slot==static_cast<int>(slot)) return static_cast<int>(i);
        return -1;
    }
    bool Free(const LoadoutPosition& p,int width,int height) const {
        if (p.inventory<0 || p.inventory>=static_cast<int>(bags.size()) || width<1 || height<1) return false;
        const auto& bag=bags[p.inventory];
        if (p.x<0 || p.y<0 || p.x+width>bag.width || p.y+height>bag.height) return false;
        for (const auto& item:items) if (item.at.inventory==p.inventory && item.at.slot<0 &&
            p.x<item.at.x+item.width && item.at.x<p.x+width && p.y<item.at.y+item.height && item.at.y<p.y+height) return false;
        return true;
    }
    bool Space(int width,int height,LoadoutPosition& result) const {
        for (size_t b=0;b<bags.size();++b) if (!bags[b].bank) {
            for (int y=0;y<=bags[b].height-height;++y) for (int x=0;x<=bags[b].width-width;++x) {
                LoadoutPosition p{static_cast<int>(b),x,y,-1};
                if (Free(p,width,height)) { result=p; return true; }
            }
        }
        return false;
    }
    bool BankSpace(int width,int height,int preferred,LoadoutPosition& result) const {
        if (!bankOpen) return false;
        for (int pass=0;pass<2;++pass) for (size_t b=0;b<bags.size();++b) {
            if (!bags[b].bank || ((bags[b].id==preferred)!=(pass==0))) continue;
            for (int y=0;y<=bags[b].height-height;++y) for (int x=0;x<=bags[b].width-width;++x) {
                LoadoutPosition p{static_cast<int>(b),x,y,-1};
                if (Free(p,width,height)) { result=p; return true; }
            }
        }
        return false;
    }
    bool Valid() const {
        if (bags.empty() || bags.size()>16 || items.size()>2048) return false;
        std::set<int> ids,slots;
        for (const auto& bag:bags) if (bag.width<1 || bag.height<1 || bag.width>32 || bag.height>32 || !ids.insert(bag.id).second) return false;
        for (size_t i=0;i<items.size();++i) {
            const auto& item=items[i];
            if (item.width<1 || item.height<1 || item.width>32 || item.height>32 || item.ref.key.empty()) return false;
            if (item.at.slot>=0) {
                if (item.at.slot>=10 || item.at.inventory!=-1 || !slots.insert(item.at.slot).second) return false;
            } else {
                if (item.at.inventory<0 || item.at.inventory>=static_cast<int>(bags.size())) return false;
                const auto& bag=bags[item.at.inventory];
                if (item.at.x<0 || item.at.y<0 || item.at.x+item.width>bag.width || item.at.y+item.height>bag.height || (bag.bank && !bankOpen)) return false;
                for (size_t j=0;j<i;++j) {
                    const auto& other=items[j];
                    if (other.at.inventory==item.at.inventory && other.at.slot<0 && item.at.x<other.at.x+other.width && other.at.x<item.at.x+item.width &&
                        item.at.y<other.at.y+other.height && other.at.y<item.at.y+item.height) return false;
                }
            }
        }
        return true;
    }
};
enum class LoadoutOperation { None, Unequip, Take, Equip, Put, Store };
struct LoadoutStep {
    LoadoutOperation operation=LoadoutOperation::None;
    LoadoutItem item;
    int slot=-1;
    LoadoutPosition destination;
};
struct LoadoutReturn {
    LoadoutItem item;
    int bank=-1;
};
struct LoadoutPlan {
    std::vector<LoadoutStep> steps;
    std::vector<LoadoutReturn> bankReturns;
    std::string error;
};
inline LoadoutPlan PlanLoadout(LoadoutSnapshot state,const Loadout& goal,const std::vector<LoadoutReturn>& bankReturns={}) {
    LoadoutPlan plan;
    if (!goal.Valid() || !state.Valid()) { plan.error="Equipment data is unavailable."; return plan; }
    if (!state.held.ref.key.empty()) { plan.error="Put down the item on your cursor first."; return plan; }
    std::array<int,10> wanted; wanted.fill(-1);
    std::set<int> used;
    for (size_t s=0;s<goal.slots.size();++s) {
        const auto& ref=goal.slots[s];
        if (ref.key.empty()) continue;
        const int current=state.Equipped(s);
        if (current>=0 && state.items[current].ref.key==ref.key) { wanted[s]=current; used.insert(current); }
    }
    std::string missing;
    for (size_t s=0;s<goal.slots.size();++s) {
        const auto& ref=goal.slots[s];
        if (ref.key.empty() || wanted[s]>=0) continue;
        for (size_t i=0;i<state.items.size();++i) if (!used.count(static_cast<int>(i)) && state.items[i].ref.key==ref.key && state.items[i].ref.Fits(s)) {
            wanted[s]=static_cast<int>(i); used.insert(static_cast<int>(i)); break;
        }
        if (wanted[s]<0) { if (!missing.empty()) missing+="\n"; missing+=std::string(LoadoutSlots[s].name)+": "+ref.name; }
        else if (!state.items[wanted[s]].equippable) { plan.error="Cannot equip: "+ref.name; return plan; }
    }
    if (!missing.empty()) { plan.error="Missing items:\n"+missing+(state.bankOpen ? "" : "\nOpen the bank to use stored items."); return plan; }
    std::vector<int> returnBank(state.items.size(),-1);
    for (const auto& entry:bankReturns) {
        int found=-1;
        for (size_t i=0;i<state.items.size();++i) if (state.items[i].ref.key==entry.item.ref.key && state.items[i].at==entry.item.at) { found=static_cast<int>(i); break; }
        if (found<0 || used.count(found) || !state.bankOpen) { plan.error="Equipment changed. Try again."; return plan; }
        returnBank[found]=entry.bank;
    }
    const auto markReturn=[&](int old,int replacement) {
        if (old<0 || replacement<0 || used.count(old)) return;
        const int bag=state.items[replacement].at.inventory;
        if (bag>=0 && state.bags[bag].bank) returnBank[old]=state.bags[bag].id;
    };
    for (size_t s=0;s<wanted.size();++s) if (wanted[s]>=0 && state.items[wanted[s]].at.slot!=static_cast<int>(s)) {
        const int current=state.Equipped(s);
        markReturn(current,wanted[s]);
    }
    if (wanted[0]>=0 && state.items[wanted[0]].ref.twoHanded) {
        const int off=state.Equipped(1);
        markReturn(off,wanted[0]);
    }
    if (wanted[1]>=0) {
        const int main=state.Equipped(0);
        if (main>=0 && state.items[main].ref.twoHanded) markReturn(main,wanted[1]);
    }
    for (size_t i=0;i<returnBank.size();++i) if (returnBank[i]>=0) plan.bankReturns.push_back({state.items[i],returnBank[i]});
    std::vector<bool> returned(state.items.size(),false);
    const auto storeReturns=[&](LoadoutSnapshot& next,std::vector<LoadoutStep>& steps,std::vector<bool>& stored) {
        for (size_t i=0;i<returnBank.size();++i) if (returnBank[i]>=0 && !stored[i] && next.items[i].at.inventory>=0) {
            auto& item=next.items[i];
            LoadoutPosition place;
            if (!next.BankSpace(item.width,item.height,returnBank[i],place)) continue;
            steps.push_back({LoadoutOperation::Store,item,-1,place}); item.at=place; stored[i]=true;
        }
    };
    storeReturns(state,plan.steps,returned);
    for (size_t pass=0;pass<wanted.size();++pass) {
        bool pending=false,advanced=false;
        for (size_t s=0;s<wanted.size();++s) {
            if (wanted[s]<0 || state.items[wanted[s]].at.slot==static_cast<int>(s)) continue;
            pending=true;
            auto next=state;
            auto stored=returned;
            std::vector<LoadoutStep> steps;
            std::set<int> displaced;
            const int current=next.Equipped(s);
            if (current>=0) displaced.insert(current);
            if (next.items[wanted[s]].at.slot>=0) displaced.insert(wanted[s]);
            if (s==0 && next.items[wanted[s]].ref.twoHanded) { const int off=next.Equipped(1); if (off>=0) displaced.insert(off); }
            if (s==1) { const int main=next.Equipped(0); if (main>=0 && next.items[main].ref.twoHanded) displaced.insert(main); }
            bool fits=true;
            for (int index:displaced) {
                auto& item=next.items[index];
                LoadoutPosition place;
                if (!next.Space(item.width,item.height,place)) { fits=false; break; }
                steps.push_back({LoadoutOperation::Unequip,item,item.at.slot,place}); item.at=place;
                storeReturns(next,steps,stored);
            }
            if (!fits) continue;
            auto& item=next.items[wanted[s]];
            const auto source=item.at;
            item.at={};
            LoadoutPosition recovery;
            if (!next.Space(item.width,item.height,recovery)) continue;
            item.at=source;
            steps.push_back({LoadoutOperation::Take,item,static_cast<int>(s),recovery});
            item.at={-1,0,0,static_cast<int>(s)};
            storeReturns(next,steps,stored);
            state=std::move(next); returned=std::move(stored);
            plan.steps.insert(plan.steps.end(),steps.begin(),steps.end());
            advanced=true; break;
        }
        if (!pending) break;
        if (!advanced) { plan.steps.clear(); plan.error="Make room in Inventory to switch this equipment."; return plan; }
    }
    storeReturns(state,plan.steps,returned);
    for (size_t i=0;i<returnBank.size();++i) if (returnBank[i]>=0 && !returned[i]) {
        plan.steps.clear(); plan.error="Make room in the bank for the equipment being removed."; return plan;
    }
    return plan;
}

struct LoadoutCommand {
    enum class Kind { None, Capture, Equip, Cancel } kind=Kind::None;
    Loadout set;
    std::string owner;
};
struct LoadoutFrame {
    bool visible=false, busy=false, bankOpen=false;
    float x=0,y=0,width=0,height=0;
    float titleX=0,titleY=0,titleWidth=0,titleHeight=0;
    std::string owner, character, message;
    unsigned completed=0;
    uint64_t response=0;
    bool captured=false;
    Loadout equipment;
};
