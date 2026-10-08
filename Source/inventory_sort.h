#pragma once
#include "bank_sort.h"
#include <map>

inline unsigned InventoryGroup(const BankItem& item) {
    if (item.slot>=1 && item.slot<=11) return 4;
    const auto icon=BankLower(item.icon),definition=BankLower(item.definition);
    if (item.width==1 && item.height==1 && definition=="questitempal.token" && icon=="dungeon_token_standard_icon") return 0;
    if (icon.find("healthpotion")!=std::string::npos || definition.find("healthpotion")!=std::string::npos) return 1;
    if (icon.find("manapotion")!=std::string::npos || definition.find("manapotion")!=std::string::npos) return 2;
    return 3;
}
using InventoryKey=std::tuple<unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,std::string,std::string,unsigned,unsigned,bool>;
inline InventoryKey InventorySortKey(const BankItem& item) {
    const auto group=InventoryGroup(item);
    return std::make_tuple(group,group==4 ? BankCategory(item) : 0,group==4 ? BankEquipmentOrder(item.slot) : 0,
        group==4 ? BankRole(item) : 0,group==4 ? 255-item.quality : 0,255-item.quantity,
        BankLower(item.icon),BankLower(item.definition),item.width,item.height,item.exchange);
}
struct InventoryReservation { int x=0,y=0,width=0,height=0; };
class InventoryPacking {
    const BankLayout& source;
    const std::vector<int>& order;
    const std::vector<InventoryKey>& keys;
    size_t& budget;
    BankLayout target;
    std::array<uint32_t,32> occupied{};
    std::vector<int> large,ranks;
    std::unordered_set<std::string> failed;
    uint32_t Mask(unsigned width,int x) const { return (width==32 ? UINT32_MAX : (uint32_t(1)<<width)-1)<<x; }
    bool Fits(const BankItem& item,int x,int y) const {
        const auto bits=Mask(item.width,x);
        for (unsigned row=0;row<item.height;++row) if (occupied[y+row]&bits) return false;
        return true;
    }
    void Mark(const BankItem& item,int x,int y,bool fill) {
        const auto bits=Mask(item.width,x);
        for (unsigned row=0;row<item.height;++row) if (fill) occupied[y+row]|=bits; else occupied[y+row]&=~bits;
    }
    bool Place(size_t n) {
        if (n==large.size()) return true;
        const auto& item=source.items[large[n]];
        const int columns=source.pages[0].width-int(item.width)+1,rows=source.pages[0].height-int(item.height)+1;
        const bool gear=std::get<0>(keys[large[n]])==4;
        int start=0;
        if (n) {
            const auto& previous=source.items[large[n-1]];
            if (previous.width==item.width && previous.height==item.height && std::get<0>(keys[large[n-1]])==std::get<0>(keys[large[n]])) start=ranks[n-1]+1;
        }
        std::string state(reinterpret_cast<const char*>(occupied.data()),source.pages[0].height*sizeof(uint32_t));
        const auto depth=static_cast<uint32_t>(n);
        state.append(reinterpret_cast<const char*>(&depth),sizeof(depth));state.append(reinterpret_cast<const char*>(&start),sizeof(start));
        if (failed.count(state)) return false;
        for (int rank=start;rank<columns*rows;++rank) {
            if (!budget) return false;
            --budget;
            const int x=gear ? columns-1-rank/rows : rank/rows,y=rank%rows;
            if (!Fits(item,x,y)) continue;
            Mark(item,x,y,true);ranks[n]=rank;target.items[large[n]].position={0,x,y};
            if (Place(n+1)) return true;
            Mark(item,x,y,false);
        }
        if (failed.size()<4096) failed.insert(std::move(state));
        return false;
    }
    void Canonicalize() {
        std::map<std::tuple<unsigned,unsigned,unsigned>,std::vector<int>> shapes;
        for (int i:order) shapes[{std::get<0>(keys[i]),source.items[i].width,source.items[i].height}].push_back(i);
        for (const auto& shape:shapes) {
            const auto& members=shape.second;
            std::vector<BankPosition> positions;
            for (int i:members) positions.push_back(target.items[i].position);
            const bool gear=std::get<0>(shape.first)==4;
            std::sort(positions.begin(),positions.end(),[&](const auto& a,const auto& b) {
                return gear ? std::make_pair(-a.x,a.y)<std::make_pair(-b.x,b.y) : BankPositionLess(a,b);
            });
            for (size_t n=0;n<members.size();++n) target.items[members[n]].position=positions[n];
            for (size_t begin=0;begin<members.size();) {
                size_t end=begin+1;
                while (end<members.size() && keys[members[begin]]==keys[members[end]]) ++end;
                std::vector<BankPosition> available(positions.begin()+begin,positions.begin()+end);
                std::vector<int> remaining;
                for (size_t n=begin;n<end;++n) {
                    const int i=members[n];const auto at=std::find(available.begin(),available.end(),source.items[i].position);
                    if (at!=available.end()) { target.items[i].position=*at;available.erase(at); }
                    else remaining.push_back(i);
                }
                for (size_t n=0;n<remaining.size();++n) target.items[remaining[n]].position=available[n];
                begin=end;
            }
        }
    }
public:
    InventoryPacking(const BankLayout& layout,const std::vector<int>& sorted,const std::vector<InventoryKey>& sortKeys,size_t& remaining)
        :source(layout),order(sorted),keys(sortKeys),budget(remaining),target(layout) {
        for (int i:order) if (source.items[i].width*source.items[i].height>1) large.push_back(i);
        std::stable_sort(large.begin(),large.end(),[&](int a,int b) {
            const auto& x=source.items[a];const auto& y=source.items[b];
            const auto first=std::make_tuple(x.width*x.height,x.height,x.width),second=std::make_tuple(y.width*y.height,y.height,y.width);
            return first!=second ? first>second : keys[a]!=keys[b] ? keys[a]<keys[b] : BankPositionLess(x.position,y.position);
        });
        ranks.resize(large.size());
    }
    bool Build(InventoryReservation reserved,BankLayout& result) {
        if (reserved.width && reserved.height) {
            BankItem blocked;blocked.width=static_cast<unsigned>(reserved.width);blocked.height=static_cast<unsigned>(reserved.height);
            Mark(blocked,reserved.x,reserved.y,true);
        }
        if (!Place(0)) return false;
        for (bool gear:{true,false}) for (int i:order) {
            const auto& item=source.items[i];
            if (item.width*item.height!=1 || (std::get<0>(keys[i])==4)!=gear) continue;
            bool placed=false;
            const int width=source.pages[0].width,height=source.pages[0].height;
            for (int rank=0;rank<width*height && !placed;++rank) {
                const int x=gear ? width-1-rank/height : rank/height,y=rank%height;
                if (Fits(item,x,y)) { Mark(item,x,y,true);target.items[i].position={0,x,y};placed=true; }
            }
            if (!placed) return false;
        }
        Canonicalize();
        BankGrid grid;
        if (!grid.Build(target)) return false;
        result=std::move(target);return true;
    }
};
inline BankPlan PlanInventorySort(const BankLayout& source,int page) {
    BankPlan failed;
    BankGrid grid;
    if (source.active) { failed.error="Put down the item on your cursor first.";return failed; }
    if (!grid.Build(source) || page<0 || page>=static_cast<int>(source.pages.size())) { failed.error="Inventory contents are unavailable.";return failed; }
    BankLayout local;local.pages.push_back(source.pages[page]);
    std::vector<int> indices,order;
    std::vector<InventoryKey> keys;
    unsigned used=0;
    for (size_t i=0;i<source.items.size();++i) if (source.items[i].position.page==page) {
        indices.push_back(static_cast<int>(i));order.push_back(static_cast<int>(local.items.size()));
        local.items.push_back(source.items[i]);local.items.back().position.page=0;
        keys.push_back(InventorySortKey(source.items[i]));used+=source.items[i].width*source.items[i].height;
    }
    std::stable_sort(order.begin(),order.end(),[&](int a,int b) {
        return keys[a]!=keys[b] ? keys[a]<keys[b] : BankPositionLess(local.items[a].position,local.items[b].position);
    });
    const int width=local.pages[0].width,height=local.pages[0].height,free=width*height-static_cast<int>(used);
    std::vector<std::pair<int,int>> shapes;
    if (width>=2 && height>=4 && free>=8) shapes.push_back({2,4});
    if (width>=2 && height>=2 && free>=4) shapes.push_back({2,2});
    std::vector<std::pair<int,int>> fallback;
    for (int w=1;w<=width;++w) for (int h=1;h<=height;++h)
        if (w*h<=free && std::find(shapes.begin(),shapes.end(),std::make_pair(w,h))==shapes.end()) fallback.push_back({w,h});
    std::sort(fallback.begin(),fallback.end(),[](const auto& a,const auto& b) {
        return std::make_tuple(a.first*a.second,std::min(a.first,a.second),a.second,a.first)>
            std::make_tuple(b.first*b.second,std::min(b.first,b.second),b.second,b.first);
    });
    shapes.insert(shapes.end(),fallback.begin(),fallback.end());shapes.push_back({0,0});
    size_t budget=500000;
    for (const auto& shape:shapes) {
        std::vector<InventoryReservation> reservations;
        if (!shape.first) reservations.push_back({});
        else for (int y=height-shape.second;y>=0;--y) for (int x=width-shape.first;x>=0;--x) reservations.push_back({x,y,shape.first,shape.second});
        std::stable_sort(reservations.begin(),reservations.end(),[&](const auto& a,const auto& b) {
            return std::make_tuple(width-a.x-a.width+height-a.y-a.height,width-a.x-a.width,height-a.y-a.height)<
                std::make_tuple(width-b.x-b.width+height-b.y-b.height,width-b.x-b.width,height-b.y-b.height);
        });
        unsigned attempts=0;
        for (const auto& reserved:reservations) {
            BankLayout target;
            InventoryPacking packing(local,order,keys,budget);
            if (!packing.Build(reserved,target)) { if (!budget) break;continue; }
            auto movement=order;
            std::stable_sort(movement.begin(),movement.end(),[&](int a,int b) {
                const auto& x=local.items[a];const auto& y=local.items[b];
                return std::make_tuple(x.width*x.height,x.height,x.width)>std::make_tuple(y.width*y.height,y.height,y.width);
            });
            auto plan=BankPlanMoves(local,target,movement);
            if (!plan.error.empty()) plan=BankPlanMoves(local,target,order);
            if (!plan.error.empty()) { if (++attempts>=4) break;continue; }
            for (auto& step:plan.steps) if (step.destination.page>=0) step.destination.page=page;
            plan.result=source;
            for (size_t i=0;i<indices.size();++i) { plan.result.items[indices[i]].position=target.items[i].position;plan.result.items[indices[i]].position.page=page; }
            return plan;
        }
    }
    failed.error="Free some inventory space to rearrange larger items.";return failed;
}
