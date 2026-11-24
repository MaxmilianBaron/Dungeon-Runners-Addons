#pragma once
#include "native_ui.h"
#include <algorithm>
#include <array>

class NativeQuestLog {
    static constexpr uintptr_t TaskControlSlot = 0x254;
    static constexpr uintptr_t TaskControlVtable = 0x44f8f8;
    static constexpr uintptr_t TaskPanelSlot = 0x19c;
    static constexpr uintptr_t LabelVtable = 0x449ee8;
    static constexpr uintptr_t LabelSetTextSlot = 0x98;
    static constexpr uintptr_t LabelText = 0xbc;

    uintptr_t panel=0,content=0,title=0,close=0,region=0;
    bool titleChanged=false;

    template<class Reader> static bool Visible(const Reader& reader,uintptr_t node,uintptr_t root) {
        for (unsigned depth=0;node && depth<16;++depth) {
            uint32_t flags=0;
            if (!reader.Read(node+0xb4,flags) || !(flags&8)) return false;
            if (node==root) return true;
            node=reader.Pointer(node+0x14);
        }
        return false;
    }

    template<class Reader> static uintptr_t OpenContent(const Reader& reader,uintptr_t image,uintptr_t ui,uintptr_t task) {
        if (!task || reader.Pointer(task)!=image+TaskControlVtable || !Visible(reader,task,ui)) return 0;
        const auto taskPanel=reader.Pointer(task+TaskPanelSlot);
        return taskPanel && reader.Pointer(taskPanel+0x14)==task && Visible(reader,taskPanel,task) ? taskPanel : 0;
    }

    template<class Reader> bool FindHeader(const Reader& reader,uintptr_t taskPanel,uintptr_t ui,const std::array<int64_t,4>& bounds) {
        std::array<uintptr_t,192> nodes{};
        nodes[0]=taskPanel;
        size_t count=1;
        uintptr_t foundTitle=0,foundClose=0,foundRegion=0;
        auto titleTop=bounds[1]+bounds[3]/4,closeTop=titleTop;
        for (size_t i=0;i<count;++i) {
            const auto node=nodes[i];
            const auto name=reader.String(node+0x10,96);
            if (name=="TaskList") continue;
            if (name=="VisibilityRegion") foundRegion=node;
            if (name=="Label" || name=="Close") {
                std::array<int64_t,4> rect{};
                if (Visible(reader,node,taskPanel) && reader.Rect(node,ui,rect) &&
                    rect[0]>=bounds[0] && rect[1]>=bounds[1] &&
                    rect[0]+rect[2]<=bounds[0]+bounds[2]) {
                    if (name=="Label" && rect[2]>=110 && rect[1]<titleTop) { foundTitle=node; titleTop=rect[1]; }
                    if (name=="Close" && rect[1]<closeTop) { foundClose=node; closeTop=rect[1]; }
                }
            }
            for (auto child=reader.Pointer(node+0x18);child;child=reader.Pointer(child+0x20)) {
                if (count==nodes.size() || reader.Pointer(child+0x14)!=node ||
                    std::find(nodes.begin(),nodes.begin()+count,child)!=nodes.begin()+count) break;
                nodes[count++]=child;
            }
        }
        title=foundTitle;
        close=foundClose;
        region=foundRegion;
        return title && close;
    }

public:
    static constexpr uintptr_t LabelSetText = 0x2a9ad0;

    uintptr_t Panel() const { return panel; }

    template<class Reader,class Setter> bool SyncTitle(const Reader& reader,uintptr_t image,bool collection,Setter setText) {
        if (!panel) return !collection;
        const auto type=reader.Pointer(title);
        if (type!=image+LabelVtable || reader.Pointer(type+LabelSetTextSlot)!=image+LabelSetText) return false;
        constexpr auto quest="Quest Log", log="Collection Log";
        const auto text=reader.String(title+LabelText,32);
        if (text!=quest && text!=log) return false;
        const auto desired=collection ? log : quest;
        if (text!=desired && !setText(title,desired)) return false;
        titleChanged=collection;
        return true;
    }

    template<class Reader> bool AcceptsInput(const Reader& reader,uintptr_t image,uintptr_t ui) const {
        return ui && reader.Pointer(ui+TaskControlSlot)==panel && OpenContent(reader,image,ui,panel)==content && content!=0;
    }

    template<class Reader> QuestLogFrame Read(const Reader& reader,uintptr_t image,uintptr_t ui,bool enabled) {
        const auto task=ui && (enabled || titleChanged) ? reader.Pointer(ui+TaskControlSlot) : 0;
        const auto taskPanel=OpenContent(reader,image,ui,task);
        panel=0;
        if (!taskPanel) {
            content=title=close=region=0;
            return {};
        }
        std::array<int64_t,4> a{},b{},c{};
        int32_t width=0,height=0;
        if (!reader.Read(ui+0xf8,width) || !reader.Read(ui+0xfc,height) || width<320 || height<200 ||
            !reader.Rect(taskPanel,ui,a) || a[2]<240 || a[3]<225) return {};
        if (content!=taskPanel || !title || !close) {
            content=taskPanel;
            if (!FindHeader(reader,taskPanel,ui,a)) return {};
        }
        if (!Visible(reader,title,taskPanel) || !Visible(reader,close,taskPanel) ||
            !reader.Rect(title,ui,b) || !reader.Rect(close,ui,c) || b[2]<110 || b[3]<12) {
            title=close=0;
            return {};
        }
        std::array<int64_t,4> visibleBounds{};
        if (region && reader.Rect(region,ui,visibleBounds) && visibleBounds[2]>=240 && visibleBounds[3]>=225) {
            const auto encloses=[&](const auto& rect) {
                return rect[0]>=visibleBounds[0] && rect[1]>=visibleBounds[1] &&
                    rect[0]+rect[2]<=visibleBounds[0]+visibleBounds[2] && rect[1]+rect[3]<=visibleBounds[1]+visibleBounds[3];
            };
            if (encloses(b) && encloses(c)) a=visibleBounds;
        }
        const auto normalize=[&](const auto& rect) {
            return std::array<float,4>{float(rect[0])/width,float(rect[1])/height,float(rect[2])/width,float(rect[3])/height};
        };
        panel=task;
        return {normalize(a),normalize(b),normalize(c)};
    }
};
