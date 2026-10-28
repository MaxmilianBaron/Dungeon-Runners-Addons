#pragma once
#include "windows_compat.h"

static void QueueLoadout(LoadoutCommand::Kind kind) {
    loadoutCommand={}; loadoutCommand.kind=kind; loadoutCommand.owner=loadoutFrame.owner;
    loadoutMessage.clear();
}
static void CaptureLoadout(int index) {
    QueueLoadout(LoadoutCommand::Kind::Capture); loadoutCapture=index;
    loadoutOpen=true;
    loadoutMessage="Saving equipment and hotbar...";
}
static void AcceptLoadoutResponse() {
    if (loadoutFrame.response==loadoutResponse) return;
    loadoutResponse=loadoutFrame.response;
    if (loadoutCapture==-2) return;
    const int index=loadoutCapture; loadoutCapture=-2;
    if (!loadoutFrame.captured) { loadoutMessage=loadoutFrame.message; return; }
    auto sets=loadoutStore.sets;
    if (index>=static_cast<int>(sets.size())) { loadoutMessage="This loadout is no longer available."; return; }
    auto captured=loadoutFrame.equipment;
    if (index>=0) captured.name=sets[index].name;
    else {
        for (unsigned n=1;n<=13;++n) {
            captured.name="Loadout "+std::to_string(n);
            if (std::none_of(sets.begin(),sets.end(),[&](const Loadout& value) { return value.name==captured.name; })) break;
        }
    }
    if (!captured.Valid()) { loadoutMessage="Equip your gear before saving a loadout."; return; }
    if (index>=0) sets[index]=captured;
    else if (sets.size()<12) sets.push_back(captured);
    else { loadoutMessage="Up to 12 loadouts can be saved per character."; return; }
    if (!loadoutStore.Save(sets)) { loadoutMessage=loadoutStore.error; return; }
    loadoutEditor=loadoutRenaming=false; loadoutMessage="Saved: "+captured.name;
}
static void CloseLoadouts() { loadoutOpen=loadoutEditor=loadoutRenaming=loadoutDelete=false; }
static void RenameLoadout() {
    if (!loadoutRenaming || loadoutEditing<0 || loadoutEditing>=static_cast<int>(loadoutStore.sets.size())) return;
    const auto name=loadoutName.Name();
    if (name.empty()) { loadoutMessage="Enter a name."; return; }
    auto sets=loadoutStore.sets;
    for (size_t i=0;i<sets.size();++i) if (static_cast<int>(i)!=loadoutEditing && sets[i].name==name) { loadoutMessage="This name is already in use."; return; }
    sets[loadoutEditing].name=name;
    if (!loadoutStore.Save(sets)) { loadoutMessage=loadoutStore.error; return; }
    loadoutRenaming=false; loadoutMessage="Name saved.";
}
static bool LoadoutInput(HWND window,UINT message,WPARAM key,LPARAM position) {
    if (message==WM_KEYUP && key<loadoutKeys.size() && loadoutKeys[key]) { loadoutKeys[key]=false; return true; }
    if (message==WM_KEYDOWN && key<loadoutKeys.size() && loadoutKeys[key] && !loadoutRenaming) return true;
    if (message==WM_CHAR && ((key==VK_RETURN && loadoutKeys[VK_RETURN]) || (key==VK_ESCAPE && loadoutKeys[VK_ESCAPE]))) return true;
    if (message==WM_KILLFOCUS) { if (loadoutFrame.busy) QueueLoadout(LoadoutCommand::Kind::Cancel); loadoutRenaming=false; loadoutKeys={}; }
    if (!addonRegistry.Loadouts() || !worldVisible || !loadoutFrame.visible || addonsOpen || reportOpen) return false;
    if (loadoutFrame.busy) {
        if (message==WM_KEYDOWN && key==VK_ESCAPE) { QueueLoadout(LoadoutCommand::Kind::Cancel); return true; }
        if (message==WM_KEYDOWN || message==WM_KEYUP || message==WM_CHAR) return true;
        if (message>=WM_MOUSEFIRST && message<=WM_MOUSELAST) {
            const POINT point{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
            const auto display=Ui::GetIO().DisplaySize;
            const bool navigation=(message==WM_MOUSEMOVE || message==WM_LBUTTONDOWN || message==WM_LBUTTONUP) && display.x>0 && display.y>0 &&
                ((loadoutInputTest && loadoutInputTest(point.x/display.x,point.y/display.y,true)) || (bankInputTest && bankInputTest(point.x/display.x,point.y/display.y,true)));
            if (navigation) return false;
            if (message==WM_MOUSEMOVE || LoadoutContains(point)) Ui::Message(window,message,key,position);
            return true;
        }
    }
    if (loadoutRenaming && (message==WM_KEYDOWN || message==WM_KEYUP || message==WM_CHAR)) {
        if (message==WM_KEYDOWN) {
            if (key<loadoutKeys.size()) loadoutKeys[key]=true;
            if (key==VK_RETURN) RenameLoadout();
            else if (key==VK_ESCAPE) { loadoutRenaming=false; loadoutMessage.clear(); }
            else if (key=='V' && (GetKeyState(VK_CONTROL)&0x8000)) {
                if (OpenClipboard(window)) {
                    const auto data=GetClipboardData(CF_UNICODETEXT);
                    const auto count=data ? std::min<size_t>(GlobalSize(data)/sizeof(wchar_t),4096) : 0;
                    const auto* text=data && count ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
                    if (text) {
                        const auto end=std::find(text,text+count,L'\0');
                        if (end==text+count || !loadoutName.Insert(std::wstring(text,end))) loadoutMessage="Use up to 80 bytes of text on one line.";
                        GlobalUnlock(data);
                    }
                    CloseClipboard();
                }
            } else loadoutName.Key(static_cast<unsigned>(key),(GetKeyState(VK_SHIFT)&0x8000)!=0,(GetKeyState(VK_CONTROL)&0x8000)!=0);
        } else if (message==WM_CHAR && key>=32 && key!=127 && key<=0xffff && !(GetKeyState(VK_CONTROL)&0x8000)) {
            if (!loadoutName.Character(static_cast<wchar_t>(key))) loadoutMessage="Name is too long or contains unsupported characters.";
            else loadoutMessage.clear();
        }
        return true;
    }
    if (loadoutOpen && message==WM_KEYDOWN && key==VK_ESCAPE) {
        if (loadoutEditor) loadoutEditor=false; else CloseLoadouts();
        loadoutKeys[VK_ESCAPE]=true; return true;
    }
    if (loadoutOpen && (message==WM_LBUTTONDOWN || message==WM_RBUTTONDOWN) && !LoadoutContains({GET_X_LPARAM(position),GET_Y_LPARAM(position)})) CloseLoadouts();
    return false;
}
static void LoadoutText(UiDrawList* draw,const std::string& name,UiPoint at,UiPoint size,UiPoint scale) {
    auto label=name;
    const float textSize=14*scale.y;
    if (valueFont->CalcTextSizeA(textSize,FLT_MAX,0,label.c_str()).x>size.x) {
        do {
            size_t end=label.size()-1;
            while (end && (static_cast<unsigned char>(label[end])&0xc0)==0x80) --end;
            label.resize(end);
        } while (!label.empty() && valueFont->CalcTextSizeA(textSize,FLT_MAX,0,(label+"...").c_str()).x>size.x);
        label+="...";
    }
    const auto extent=valueFont->CalcTextSizeA(textSize,FLT_MAX,0,label.c_str());
    draw->PushClipRect(at,{at.x+size.x,at.y+size.y});
    BodyText(draw,label.c_str(),{at.x+(size.x-extent.x)*.5f,at.y+(size.y-extent.y)*.5f},scale,GoldColor);
    draw->PopClipRect();
}
static bool LoadoutButton(const char* id,const char* label,UiPoint origin,UiPoint scale,float x,float y,float width,float height=28) {
    const auto at=At(origin,scale,x,y); const UiPoint size(width*scale.x,height*scale.y);
    Ui::SetCursorScreenPos(at);
    const bool clicked=Ui::InvisibleButton(id,size);
    nativeSkin.CompactButton(Ui::GetWindowDrawList(),at,size,scale,Ui::IsItemHovered(),Ui::IsItemActive(),label);
    return clicked;
}
static void DrawLoadouts() {
    static std::string sortMessage;
    static uint64_t sortMessageUntil=0;
    loadoutArea=loadoutListButton=loadoutSortButton=loadoutNameArea=inventoryArea={};
    if (!addonRegistry.Loadouts() || !loadoutFrame.visible || addonsOpen || reportOpen || moveEditing) return;
    if (loadoutStore.Owner()!=loadoutFrame.owner) { CloseLoadouts(); loadoutCapture=-2; loadoutMessage.clear(); loadoutStore.Select(loadoutFrame.owner); }
    AcceptLoadoutResponse();
    if (loadoutEditor && (loadoutEditing<0 || loadoutEditing>=static_cast<int>(loadoutStore.sets.size()))) loadoutEditor=loadoutRenaming=false;
    if (loadoutFrame.titleWidth<160 || loadoutFrame.titleHeight<12 || loadoutFrame.titleHeight>64) return;
    const auto display=Ui::GetIO().DisplaySize;
    const auto gameScale=GameScale();
    if (sortMessage!=inventoryFrame.message) {
        sortMessage=inventoryFrame.message;
        sortMessageUntil=!sortMessage.empty() && !inventoryFrame.busy ? WindowsCompat::Milliseconds()+8000 : 0;
    }
    const UiPoint title((loadoutFrame.titleX+loadoutFrame.titleWidth*.5f)*gameScale.x,loadoutFrame.titleY*gameScale.y);
    const bool busy=loadoutFrame.busy || bankFrame.busy || inventoryFrame.busy || loadoutCapture!=-2 || loadoutCommand.kind==LoadoutCommand::Kind::Equip;
    for (unsigned i=0;i<2;++i) {
        if (i && !bankEnabled && !inventoryFrame.busy) continue;
        const UiPoint at(title.x+(i ? 82.0f : -146.0f)*gameScale.x,title.y+(loadoutFrame.titleHeight-22)*.5f*gameScale.y);
        const float buttonWidth=i ? 52.0f : 66.0f;
        const UiPoint size(buttonWidth*gameScale.x,22*gameScale.y);
        if (at.x<0 || at.y<0 || at.x+size.x>display.x || at.y+size.y>display.y) continue;
        Ui::SetNextWindowPos(at); Ui::SetNextWindowSize(size);
        if (Ui::Begin(i ? "##LoadoutSort" : "##LoadoutList",nullptr,SurfaceFlags|Ui::NoSavedSettings)) {
            RegisterHitArea(); (i ? loadoutSortButton : loadoutListButton)=Rectangle(at,size);
            if (i) inventoryArea=loadoutSortButton;
            Ui::BeginDisabled(i ? !inventoryFrame.busy && (busy || loadoutRenaming || !inventoryFrame.visible || !inventoryFrame.available) : inventoryFrame.busy);
            if (LoadoutButton("button",i ? (inventoryFrame.busy ? "Cancel" : "Sort") : "Loadouts",at,gameScale,0,0,buttonWidth,22)) {
                if (i) { inventoryAction=inventoryFrame.busy ? 3 : 1; CloseLoadouts(); }
                else if (loadoutOpen) CloseLoadouts(); else loadoutOpen=true;
            }
            if (!loadoutOpen && (Ui::IsItemHovered() || (i && WindowsCompat::Milliseconds()<sortMessageUntil))) {
                const auto text=i ? (inventoryFrame.busy ? "Cancel inventory sorting." : inventoryFrame.message.empty() ? "Group coins and potions, pack equipment and leave room for loot." : inventoryFrame.message.c_str()) : "Equip or manage your saved loadouts.";
                QueueButtonHelp(text,at,size,gameScale);
            }
            Ui::EndDisabled();
        }
        Ui::End();
    }
    if (!loadoutOpen) return;
    const float padding=24.0f;
    const float width=loadoutEditor ? 360.0f : 272.0f;
    const float contentWidth=width-2*padding;
    const float listBottom=50.0f+36.0f*static_cast<float>(loadoutStore.sets.size());
    const float statusY=listBottom+10.0f+(loadoutFrame.busy ? 36.0f : 0.0f);
    const float height=loadoutEditor ? 536.0f : statusY+80.0f;
    auto scale=FitScale(GameScale(),{width,height});
    const float top=title.y+(loadoutFrame.titleHeight+4)*gameScale.y;
    const float fit=std::min(1.0f,std::max(1.0f,display.y-top-4)/(height*scale.y));
    scale={scale.x*fit,scale.y*fit};
    const UiPoint size(width*scale.x,height*scale.y);
    const UiPoint origin(std::clamp(title.x-size.x*.5f,0.0f,std::max(0.0f,display.x-size.x)),std::clamp(top,0.0f,std::max(0.0f,display.y-size.y)));
    Ui::SetNextWindowPos(origin); Ui::SetNextWindowSize(size);
    if (Ui::Begin("##Loadouts",nullptr,SurfaceFlags|Ui::NoSavedSettings)) {
        RegisterHitArea(); loadoutArea=Rectangle(origin,size);
        auto* draw=Ui::GetWindowDrawList();
        draw->AddRectFilled(At(origin,scale,15,11),At(origin,scale,width-15,height-11),UI_COLOR(0,0,0,210));
        nativeSkin.Frame(draw,origin,size,scale);
        nativeSkin.AlignedText(draw,"Loadouts",At(origin,scale,padding,18),{(contentWidth-106)*scale.x,24*scale.y},{scale.x*.8f,scale.y*.8f},true);
        if (!loadoutEditor) {
            Ui::BeginDisabled(busy || loadoutRenaming || !loadoutStore.Ready() || loadoutStore.sets.size()>=12);
            if (LoadoutButton("add","+",origin,scale,width-padding-94,18,26,24)) CaptureLoadout(-1);
            if (Ui::IsItemHovered()) QueueButtonHelp("Save your equipped gear and hotbar skills as a new loadout.",At(origin,scale,width-padding-94,18),{26*scale.x,24*scale.y},scale);
            Ui::EndDisabled();
        }
        if (LoadoutButton("close","Close",origin,scale,width-padding-60,18,60,24)) {
            CloseLoadouts(); loadoutArea={};
            Ui::End(); return;
        }
        if (!loadoutEditor) {
            Ui::BeginDisabled(busy || !loadoutStore.Ready());
            for (size_t i=0;i<loadoutStore.sets.size();++i) {
                Ui::PushID(static_cast<int>(i));
                const float y=50+36*static_cast<float>(i);
                if (LoadoutButton("equip","",origin,scale,padding,y,contentWidth-38)) {
                    QueueLoadout(LoadoutCommand::Kind::Equip); loadoutCommand.set=loadoutStore.sets[i];
                }
                LoadoutText(draw,loadoutStore.sets[i].name,At(origin,scale,padding+8,y),{(contentWidth-54)*scale.x,28*scale.y},scale);
                if (Ui::IsItemHovered()) QueueHelp(("Equip "+loadoutStore.sets[i].name).c_str(),At(origin,scale,width+4,y),scale,origin.x);
                if (LoadoutButton("edit","...",origin,scale,width-padding-30,y,30)) { loadoutEditor=true; loadoutEditing=static_cast<int>(i); loadoutDelete=loadoutRenaming=false; loadoutMessage.clear(); }
                Ui::PopID();
            }
            Ui::EndDisabled();
            if (loadoutFrame.busy && LoadoutButton("cancel","Cancel",origin,scale,padding,listBottom,contentWidth)) QueueLoadout(LoadoutCommand::Kind::Cancel);
            const auto& status=!loadoutStore.error.empty() ? loadoutStore.error : !loadoutMessage.empty() ? loadoutMessage : loadoutFrame.message;
            const std::string text=status.empty() ? "Save gear and hotbar with + in this menu. Select a loadout to equip it; ... opens its details." : status;
            const auto statusAt=At(origin,scale,padding+2,statusY);
            draw->PushClipRect(At(origin,scale,padding,statusY),At(origin,scale,width-padding,height-24));
            BodyText(draw,text.c_str(),statusAt,{scale.x*.9f,scale.y*.9f},BodyColor,(contentWidth-4)*scale.x); draw->PopClipRect();
            if (Ui::IsWindowHovered() && !status.empty()) QueueHelp(status.c_str(),At(origin,scale,width+4,50),scale,origin.x);
        } else {
            const auto& set=loadoutStore.sets[loadoutEditing];
            Ui::BeginDisabled(busy);
            if (loadoutRenaming) {
                const auto at=At(origin,scale,padding,50); const UiPoint extent(contentWidth*scale.x,28*scale.y);
                loadoutNameArea=Rectangle(at,extent);
                draw->AddRectFilled(at,{at.x+extent.x,at.y+extent.y},UI_COLOR(0,0,0,220));
                draw->AddLine({at.x,at.y+extent.y},{at.x+extent.x,at.y+extent.y},GoldColor,scale.y);
                Ui::SetCursorScreenPos(at);
                if (Ui::InvisibleButton("name",extent)) { loadoutName.anchor=0; loadoutName.caret=loadoutName.text.size(); }
                const auto text=LoadoutNameInput::Utf8(loadoutName.text);
                const auto prefix=LoadoutNameInput::Utf8(loadoutName.text.substr(0,loadoutName.caret));
                const auto anchor=LoadoutNameInput::Utf8(loadoutName.text.substr(0,loadoutName.anchor));
                const float caret=valueFont->CalcTextSizeA(14*scale.y,FLT_MAX,0,prefix.c_str()).x;
                const float selected=valueFont->CalcTextSizeA(14*scale.y,FLT_MAX,0,anchor.c_str()).x;
                const float offset=std::max(0.0f,caret-(contentWidth-20)*scale.x);
                const auto textAt=At(at,scale,8,6);
                draw->PushClipRect(At(at,scale,6,2),At(at,scale,contentWidth-6,26));
                if (caret!=selected) draw->AddRectFilled({textAt.x+std::min(caret,selected)-offset,at.y+4*scale.y},{textAt.x+std::max(caret,selected)-offset,at.y+24*scale.y},UI_COLOR(115,86,19,180));
                BodyText(draw,text.c_str(),{textAt.x-offset,textAt.y},scale);
                if ((WindowsCompat::Milliseconds()/500)%2==0) draw->AddLine({textAt.x+caret-offset,at.y+5*scale.y},{textAt.x+caret-offset,at.y+23*scale.y},GoldColor,scale.x);
                draw->PopClipRect();
                if (LoadoutButton("rename-save","Save name",origin,scale,padding,86,152)) RenameLoadout();
                if (LoadoutButton("rename-cancel","Cancel",origin,scale,184,86,152)) { loadoutRenaming=false; loadoutMessage.clear(); }
            } else {
                LoadoutText(draw,set.name,At(origin,scale,padding,50),{(contentWidth-92)*scale.x,28*scale.y},scale);
                if (LoadoutButton("rename","Rename",origin,scale,width-padding-84,50,84)) { loadoutName.Open(set.name); loadoutRenaming=true; loadoutDelete=false; loadoutMessage.clear(); }
                if (HoverArea(At(origin,scale,padding,50),{(contentWidth-92)*scale.x,28*scale.y})) QueueHelp(set.name.c_str(),At(origin,scale,width+4,50),scale,origin.x);
            }
            Ui::EndDisabled();
            for (size_t i=0;i<LoadoutSlots.size();++i) {
                const float y=122+28*static_cast<float>(i);
                const auto at=At(origin,scale,padding,y); const UiPoint extent(contentWidth*scale.x,26*scale.y);
                draw->AddRectFilled(at,{at.x+extent.x,at.y+extent.y},UI_COLOR(0,0,0,120));
                BodyText(draw,LoadoutSlots[i].name,At(at,scale,6,6),{scale.x*.9f,scale.y*.9f});
                const auto& ref=set.slots[i];
                draw->PushClipRect(At(at,scale,98,0),{at.x+extent.x-6*scale.x,at.y+extent.y});
                BodyText(draw,ref.key.empty() ? "Keep current" : ref.name.c_str(),At(at,scale,100,6),{scale.x*.9f,scale.y*.9f},ref.key.empty() ? MutedColor : GoldColor); draw->PopClipRect();
                if (HoverArea(at,extent) && !ref.name.empty()) QueueHelp(ref.name.c_str(),At(origin,scale,width+4,y),scale,origin.x);
            }
            Ui::BeginDisabled(busy || loadoutRenaming);
            if (LoadoutButton("update","Save gear + skills",origin,scale,padding,418,154)) CaptureLoadout(loadoutEditing);
            if (LoadoutButton("delete",loadoutDelete ? "Confirm" : "Delete",origin,scale,186,418,72)) {
                if (!loadoutDelete) loadoutDelete=true;
                else {
                    auto sets=loadoutStore.sets; sets.erase(sets.begin()+loadoutEditing);
                    if (loadoutStore.Save(sets)) { loadoutEditor=false; loadoutMessage="Loadout deleted."; }
                    else loadoutMessage=loadoutStore.error;
                }
            }
            Ui::EndDisabled();
            if (LoadoutButton("back","Back",origin,scale,266,418,70)) loadoutEditor=loadoutRenaming=false;
            const auto text=loadoutMessage.empty() ? (set.hotbarSaved ? "Gear and hotbar skills are saved. Save gear + skills replaces both with your current setup." : "This older loadout keeps your current skills. Save gear + skills adds your current hotbar.") : loadoutMessage;
            draw->PushClipRect(At(origin,scale,padding,466),At(origin,scale,width-padding,height-24));
            BodyText(draw,text.c_str(),At(origin,scale,padding+2,466),{scale.x*.9f,scale.y*.9f},BodyColor,(contentWidth-4)*scale.x); draw->PopClipRect();
        }
    }
    Ui::End();
}
static void OpenLoadoutSettings() { OpenBankSettings(); }
static void DrawLoadoutSettings(UiPoint origin,UiPoint scale) {
    auto* draw=Ui::GetWindowDrawList();
    Heading(draw,"Loadouts & Sorting",At(origin,scale,24,20),scale,302);
    if (OptionRow("Sorting:",draftBankEnabled ? "On" : "Off",At(origin,scale,10,58),scale,"Shows sorting buttons in Inventory and Bank.",At(origin,scale,355,58),origin.x)) draftBankEnabled=!draftBankEnabled;
    BodyText(draw,"+ in Loadouts saves gear and hotbar skills. Loadouts restores both from Inventory or an open bank; older saves keep your current skills.\n\nSort beside Inventory fills columns from top to bottom. Sort Page sorts the current bank page. Sort Pages lets you choose pages before sorting.",At(origin,scale,24,106),scale,BodyColor,302*scale.x);
    if (!bankMessage.empty()) BodyText(draw,bankMessage.c_str(),At(origin,scale,24,244),scale,GoldColor,302*scale.x);
    if (DrawSkinControl("Okay",At(origin,scale,24,280),scale)) { if (SaveBankSettings()) { bankEnabled=draftBankEnabled; activeAddon=nullptr; } else bankMessage="Settings could not be saved."; }
    if (DrawSkinControl("Back",At(origin,scale,185,280),scale)) activeAddon=nullptr;
}
extern "C" bool __cdecl MeterOverlayLoadouts(const LoadoutFrame* value,LoadoutCommand* command,bool* focused) {
    Lock lock;
    if (value) {
        if (loadoutFrame.owner!=value->owner) { CloseLoadouts(); loadoutCapture=-2; loadoutCommand={}; loadoutResponse=value->response; loadoutMessage.clear(); }
        if (!value->visible) { CloseLoadouts(); loadoutArea=loadoutListButton=loadoutSortButton=loadoutNameArea=inventoryArea={}; }
        loadoutFrame=*value;
    }
    if (command) { *command=std::move(loadoutCommand); loadoutCommand={}; }
    if (focused) *focused=gameWindow && GetForegroundWindow()==gameWindow && !addonsOpen && !reportOpen && !moveEditing;
    return addonRegistry.Loadouts();
}
extern "C" void __cdecl MeterOverlayLoadoutInputTest(AddonInputTest input) { Lock lock; loadoutInputTest=input; }
