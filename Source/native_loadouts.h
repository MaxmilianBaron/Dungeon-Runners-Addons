#pragma once
#include "native_reader.h"
#include "native_chat.h"
#include "loadouts.h"

class NativeLoadouts {
    struct Snapshot {
        uintptr_t unit=0, equipment=0, container=0, panel=0;
        std::vector<uintptr_t> objects, inventories;
        uintptr_t held=0;
        LoadoutSnapshot state;
    };
    LoadoutFrame frame;
    Loadout goal;
    LoadoutStep pending;
    std::vector<LoadoutReturn> bankReturns;
    std::vector<LoadoutPosition> previousCopies;
    std::vector<int> bagIds;
    int displacedReturn=-1,storingReturn=-1;
    uintptr_t owner=0;
    uint64_t nextSample=0, sentAt=0;
    bool cancelled=false, waiting=false, textFault=false, bankRequired=false;
    bool bankChanged=false, sortRequested=false;
    std::string reason;
    static std::string Definition(const NativeReader& r,uintptr_t node) {
        std::set<uintptr_t> seen;
        while (node && seen.size()<24 && seen.insert(node).second) {
            if (const auto type=r.Pointer(node+0x58)) return r.String(type+0x18,256);
            node=r.Pointer(node+0x54);
        }
        return {};
    }
    static bool Fingerprint(const NativeReader& r,uintptr_t image,uintptr_t item,std::string& key,unsigned depth=0) {
        if (depth>3) return false;
        const auto def=Definition(r,item);
        uint8_t level=0;
        if (def.empty() || !r.Read(item+0x7f,level)) return false;
        key=std::to_string(def.size())+":"+def+":"+std::to_string(level);
        std::set<uintptr_t> seen;
        for (auto child=r.Pointer(item+0x18);child;child=r.Pointer(child+0x20)) {
            if (seen.size()>=64 || !seen.insert(child).second) return false;
            if (r.KindOf(child,image+0x530b90)) {
                const auto modifier=Definition(r,child);
                uint8_t rank=0; uint32_t value=0;
                if (modifier.empty() || !r.Read(child+0x74,rank) || !r.Read(child+0x78,value)) return false;
                key+="|m"+std::to_string(modifier.size())+":"+modifier+":"+std::to_string(rank)+":"+std::to_string(value);
            } else if (r.KindOf(child,image+0x530b80)) {
                std::string nested;
                if (!Fingerprint(r,image,child,nested,depth+1)) return false;
                key+="|i"+std::to_string(nested.size())+":"+nested;
            }
            if (key.size()>8192) return false;
        }
        return true;
    }
    static bool Eligible(uintptr_t item,uintptr_t unit,uintptr_t entry) {
        __try { return reinterpret_cast<bool(__thiscall*)(void*,void*)>(entry)(reinterpret_cast<void*>(item),reinterpret_cast<void*>(unit)); }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool Access(uintptr_t image,uintptr_t container,uintptr_t inventory) {
        const uintptr_t function=image+0x18e400;
        bool result=false;
        __try {
            __asm {
                mov eax, inventory
                push 0
                push container
                call function
                mov result, al
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        return result;
    }
    static bool Send(uintptr_t image,uintptr_t container,uintptr_t equipment,uintptr_t object,LoadoutOperation operation,unsigned slot,int x,int y) {
        const uintptr_t take=image+0x18beb0,put=image+0x18bd80;
        __try {
            if (operation==LoadoutOperation::Take || operation==LoadoutOperation::Store) {
                __asm {
                    mov edi, container
                    mov eax, object
                    lock inc dword ptr [eax+4]
                    push eax
                    call take
                }
            } else if (operation==LoadoutOperation::Put) {
                __asm {
                    mov edi, container
                    mov eax, object
                    lock inc dword ptr [eax+4]
                    push y
                    push x
                    push eax
                    call put
                }
            } else if (operation==LoadoutOperation::Unequip) {
                reinterpret_cast<void(__thiscall*)(void*,unsigned)>(image+0x18f490)(reinterpret_cast<void*>(equipment),slot);
            } else if (operation==LoadoutOperation::Equip) {
                const uintptr_t validate=image+0x18f2a0;
                bool allowed=false;
                __asm {
                    mov eax, object
                    push slot
                    push equipment
                    call validate
                    mov allowed, al
                }
                if (!allowed) return false;
                reinterpret_cast<void(__thiscall*)(void*,unsigned,void*)>(image+0x18f3a0)(reinterpret_cast<void*>(equipment),slot,reinterpret_cast<void*>(object));
            } else return false;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }
    static bool Bounds(const NativeReader& r,uintptr_t control,uintptr_t ui,float& x,float& y,float& width,float& height) {
        int32_t w=0,h=0;
        if (!control || !r.Read(control+0xf8,w) || !r.Read(control+0xfc,h) || w<=0 || h<=0 || w>4096 || h>4096) return false;
        width=static_cast<float>(w); height=static_cast<float>(h); x=y=0;
        std::set<uintptr_t> seen;
        for (auto node=control;node;node=r.Pointer(node+0x14)) {
            uint32_t flags=0; int32_t nx=0,ny=0;
            if (seen.size()>=32 || !seen.insert(node).second || !r.Read(node+0xb4,flags) || !(flags&8)) return false;
            if (node==ui) return true;
            if (!r.Read(node+0xf0,nx) || !r.Read(node+0xf4,ny) || std::abs(int64_t(nx))>16384 || std::abs(int64_t(ny))>16384) return false;
            x+=nx; y+=ny;
        }
        return false;
    }
    static bool ReadItem(const NativeReader& r,uintptr_t image,uintptr_t unit,uintptr_t object,LoadoutPosition position,LoadoutItem& item) {
        const auto desc=r.Pointer(object+0x5c);
        uint8_t width=0,height=0,type=0,flags=0,category=0;
        if (!desc || !r.Read(desc+0xcb,width) || !r.Read(desc+0xcc,height) || !r.Read(desc+0xc8,type) || !r.Read(desc+0xcf,flags) ||
            !Fingerprint(r,image,object,item.ref.key)) return false;
        item.width=width; item.height=height; item.at=position; item.ref.type=type;
        if (type==10 && r.Read(desc+0xd5,category)) item.ref.twoHanded=category==3 || category==6 || category==8 || category==13;
        const auto validation=r.Pointer(r.Pointer(object)+0x110);
        item.equippable=(flags&4) && !(flags&8) && validation>=image+0x1000 && validation<image+0x430000 && Eligible(object,unit,validation);
        return width>0 && height>0;
    }
    static uintptr_t Title(const NativeReader& r,uintptr_t panel) {
        std::vector<uintptr_t> nodes{panel};
        for (size_t i=0;i<nodes.size();++i) {
            const auto node=nodes[i];
            if (r.String(node+0x10,64)=="Label") return node;
            for (auto child=r.Pointer(node+0x18);child;child=r.Pointer(child+0x20)) {
                if (nodes.size()>=128 || std::find(nodes.begin(),nodes.end(),child)!=nodes.end()) return 0;
                nodes.push_back(child);
            }
        }
        return 0;
    }
    bool Read(const NativeReader& r,uintptr_t image,Snapshot& result,bool detailed) {
        if (!r.InWorld()) return false;
        const auto ui=r.Pointer(image+0x5314b0), panel=r.Pointer(ui+0x230);
        const auto player=r.Pointer(r.Pointer(ui+0x1b4)+0xf8);
        result.unit=r.Pointer(player+0xb0); result.panel=panel;
        result.equipment=r.Pointer(panel+0x184); result.container=r.Pointer(panel+0x188);
        if (!result.unit || r.Pointer(panel+0x180)!=result.unit || !result.equipment || !result.container || r.Pointer(result.container+0x14)!=result.unit) return false;
        const auto character=r.String(player+0x10,128);
        if (character.empty()) return false;
        frame.owner=character; frame.character=character;
        frame.visible=!r.Pointer(ui+0x180) && Bounds(r,panel,ui,frame.x,frame.y,frame.width,frame.height);
        frame.titleX=frame.titleY=frame.titleWidth=frame.titleHeight=0;
        if (frame.visible && !Bounds(r,Title(r,panel),ui,frame.titleX,frame.titleY,frame.titleWidth,frame.titleHeight)) frame.titleWidth=0;
        float x=0,y=0,w=0,h=0;
        result.state.bankOpen=Bounds(r,r.Pointer(ui+0x23c),ui,x,y,w,h);
        frame.bankOpen=result.state.bankOpen;
        if (!detailed) return true;
        uint32_t unitFlags=0;
        if (!r.Read(result.unit+0xa0,unitFlags) || (unitFlags&(1u<<11))) return false;
        std::set<uintptr_t> seen;
        for (auto inventory=r.Pointer(result.container+0x18);inventory;inventory=r.Pointer(inventory+0x20)) {
            if (seen.size()>=64 || !seen.insert(inventory).second) return false;
            const auto desc=r.Pointer(inventory+0x5c);
            if (!r.KindOf(desc,image+0x530b7c)) continue;
            uint8_t type=0,id=0,width=0,height=0;
            if (!r.Read(desc+0xd7,type)) return false;
            if (type!=1 && !(type==2 && result.state.bankOpen)) continue;
            if (!Access(image,result.container,inventory)) continue;
            if (!r.Read(inventory+0x65,id) || !r.Read(desc+0xd5,width) || !r.Read(desc+0xd6,height) || result.inventories.size()>=16) return false;
            result.inventories.push_back(inventory); result.state.bags.push_back({id,width,height,type==2});
        }
        seen.clear();
        for (size_t b=0;b<result.inventories.size();++b) {
            for (auto object=r.Pointer(result.inventories[b]+0x18);object;object=r.Pointer(object+0x20)) {
                uint8_t xCell=0,yCell=0; LoadoutItem item;
                if (seen.size()>=2048 || !seen.insert(object).second || !r.Read(object+0x80,xCell) || !r.Read(object+0x81,yCell) ||
                    !ReadItem(r,image,result.unit,object,{static_cast<int>(b),xCell,yCell,-1},item)) return false;
                result.objects.push_back(object); result.state.items.push_back(std::move(item));
            }
        }
        std::set<uintptr_t> equipmentSeen;
        for (auto object=r.Pointer(result.equipment+0x18);object;object=r.Pointer(object+0x20)) {
            if (equipmentSeen.size()>=64 || !equipmentSeen.insert(object).second) return false;
            if (!r.KindOf(object,image+0x530b80)) continue;
            uint32_t flags=0,id=0;
            if (!r.Read(object+0x60,flags) || !r.Read(object+0x68,id)) return false;
            if (flags&1) continue;
            const int slot=LoadoutSlotIndex(id);
            if (slot<0) continue;
            LoadoutItem item;
            if (!ReadItem(r,image,result.unit,object,{-1,0,0,slot},item)) return false;
            result.objects.push_back(object); result.state.items.push_back(std::move(item));
        }
        result.held=r.Pointer(result.container+0x80);
        if (result.held && !ReadItem(r,image,result.unit,result.held,{},result.state.held)) return false;
        return result.state.Valid();
    }
    std::string Name(uintptr_t image,uintptr_t item) {
        char text[NativeLocalTextCapacity]{};
        if (CopyNativeItemLink(image+0x97a30,image+0x320925,item,text,textFault)) {
            const std::string link(text);
            const auto first=link.find(">["),last=link.rfind("]</font>");
            if (first!=std::string::npos && last!=std::string::npos && last>first+2 && last-first-2<=256) return link.substr(first+2,last-first-2);
        }
        return "Equipment";
    }
    void Capture(uintptr_t image,const Snapshot& sample) {
        frame.equipment={};
        for (size_t i=0;i<sample.state.items.size();++i) if (sample.state.items[i].at.slot>=0) {
            auto ref=sample.state.items[i].ref; ref.name=Name(image,sample.objects[i]);
            frame.equipment.slots[sample.state.items[i].at.slot]=std::move(ref);
        }
        frame.captured=true; frame.message="Current equipment captured.";
    }
    void Finish(const std::string& message) {
        frame.busy=false; waiting=false; pending={}; goal={}; frame.message=message; cancelled=false;
        bankReturns.clear(); previousCopies.clear(); bagIds.clear(); displacedReturn=storingReturn=-1;
    }
    void Request(uintptr_t image,const Snapshot& sample,LoadoutStep step,uintptr_t object,uint64_t now) {
        pending=std::move(step);
        if (pending.operation==LoadoutOperation::Unequip) {
            displacedReturn=-1; previousCopies.clear();
            for (size_t i=0;i<bankReturns.size();++i) if (bankReturns[i].item.ref.key==pending.item.ref.key && bankReturns[i].item.at==pending.item.at) displacedReturn=static_cast<int>(i);
            for (const auto& item:sample.state.items) if (item.ref.key==pending.item.ref.key && item.at.inventory>=0) previousCopies.push_back(item.at);
        } else if (pending.operation==LoadoutOperation::Store) {
            storingReturn=-1;
            for (size_t i=0;i<bankReturns.size();++i) if (bankReturns[i].item.ref.key==pending.item.ref.key && bankReturns[i].item.at==pending.item.at) storingReturn=static_cast<int>(i);
            if (storingReturn<0) { Finish("Equipment changed. Try again."); return; }
        }
        if (!Send(image,sample.container,sample.equipment,object,pending.operation,pending.slot>=0 ? LoadoutSlots[pending.slot].id : 0,pending.destination.x,pending.destination.y)) {
            cancelled=true; reason="The game could not equip this item."; waiting=false;
            if (sample.held) ReturnHeld(image,sample,now); else Finish(reason);
            return;
        }
        waiting=true; sentAt=now;
    }
    void ReturnHeld(uintptr_t image,const Snapshot& sample,uint64_t now) {
        if (sample.state.held.ref.key.empty()) { Finish(reason); return; }
        if (sample.state.held.ref.key!=pending.item.ref.key) { Finish("Stopped: the item on your cursor changed."); return; }
        LoadoutPosition place;
        if (!sample.state.Space(sample.state.held.width,sample.state.held.height,place)) { frame.message="Make room in Inventory to put down the item."; waiting=false; return; }
        pending={LoadoutOperation::Put,sample.state.held,-1,place};
        if (Send(image,sample.container,sample.equipment,sample.inventories[place.inventory],LoadoutOperation::Put,0,place.x,place.y)) { waiting=true; sentAt=now; }
        else { waiting=false; frame.message="Could not put down the item. Make room in Inventory."; }
    }
public:
    const LoadoutFrame& Frame() const { return frame; }
    bool TakeBankSortRequest() { const bool result=sortRequested; sortRequested=false; return result; }
    void Reset() {
        frame={}; goal={}; pending={}; owner=0; nextSample=sentAt=0; cancelled=waiting=textFault=bankRequired=false; reason.clear();
        bankReturns.clear(); previousCopies.clear(); bagIds.clear(); displacedReturn=storingReturn=-1;
        bankChanged=sortRequested=false;
    }
    void Service(const NativeReader& r,uintptr_t image,bool enabled,const LoadoutCommand& command,bool focused,bool bankBusy,uint64_t now) {
        const bool action=command.kind!=LoadoutCommand::Kind::None;
        if (command.kind==LoadoutCommand::Kind::Capture) { frame.captured=false; frame.equipment={}; ++frame.response; frame.message="Equipment could not be saved. Open Inventory and try again."; }
        if (frame.busy && (!enabled || !focused || command.kind==LoadoutCommand::Kind::Cancel || bankBusy)) { cancelled=true; reason="Equipment change cancelled."; }
        if (!enabled && !frame.busy) { frame.visible=false; return; }
        if (now<nextSample && !action) return;
        nextSample=now+(frame.busy ? 33 : 200);
        Snapshot sample;
        if (!Read(r,image,sample,frame.busy || action)) {
            frame.visible=false;
            if (frame.busy) {
                if (!r.InWorld() || (sample.unit && sample.unit!=owner)) Finish("Equipment change stopped: character changed.");
                else { cancelled=true; reason="Equipment is unavailable. Waiting to finish the pending move."; frame.message=reason; }
            } else if (action) frame.message="Open Inventory before using Loadouts.";
            return;
        }
        if (!frame.busy) {
            if (!action || !frame.visible || !focused || command.owner!=frame.owner) return;
            if (command.kind==LoadoutCommand::Kind::Capture) { Capture(image,sample); return; }
            if (command.kind!=LoadoutCommand::Kind::Equip) return;
            if (bankBusy) { frame.message="Wait for bank sorting to finish."; return; }
            auto plan=PlanLoadout(sample.state,command.set);
            if (!plan.error.empty()) { frame.message=plan.error; return; }
            if (plan.steps.empty()) { frame.message="Already equipped."; return; }
            bankReturns=std::move(plan.bankReturns); displacedReturn=storingReturn=-1; previousCopies.clear(); bagIds.clear();
            bankChanged=sortRequested=false;
            for (const auto& bag:sample.state.bags) bagIds.push_back(bag.id);
            bankRequired=!bankReturns.empty();
            for (const auto& step:plan.steps) if (step.operation==LoadoutOperation::Take && step.item.at.inventory>=0 && sample.state.bags[step.item.at.inventory].bank) bankRequired=true;
            goal=command.set; owner=sample.unit; frame.completed=0; frame.busy=true; cancelled=waiting=false; pending={}; reason.clear();
            nextSample=now+33;
        }
        if (sample.unit!=owner) { Finish("Equipment change stopped: character changed."); return; }
        if (!frame.visible || (bankRequired && !sample.state.bankOpen)) { cancelled=true; reason="Equipment change cancelled: window closed."; }
        if (!cancelled && bankRequired) {
            bool same=sample.state.bags.size()==bagIds.size();
            for (size_t i=0;same && i<bagIds.size();++i) same=sample.state.bags[i].id==bagIds[i];
            if (!same) { cancelled=true; reason="Equipment change cancelled: bank access changed."; }
        }
        if (waiting) {
            bool acknowledged=false;
            const auto& state=sample.state;
            const int equipped=pending.slot>=0 ? state.Equipped(pending.slot) : -1;
            if (pending.operation==LoadoutOperation::Take || pending.operation==LoadoutOperation::Store) acknowledged=state.held.ref.key==pending.item.ref.key;
            else if (pending.operation==LoadoutOperation::Equip) acknowledged=equipped>=0 && state.items[equipped].ref.key==pending.item.ref.key && !sample.held;
            else if (pending.operation==LoadoutOperation::Unequip) {
                if (equipped<0) {
                    acknowledged=state.held.ref.key==pending.item.ref.key;
                    int found=-1;
                    for (size_t i=0;i<state.items.size();++i) {
                        const auto& item=state.items[i];
                        if (item.ref.key!=pending.item.ref.key || item.at.inventory<0 || std::find(previousCopies.begin(),previousCopies.end(),item.at)!=previousCopies.end()) continue;
                        if (found>=0) { found=-2; break; }
                        found=static_cast<int>(i);
                    }
                    if (!sample.held && found>=0) {
                        acknowledged=true;
                        if (displacedReturn>=0) { bankReturns[displacedReturn].item.at=state.items[found].at; displacedReturn=-1; }
                    }
                }
            } else if (pending.operation==LoadoutOperation::Put && !sample.held) {
                for (const auto& item:state.items) if (item.ref.key==pending.item.ref.key && item.at==pending.destination) acknowledged=true;
                if (cancelled && storingReturn>=0) acknowledged=true;
            }
            if (!acknowledged) {
                if (now-sentAt>=8000) { cancelled=true; reason="The game has not confirmed the move. Waiting for it to finish."; frame.message=reason; }
                return;
            }
            waiting=false;
            if (pending.operation==LoadoutOperation::Equip) ++frame.completed;
            if (pending.operation==LoadoutOperation::Put) {
                if (displacedReturn>=0) { bankReturns[displacedReturn].item.at=pending.destination; displacedReturn=-1; }
                if (storingReturn>=0) { bankReturns.erase(bankReturns.begin()+storingReturn); storingReturn=-1; if (!cancelled) bankChanged=true; }
            }
            if (sample.held) {
                if (pending.operation==LoadoutOperation::Take && !cancelled) {
                    LoadoutStep equip{LoadoutOperation::Equip,sample.state.held,pending.slot,{}};
                    Request(image,sample,std::move(equip),sample.held,now); return;
                }
                if (pending.operation==LoadoutOperation::Store && !cancelled) {
                    const auto place=pending.destination;
                    if (place.inventory>=0 && place.inventory<static_cast<int>(state.bags.size()) && state.bags[place.inventory].bank &&
                        state.Free(place,state.held.width,state.held.height)) {
                        LoadoutStep put{LoadoutOperation::Put,state.held,-1,place};
                        Request(image,sample,std::move(put),sample.inventories[place.inventory],now); return;
                    }
                    cancelled=true; reason="Equipment change cancelled: bank space changed.";
                }
                const bool wasCancelled=cancelled;
                ReturnHeld(image,sample,now);
                if (!wasCancelled && !waiting) return;
                return;
            }
            pending={};
        }
        if (cancelled) { if (sample.held) ReturnHeld(image,sample,now); else Finish(reason); return; }
        if (sample.held) { Finish("Put down the item on your cursor before trying again."); return; }
        auto plan=PlanLoadout(sample.state,goal,bankReturns);
        if (!plan.error.empty()) { Finish(plan.error); return; }
        if (plan.steps.empty()) { sortRequested=bankChanged && sample.state.bankOpen; Finish("Equipped: "+goal.name); return; }
        const auto& step=plan.steps.front();
        uintptr_t object=0;
        for (size_t i=0;i<sample.state.items.size();++i) if (sample.state.items[i].ref.key==step.item.ref.key && sample.state.items[i].at==step.item.at) { object=sample.objects[i]; break; }
        if (!object) { Finish("Equipment changed. Try again."); return; }
        frame.message=(step.operation==LoadoutOperation::Store ? "Returning equipment to bank..." : "Equipping "+goal.name+"...");
        Request(image,sample,step,object,now);
    }
};
