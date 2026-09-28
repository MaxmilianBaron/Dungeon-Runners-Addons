#pragma once
#include "native_reader.h"
#include "bank_sort.h"
#include <future>

class NativeBankSort {
    struct Snapshot {
        uintptr_t bank = 0, unit = 0, container = 0;
        int selected = -1;
        BankLayout layout;
        std::vector<uintptr_t> inventories, objects;
    };
    BankSortSession session;
    uintptr_t owner = 0, window = 0;
    uint64_t nextSample = 0;
    BankSortFrame frame;
    std::future<BankPlan> planning;
    BankLayout planningSource;
    bool planningCancelled = false;
    static bool Access(uintptr_t image,uintptr_t container,uintptr_t inventory) {
        const uintptr_t function = image + 0x18e400;
        bool result = false;
        __try {
            __asm {
                mov eax, inventory
                push 0
                push container
                call function
                mov result, al
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return result;
    }
    static bool Send(uintptr_t image,uintptr_t container,uintptr_t object,const BankStep& step) {
        const uintptr_t function = image + (step.take ? 0x18beb0 : 0x18bd80);
        const bool take = step.take;
        const int x = step.destination.x, y = step.destination.y;
        __try {
            if (take) {
                __asm {
                    mov edi, container
                    mov eax, object
                    lock inc dword ptr [eax+4]
                    push eax
                    call function
                }
            } else {
                __asm {
                    mov edi, container
                    mov eax, object
                    lock inc dword ptr [eax+4]
                    push y
                    push x
                    push eax
                    call function
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return true;
    }
    static bool Geometry(const NativeReader& r,uintptr_t bank,uintptr_t ui,BankSortFrame& output) {
        const auto inventory = r.Pointer(bank+0x198);
        int32_t width = 0, height = 0;
        if (!inventory || !r.Read(inventory+0xf8,width) || !r.Read(inventory+0xfc,height) || width < 160 || height < 80 || width > 2048 || height > 2048) return false;
        output.x = 0; output.y = -26; output.visible = false;
        output.width = static_cast<float>(width); output.height = 22;
        std::set<uintptr_t> seen;
        for (auto node = inventory; node; node = r.Pointer(node+0x14)) {
            uint32_t flags = 0;
            if (seen.size() == 32 || !seen.insert(node).second || !r.Read(node+0xb4,flags) || !(flags&8)) return false;
            if (node == ui) { output.visible = seen.count(bank) != 0; return output.visible; }
            int32_t x = 0, y = 0;
            if (!r.Read(node+0xf0,x) || !r.Read(node+0xf4,y) || std::abs(int64_t(x)) > 16384 || std::abs(int64_t(y)) > 16384) return false;
            output.x += x; output.y += y;
        }
        return false;
    }
    static std::string Definition(const NativeReader& r,uintptr_t address) {
        std::array<uintptr_t,24> seen{};
        for (size_t depth = 0; address && depth < seen.size(); ++depth) {
            if (std::find(seen.begin(),seen.begin()+depth,address) != seen.begin()+depth) return {};
            seen[depth] = address;
            if (const auto type = r.Pointer(address+0x58)) return BankLower(r.String(type+0x18,256));
            address = r.Pointer(address+0x54);
        }
        return {};
    }
    static bool Item(const NativeReader& r,uintptr_t address,BankPosition position,BankItem& item) {
        const auto desc = r.Pointer(address+0x5c);
        uint8_t width = 0, height = 0, slot = 0, quality = 0, quantity = 0, flags = 0, flags2 = 0;
        if (!desc || !r.Read(address+0x68,item.id) || !r.Read(address+0x82,quantity) || !r.Read(desc+0xcb,width) || !r.Read(desc+0xcc,height) || !r.Read(desc+0xc8,slot) || !r.Read(desc+0xcd,quality) || !r.Read(desc+0xce,flags) || !r.Read(desc+0xcf,flags2)) return false;
        item.width = width; item.height = height; item.slot = slot; item.quality = quality; item.quantity = quantity;
        item.exchange = !(flags&0x40) && !(flags2&8);
        item.icon = r.String(desc+0xa4,256);
        item.definition = Definition(r,address);
        item.position = position;
        return item.id && width && height && quantity;
    }
    static bool Read(const NativeReader& r,uintptr_t image,Snapshot& result,BankSortFrame& output) {
        if (!r.InWorld()) return false;
        const auto ui = r.Pointer(image+0x5314b0);
        result.bank = r.Pointer(ui+0x23c);
        if (!result.bank || !Geometry(r,result.bank,ui,output) || r.Pointer(ui+0x180)) return false;
        const auto player = r.Pointer(r.Pointer(ui+0x1b4)+0xf8);
        result.unit = r.Pointer(player+0xb0);
        result.container = r.Pointer(result.bank+0x180);
        if (!result.unit || r.Pointer(result.bank+0x17c) != result.unit || !result.container || r.Pointer(result.container+0x14) != result.unit) return false;
        const auto selected = r.Pointer(r.Pointer(result.bank+0x198)+0x168);
        uint16_t level = 0;
        if (!r.Read(result.unit+0x314,level)) return false;
        std::set<uintptr_t> seen;
        for (auto node = r.Pointer(result.container+0x18); node; node = r.Pointer(node+0x20)) {
            if (seen.size() == 64 || !seen.insert(node).second || r.Pointer(node+0x14) != result.container) return false;
            const auto desc = r.Pointer(node+0x5c);
            if (!desc || !r.KindOf(desc,image+0x530b7c)) continue;
            uint8_t type = 0, width = 0, height = 0, id = 0, required = 0;
            if (!r.Read(desc+0xd7,type)) return false;
            if (type != 2) continue;
            if (!r.Read(desc+0xd4,required) || !r.Read(desc+0xd5,width) || !r.Read(desc+0xd6,height) || !r.Read(node+0x65,id)) return false;
            if (level < required || !Access(image,result.container,node)) continue;
            if (result.inventories.size() >= 16) return false;
            result.inventories.push_back(node);
            result.layout.pages.push_back({id,width,height});
        }
        std::vector<size_t> order(result.inventories.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(),order.end(),[&](size_t a,size_t b) { return result.layout.pages[a].id < result.layout.pages[b].id; });
        const auto inventories = result.inventories;
        const auto pages = result.layout.pages;
        for (size_t i = 0; i < order.size(); ++i) { result.inventories[i] = inventories[order[i]]; result.layout.pages[i] = pages[order[i]]; }
        seen.clear();
        for (size_t p = 0; p < result.inventories.size(); ++p) {
            const auto inventory = result.inventories[p];
            if (inventory == selected) result.selected = static_cast<int>(p);
            for (auto node = r.Pointer(inventory+0x18); node; node = r.Pointer(node+0x20)) {
                if (seen.size() >= 1024 || !seen.insert(node).second || r.Pointer(node+0x14) != inventory) return false;
                uint8_t x = 0, y = 0;
                BankItem item;
                if (!r.Read(node+0x80,x) || !r.Read(node+0x81,y) || !Item(r,node,{static_cast<int>(p),x,y},item)) return false;
                result.objects.push_back(node); result.layout.items.push_back(std::move(item));
            }
        }
        const auto held = r.Pointer(result.container+0x80);
        if (held) {
            BankItem item;
            if (!seen.insert(held).second || !Item(r,held,{},item)) return false;
            result.layout.active = item.id;
            result.objects.push_back(held); result.layout.items.push_back(std::move(item));
        }
        order.resize(result.objects.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(),order.end(),[&](size_t a,size_t b) { return result.layout.items[a].id < result.layout.items[b].id; });
        const auto objects = result.objects;
        const auto items = result.layout.items;
        for (size_t i = 0; i < order.size(); ++i) { result.objects[i] = objects[order[i]]; result.layout.items[i] = items[order[i]]; }
        BankGrid grid;
        return result.selected >= 0 && grid.Build(result.layout);
    }
public:
    static bool Navigation(const NativeReader& r,uintptr_t image,uintptr_t control) {
        const auto ui = r.Pointer(image+0x5314b0), bank = r.Pointer(ui+0x23c);
        const auto unit = r.Pointer(bank+0x17c), container = r.Pointer(bank+0x180);
        if (!bank || !unit || !container || r.Pointer(container+0x14) != unit) return false;
        bool allowed = false;
        for (unsigned depth = 0; control && depth < 24; ++depth) {
            if (control == bank) return allowed;
            const auto name = r.String(control+0x10,96);
            const auto parent = r.Pointer(control+0x14);
            if (name == "Close" || name == "Cancel") allowed = true;
            if (r.String(parent+0x10,96) == "TabPanel") {
                const auto inventory = r.Pointer(r.Pointer(control+0xec)+0x168), desc = r.Pointer(inventory+0x5c);
                uint8_t type = 0, required = 0;
                uint16_t level = 0;
                allowed = r.Pointer(inventory+0x14) == container && r.KindOf(desc,image+0x530b7c) &&
                    r.Read(desc+0xd7,type) && type == 2 && r.Read(desc+0xd4,required) && r.Read(unit+0x314,level) && level >= required && Access(image,container,inventory);
            }
            control = parent;
        }
        return false;
    }
    void Reset() { if (session.Busy()) session.Stop("Sorting stopped."); planningCancelled = true; owner = window = 0; nextSample = 0; frame = {}; }
    const BankSortFrame& Frame() const { return frame; }
    void Service(const NativeReader& reader,uintptr_t image,bool enabled,unsigned action,bool focused,uint64_t now) {
        if (action == 3 || !enabled || !focused) { session.Cancel(); planningCancelled = true; }
        if (session.Busy()) {
            const auto ui = reader.Pointer(image+0x5314b0), bank = reader.Pointer(ui+0x23c);
            BankSortFrame visible;
            if (!reader.InWorld() || bank != window || !Geometry(reader,bank,ui,visible)) {
                session.Stop("Sorting stopped: bank closed."); frame = {}; return;
            }
        }
        if (planning.valid()) {
            frame = {};
            const auto ui = reader.Pointer(image+0x5314b0), bank = reader.Pointer(ui+0x23c);
            if (!reader.InWorld() || bank != window || !Geometry(reader,bank,ui,frame)) planningCancelled = true;
            if (planning.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
                frame.busy = !planningCancelled; frame.message = "Planning bank layout...";
                return;
            }
            try {
                auto plan = planning.get();
                Snapshot current;
                if (!planningCancelled && Read(reader,image,current,frame) && current.unit == owner && current.bank == window && current.layout == planningSource)
                    session.StartPrepared(current.layout,std::move(plan),now);
                else session.Stop("Sorting cancelled before moving items.");
            } catch (...) { session.Stop("Bank layout could not be prepared."); }
            planningSource = {};
            nextSample = 0;
        }
        if (!enabled && !session.Busy()) { frame = {}; return; }
        if (now < nextSample && !action) return;
        nextSample = now + 16;
        if (!session.Busy() && !action) {
            frame = {};
            nextSample = now + 250;
            const auto ui = reader.Pointer(image+0x5314b0), bank = reader.Pointer(ui+0x23c);
            if (reader.InWorld() && bank && !reader.Pointer(ui+0x180) && Geometry(reader,bank,ui,frame)) {
                const auto container = reader.Pointer(bank+0x180);
                frame.available = container && !reader.Pointer(container+0x80);
                frame.message = session.Message();
            }
            return;
        }
        Snapshot sample;
        frame = {};
        const bool valid = Read(reader,image,sample,frame);
        if (!valid || (session.Busy() && (owner != sample.unit || window != sample.bank))) {
            if (session.Busy()) session.Stop("Stopped: bank closed or changed. Check the item on your cursor.");
            if (!valid) session.Stop("Bank page is unavailable.");
            frame.message = session.Message();
            return;
        }
        frame.available = !sample.layout.active;
        if (!session.Busy() && enabled && focused && (action == 1 || action == 2)) {
            owner = sample.unit; window = sample.bank;
            planningSource = sample.layout; planningCancelled = false;
            try { planning = std::async(std::launch::async,[layout=sample.layout,page=sample.selected,all=action==2] { return PlanBankSort(layout,page,all); }); }
            catch (...) { planningSource = {}; session.Stop("Bank layout could not be prepared."); }
        }
        if (session.Busy()) {
            if (const auto* step = session.Next(sample.layout,now,focused || sample.layout.active != 0)) {
                const int index = sample.layout.Find(step->item);
                const auto object = step->take ? (index >= 0 ? sample.objects[index] : 0) : sample.inventories[step->destination.page];
                session.Sent(object && Send(image,sample.container,object,*step),now);
            }
        }
        frame.busy = session.Busy() || planning.valid();
        frame.completed = static_cast<unsigned>(session.Completed()); frame.total = static_cast<unsigned>(session.Total());
        frame.message = planning.valid() ? "Planning bank layout..." : session.Message();
    }
};
