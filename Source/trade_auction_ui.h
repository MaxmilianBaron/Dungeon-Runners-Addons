enum class TradeWindowDrag { None,Move,Resize };
static TradeWindowDrag tradeWindowDrag=TradeWindowDrag::None;
static UiPoint tradeDragPointer,tradeDragOrigin,tradeDragLast;
static float tradeDragScale=0;

static void SaveTradeWindow() {
    if (!tradeWindowDirty || tradeWindowSettings.empty()) return;
    auto pending=tradeWindowSettings;
    pending+=L".pending";
    std::ofstream output(pending,std::ios::trunc);
    const auto& anchor=tradeWindowLayout.anchor.fraction;
    output.precision(9);
    output<<1<<' '<<anchor.x<<' '<<anchor.y<<' '<<tradeWindowLayout.preferredScale<<'\n';
    output.close();
    if (output.good() && MoveFileExW(pending.c_str(),tradeWindowSettings.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        tradeWindowDirty=false;
}

static void EndTradeWindowDrag() {
    const bool hadCapture=tradeWindowDrag!=TradeWindowDrag::None;
    tradeWindowDrag=TradeWindowDrag::None;
    if (hadCapture && GetCapture()==gameWindow) ReleaseCapture();
    SaveTradeWindow();
}

static bool TradeWindowInput(HWND window,UINT message,WPARAM wparam,LPARAM nativePosition,LPARAM position) {
    if (message==WM_CAPTURECHANGED || message==WM_KILLFOCUS || message==WM_CANCELMODE ||
        message==WM_NCDESTROY || (message==WM_ACTIVATEAPP && !wparam)) {
        EndTradeWindowDrag();
        return false;
    }
    if (!tradeOpen || !worldVisible || moveEditing || bankFrame.busy || inventoryFrame.busy) return false;
    if (message==WM_SETCURSOR && LOWORD(nativePosition)==HTCLIENT && !tradeHoldingItem) {
        POINT cursor{};
        if (!GetCursorPos(&cursor) || !ScreenToClient(window,&cursor)) return false;
        const auto location=OverlayMousePosition(window,WM_MOUSEMOVE,MAKELPARAM(cursor.x,cursor.y));
        cursor={GET_X_LPARAM(location),GET_Y_LPARAM(location)};
        const bool resize=tradeWindowDrag==TradeWindowDrag::Resize || PtInRect(&tradeResizeArea,cursor);
        const bool move=tradeWindowDrag==TradeWindowDrag::Move || PtInRect(&tradeTitleArea,cursor);
        if (!resize && !move) return false;
        SetCursor(LoadCursorW(nullptr,resize ? IDC_SIZENWSE : IDC_SIZEALL));
        return true;
    }
    const POINT pointer{GET_X_LPARAM(position),GET_Y_LPARAM(position)};
    const UiPoint mouse(static_cast<float>(pointer.x),static_cast<float>(pointer.y));
    const auto display=Ui::GetIO().DisplaySize;
    if (tradeWindowDrag!=TradeWindowDrag::None) {
        if (message==WM_MOUSEMOVE || message==WM_LBUTTONUP) {
            if (mouse.x!=tradeDragLast.x || mouse.y!=tradeDragLast.y) {
                const UiPoint delta(mouse.x-tradeDragPointer.x,mouse.y-tradeDragPointer.y);
                if (tradeWindowDrag==TradeWindowDrag::Move)
                    tradeWindowLayout.MoveTo({tradeDragOrigin.x+delta.x,tradeDragOrigin.y+delta.y},display);
                else tradeWindowLayout.ResizeFrom(tradeDragOrigin,delta,tradeDragScale,display);
                tradeDragLast=mouse;
                tradeWindowDirty=true;
            }
            Ui::GetIO().AddMousePosEvent(mouse.x,mouse.y);
            if (message==WM_LBUTTONUP) EndTradeWindowDrag();
            return true;
        }
        return message>=WM_MOUSEFIRST && message<=WindowsCompat::MouseHorizontalWheel;
    }
    if ((message!=WM_LBUTTONDOWN && message!=WM_LBUTTONDBLCLK) || tradeHoldingItem || dragging) return false;
    const bool resize=PtInRect(&tradeResizeArea,pointer);
    if (!resize && !PtInRect(&tradeTitleArea,pointer)) return false;
    tradeTyping=false;
    tradeWindowDrag=resize ? TradeWindowDrag::Resize : TradeWindowDrag::Move;
    tradeDragPointer=tradeDragLast=mouse;
    tradeDragOrigin={static_cast<float>(tradeArea.left),static_cast<float>(tradeArea.top)};
    tradeDragScale=tradeWindowLayout.Scale(display);
    SetCapture(window);
    return true;
}

static bool TradeMouseArea(POINT point) {
    return !tradeHoldingItem && (PtInRect(&tradeEntrance,point) || (tradeOpen && PtInRect(&tradeArea,point)));
}

static void CloseTrade() {
    EndTradeWindowDrag();
    tradeOpen=tradeTyping=tradeRefresh=tradeCapturing=false;
    tradeRequest={}; tradeDraft={}; tradeWantedSample={}; tradeChoices.clear(); tradeReplyItem.clear();
    tradeWatchStatus.clear();
    tradeArea=tradeEntrance=tradeSearchArea=tradeScrollArea=tradeTitleArea=tradeResizeArea={};
    tradeHoverBlocked=false;
}

static bool TradeKeyboard(UINT message,WPARAM key) {
    if (!tradeOpen || !tradeTyping) return false;
    if (message==WM_KILLFOCUS) { tradeTyping=false; return false; }
    if (message!=WM_KEYDOWN && message!=WM_KEYUP && message!=WM_CHAR) return false;
    if (message==WM_KEYDOWN) {
        if (key==VK_ESCAPE || key==VK_RETURN) {
            tradeTyping=false;
            if (key==VK_ESCAPE) consumeEscapeUp=true;
        } else tradeSearch.Key(static_cast<unsigned>(key),(GetKeyState(VK_SHIFT)&0x8000)!=0,(GetKeyState(VK_CONTROL)&0x8000)!=0);
    } else if (message==WM_CHAR && key>=32 && key!=127 && key<=0xffff && !(GetKeyState(VK_CONTROL)&0x8000)) {
        tradeSearch.Character(static_cast<wchar_t>(key));
    }
    return true;
}

static void TradeCatalogItems(TradeAuction::CatalogType type,bool search) {
    tradeCatalogPage=TradeCatalogPage::Items;
    tradeCatalogType=type;
    tradeSearch.Open({});
    tradeSearchFilter="\n";
    tradeTyping=search;
}

static void OpenTradeCatalog(TradeScreen back) {
    tradeTyping=false;
    tradeWatchStatus.clear();
    tradeCatalogPage=TradeCatalogPage::Groups;
    tradeCatalogBack=back;
    tradeScreen=TradeScreen::Catalog;
}

static const BankCatalogEntry* tradeVariantItem=nullptr;
static unsigned tradeSelectedTier=0;

static std::string TradeRollCaption(const TradeRollRecipe& recipe,size_t slot) {
    const std::string_view family=TradeRollGroups[recipe.slots[slot]].label;
    size_t ordinal=0,count=0;
    for (size_t i=0;i<recipe.slots.size() && recipe.slots[i];++i) {
        if (family!=TradeRollGroups[recipe.slots[i]].label) continue;
        ++count;
        if (i<=slot) ++ordinal;
    }
    if (count==1) return std::string(family);
    return std::string(family=="Bonuses" ? "Bonus" : family)+" "+std::to_string(ordinal);
}

static std::string TradeShieldFixedBonuses(std::string_view fixed) {
    constexpr std::string_view separator=" · ";
    std::string bonuses;
    while (!fixed.empty()) {
        const auto end=fixed.find(separator);
        const auto bonus=fixed.substr(0,end);
        if (bonus!="Block") {
            if (!bonuses.empty()) bonuses.append(separator);
            bonuses.append(bonus);
        }
        if (end==std::string_view::npos) break;
        fixed.remove_prefix(end+separator.size());
    }
    return bonuses;
}

static void ApplyTradeVariant(bool any) {
    if (!tradeVariantItem) return;
    tradeDraft.wantedDefinition=TradeAuction::CanonicalDefinition(tradeRecipes.empty() ? tradeVariantItem->key : tradeRecipes[tradeRecipeIndex]->item);
    tradeDraft.wanted=tradeVariantItem->label;
    tradeDraft.wantedSignature.clear();
    tradeDraft.wantedOptions.clear();
    if (tradeSelectedTier) tradeDraft.wantedOptions.push_back("Roll tier: "+std::to_string(tradeSelectedTier));
    if (!tradeRecipes.empty()) {
        const auto& recipe=*tradeRecipes[tradeRecipeIndex];
        if (*recipe.rarity) tradeDraft.wantedOptions.push_back(recipe.rarity);
        if (!any) {
            for (size_t i=0;i<recipe.slots.size();++i) if (const auto* option=TradeAuction::RollChoice(recipe.slots[i],tradeRolls[i])) {
                tradeDraft.wantedOptions.push_back(std::string(TradeRollGroups[recipe.slots[i]].label)+" · "+option->label);
            }
            if (*recipe.fixed && std::strlen(recipe.fixed)<=192 && tradeDraft.wantedOptions.size()<8)
                tradeDraft.wantedOptions.push_back(recipe.fixed);
        }
    }
    tradeStatOffset=0;
    tradeScreen=TradeScreen::Editor;
}

static void PickTradeVariant(const TradeAuction::CatalogItem& selection) {
    const auto& item=*selection.entry;
    tradeTyping=false;
    tradeWatchStatus.clear();
    tradeVariantItem=&item;
    tradeRecipes=selection.recipes;
    tradeRolls={};
    const auto saved=std::find_if(tradeRecipes.begin(),tradeRecipes.end(),[](const auto* recipe) {
        return TradeAuction::CanonicalDefinition(tradeDraft.wantedDefinition)==TradeAuction::CanonicalDefinition(recipe->item);
    });
    tradeRecipeIndex=saved==tradeRecipes.end() ? 0 : static_cast<size_t>(saved-tradeRecipes.begin());
    tradeSelectedTier=saved==tradeRecipes.end() ? 0 : TradeAuction::WantedTier(tradeDraft);
    if (tradeRecipes.empty()) ApplyTradeVariant(true);
    else tradeScreen=TradeScreen::Variant;
}

static void TradePick() {
    tradeTyping=false;
    tradeChoicePage=0;
    tradeChoices.clear();
    tradeScreen=TradeScreen::Picker;
    tradeScannedStorage=tradeStorage;
    tradeRequest={TradeAuction::Capture::Kind::Page,true,0,0};
    tradeCapturing=true;
    tradeStatus="Reading Inventory and Bank…";
}

static void OpenTrade() {
    tradeScreen=TradeScreen::Browse;
    tradeOpen=true;
    tradeMine=false;
    tradeTyping=false;
    tradePage={}; tradeDraft={}; tradeRequest={}; tradeChoices.clear();
    tradeBefore.clear(); tradeStatus.clear(); tradeStatOffset=0;
    tradeMarketPage=tradeMarketTarget=0;
    tradeRefresh=true;
    tradeCapturing=false;
    leaderboardOpen=false;
    addonsOpen=false;
    activeAddon=nullptr;
}

static void DrawTradeEntrance() {
    tradeEntrance={};
    const auto& frame=tradeGeometry;
    if (!tradeClient || !tradeEnabled || tradeOpen || addonsOpen || !frame.visible || !nativeSkin.Ready() ||
        inventoryFrame.busy || bankFrame.busy) return;
    const UiPoint display=Ui::GetIO().DisplaySize;
    auto scale=GameScale();
    UiPoint origin,size;
    if (!AttachedButton({frame.x,frame.y},{frame.width,frame.height},scale,display,origin,size)) return;
    scale.y=size.y/18;
    Ui::SetNextWindowPos(origin);
    Ui::SetNextWindowSize(size);
    if (Ui::Begin("##TradeInventory",nullptr,SurfaceFlags | Ui::NoSavedSettings)) {
        Ui::SetCursorScreenPos(origin);
        const bool clicked=Ui::InvisibleButton("open-trade",size);
        nativeSkin.CompactButton(Ui::GetWindowDrawList(),origin,size,scale,Ui::IsItemHovered(),Ui::IsItemActive(),"Trade Auction");
        tradeEntrance=Rectangle(origin,size);
        if (clicked) OpenTrade();
        RegisterHitArea();
    }
    Ui::End();
}

static void TradeRefresh() {
    tradeBefore.clear();
    tradeMarketTarget=0;
    tradeRefresh=true;
}

static void TradeResults(uint64_t now) {
    if (!tradeClient) return;
    if (auto result=tradeClient->Poll()) {
        if (!result->error.empty()) tradeStatus=result->error;
        else if (result->kind==TradeAuction::Result::Kind::List) {
            tradePage=std::move(result->page);
            tradeMarketPage=tradeMarketTarget;
            if (tradeScreen==TradeScreen::Browse) tradeStatus.clear();
            tradeLastSync=now;
        } else {
            tradeDraft={};
            tradeStatus="Offer saved.";
            tradeTyping=false;
            tradeScreen=TradeScreen::Browse;
            tradeMine=true;
            tradePage={};
            tradeMarketPage=0;
            TradeRefresh();
        }
    }
    if (!tradeClient->Configured()) return;
    const bool periodic=tradeScreen==TradeScreen::Browse && tradeBefore.empty() && now>=tradeLastSync+30000;
    if ((tradeRefresh || periodic) && tradeClient->Request({tradeMine,tradeBefore})) {
        tradeRefresh=false;
        tradeLastSync=now;
    }
}

static void DrawTradeWindow(uint64_t now) {
    if (!tradeOpen || !tradeClient || !nativeSkin.Ready()) return;
    tradeSearchArea=tradeScrollArea={};
    TradeResults(now);
    const auto display=Ui::GetIO().DisplaySize;
    constexpr auto width=TradeAuction::WindowLayout::Width,height=TradeAuction::WindowLayout::Height;
    const float factor=tradeWindowLayout.Scale(display);
    if (factor<=0) return;
    const UiPoint scale(factor,factor);
    const auto em=std::max(8,static_cast<int>(std::lround(17*scale.y)));
    if (tradeFontEm!=em) {
        tradeFontEm=em;
        auto raster=std::make_unique<UiFont>();
        tradeFont=raster->Load(graphics,SkinSylfaen.data(),SkinSylfaen.size(),em) ? std::move(raster) : nullptr;
    }
    auto* font=tradeFont ? tradeFont.get() : valueFont;
    const float textSize=tradeFont ? tradeFont->Height() : font->SizeForEm(static_cast<float>(em));
    constexpr UiColor tradeGold=UI_COLOR(255,214,118,255),tradeMuted=UI_COLOR(220,207,181,255);
    const auto size=tradeWindowLayout.Size(display);
    const auto placed=tradeWindowLayout.Origin(display);
    const UiPoint origin(std::floor(placed.x),std::floor(placed.y));
    auto point=[origin,scale](float x,float y){return At(origin,scale,x,y);};
    Ui::SetNextWindowPos(origin);
    Ui::SetNextWindowSize(size);
    const bool pointerAvailable=!tradeHoldingItem;
    if (Ui::Begin("##TradeAuction",nullptr,SurfaceFlags | Ui::NoSavedSettings | (pointerAvailable ? 0 : Ui::NoInputs))) {
        UiDrawList* draw=Ui::GetWindowDrawList();
        draw->AddRectFilled(point(8,8),point(width-8,height-8),UI_COLOR(19,14,9,255));
        nativeSkin.Frame(draw,origin,size,scale);
        nativeSkin.AlignedText(draw,"Trade Auction",point(24,12),{376*scale.x,30*scale.y},scale);
        const auto connection=tradeClient->Connection();
        const char* status="Checking…";
        UiColor statusColor=tradeMuted;
        if (connection==TradeAuction::ConnectionStatus::Connected) {
            status="Connected";
            statusColor=UI_COLOR(162,214,147,255);
        } else if (connection==TradeAuction::ConnectionStatus::Offline) {
            status="Offline";
            statusColor=UI_COLOR(238,160,137,255);
        }
        const float statusSize=textSize*.9f;
        const auto statusExtent=font->CalcTextSizeA(statusSize,FLT_MAX,0,status);
        const auto statusRight=point(400,27);
        draw->AddText(font,statusSize,{std::round(statusRight.x-statusExtent.x),
            std::round(statusRight.y-statusExtent.y*.5f)},statusColor,status);
        tradeTitleArea=Rectangle(point(16,9),{386*scale.x,38*scale.y});
        tradeResizeArea=Rectangle(point(width-25,height-25),{25*scale.x,25*scale.y});
        const auto cursor=Ui::GetIO().MousePos;
        const POINT mouse{static_cast<LONG>(cursor.x),static_cast<LONG>(cursor.y)};
        const auto gripColor=tradeWindowDrag==TradeWindowDrag::Resize || PtInRect(&tradeResizeArea,mouse) ? tradeGold : tradeMuted;
        draw->AddLine(point(width-17,height-8),point(width-8,height-17),gripColor,std::max(1.0f,scale.x));
        draw->AddLine(point(width-12,height-8),point(width-8,height-12),gripColor,std::max(1.0f,scale.x));
        auto textAt=[&](const char* text,UiPoint position,UiColor color,float wrap=0) {
            draw->AddText(font,textSize,{std::round(position.x),std::round(position.y)},color,text,nullptr,wrap);
        };
        auto textColor=[&](UiColor color) {
            return color==GoldColor ? tradeGold : color==MutedColor ? tradeMuted : color;
        };
        auto label=[&](const std::string& text,float x,float y,float available=475,UiColor color=BodyColor) {
            textAt(text.c_str(),point(x,y),textColor(color),available*scale.x);
        };
        auto line=[&](const std::string& text,float x,float y,float available,UiColor color=BodyColor) {
            const auto clipped=FitTextLine(*font,textSize,text.c_str(),(available-3)*scale.x);
            textAt(clipped.c_str(),point(x,y),textColor(color));
            return clipped!=text;
        };
        auto cell=[&](const std::string& text,float x,float y,float w,float h,UiColor color=BodyColor) {
            const float top=y+(h-textSize/scale.y)*.5f;
            if (line(text,x,top,w,color) && HoverArea(point(x,y),{w*scale.x,h*scale.y}))
                QueueButtonHelp(text.c_str(),point(x,y),{w*scale.x,h*scale.y},scale);
        };
        auto centered=[&](const std::string& text,float x,float y,float width=476) {
            const auto extent=font->CalcTextSizeA(textSize,FLT_MAX,width*scale.x,text.c_str());
            auto at=point(x,y);
            at.x-=extent.x*.5f;
            textAt(text.c_str(),at,tradeMuted,width*scale.x);
        };
        auto pages=[&](size_t current,size_t total,float x) {
            centered(TradeAuction::PageLabel(current,total),x,483+(30-textSize/scale.y)*.5f,80);
        };
        auto itemHeader=[&](const std::string& name,const std::string& definition,const std::string& subtitle) {
            constexpr float top=92,h=48;
            tradeIcons.Draw(draw,graphics,definition,point(27,top+2),{46*scale.x,44*scale.y});
            if (subtitle.empty()) {
                cell(name,87,top,412,h,GoldColor);
            } else {
                cell(name,87,top,412,h*.5f,GoldColor);
                cell(subtitle,87,top+h*.5f,412,h*.5f,MutedColor);
            }
        };
        auto button=[&](const char* id,const char* text,float x,float y,float w,bool enabled=true,float h=30) {
            Ui::BeginDisabled(!enabled);
            const auto at=point(x,y);
            const UiPoint size(w*scale.x,h*scale.y);
            Ui::SetCursorScreenPos(at);
            const bool pressed=Ui::InvisibleButton(id,size);
            nativeSkin.CompactButton(draw,at,size,scale,Ui::IsItemHovered(),Ui::IsItemActive(),text);
            Ui::EndDisabled();
            return pressed && enabled;
        };
        auto formHeight=[&](const std::string& value) {
            return std::max(30.0f,font->CalcTextSizeA(textSize,FLT_MAX,328*scale.x,value.c_str()).y/scale.y+6);
        };
        auto formValue=[&](const std::string& name,const std::string& value,float y,float limit) {
            constexpr float x=171,w=328,rowHeight=30;
            const float h=formHeight(value);
            cell(name,26,y,134,rowHeight,GoldColor);
            if (y+h<=limit) {
                const auto at=point(x,y+(rowHeight-textSize/scale.y)*.5f);
                textAt(value.c_str(),at,BodyColor,w*scale.x);
                return h;
            }
            cell(value,x,y,w,rowHeight);
            return rowHeight;
        };
        auto row=[&](const std::string& title,const std::string& metadata,const std::string& definition,float top,float clickWidth=478) {
            const auto at=point(24,top);
            const UiPoint block(478*scale.x,52*scale.y);
            const UiPoint hitSize(clickWidth*scale.x,block.y);
            Ui::SetCursorScreenPos(at);
            const bool clicked=Ui::InvisibleButton("item",hitSize);
            const bool hovered=Ui::IsItemHovered();
            nativeSkin.Row(draw,at,block,scale,0,hovered);
            tradeIcons.Draw(draw,graphics,definition,point(29,top+5),{40*scale.x,42*scale.y});
            const UiPoint innerMin=point(80,top+7);
            const UiPoint innerMax=point(24+clickWidth-12,top+45);
            const auto lines=CenterTwoLines(innerMin.y,innerMax.y,textSize,std::round(2*scale.y));
            const float available=innerMax.x-innerMin.x-3*scale.x;
            const auto name=FitTextLine(*font,lines.size,title.c_str(),available);
            const auto info=FitTextLine(*font,lines.size,metadata.c_str(),available);
            const float x=std::round(innerMin.x);
            draw->PushClipRect(innerMin,innerMax);
            draw->AddText(font,lines.size,{x,lines.first},tradeGold,name.c_str());
            draw->AddText(font,lines.size,{x,lines.second},tradeMuted,info.c_str());
            draw->PopClipRect();
            if (hovered) {
                const bool overMetadata=Ui::GetIO().MousePos.y>=lines.second;
                if (overMetadata && info!=metadata) QueueButtonHelp(metadata.c_str(),at,hitSize,scale);
                else if (name!=title) QueueButtonHelp(title.c_str(),at,hitSize,scale);
            }
            return clicked;
        };
        const bool busy=tradeClient->Busy() || tradeCapturing;
        const char* privateHelp=nullptr;
        if (button("close","Close",414,12,88)) { CloseTrade(); Ui::End(); return; }
        const auto browse=[&](bool mine) {
            tradeScreen=TradeScreen::Browse; tradeMine=mine; tradePage={}; tradeDraft={};
            tradeMarketPage=0;
            tradeTyping=false; tradeRequest={}; tradeWantedSample={}; tradeCapturing=false; tradeStatus.clear(); tradeReplyItem.clear(); TradeRefresh();
        };
        const auto track=[&](const TradeAuction::Offer& offer,float x,float y,float h) {
            const bool active=tradeClient->Tracking(offer.id);
            if (button("track",active ? "Untrack" : "Track",x,y,89,!busy && tradeClient->Configured(),h))
                tradeStatus=tradeClient->ToggleTrack(offer);
            if (Ui::IsItemHovered()) QueueButtonHelp(!active && !TradeAuction::CanTrack(offer) ?
                "Exact drop alerts require an offer for a specific rolled item." : "Get an Info alert when the exact requested item drops.",
                point(x,y),{89*scale.x,h*scale.y},scale);
        };
        if (button("market","Market",24,54,91,!busy)) browse(false);
        if (button("mine","My Offers",121,54,104,!busy)) browse(true);
        if (button("new","New Offer",231,54,111,!busy)) { tradeWantedSample={}; tradeReplyItem.clear(); TradePick(); }
        if (button("wanted-request","Looking for Item",348,54,154,!tradeCapturing)) {
            const bool inCatalog=tradeScreen==TradeScreen::Catalog || tradeScreen==TradeScreen::Variant ||
                tradeScreen==TradeScreen::RollChoices || tradeScreen==TradeScreen::Watches || tradeScreen==TradeScreen::WatchDetails;
            const bool editing=tradeScreen==TradeScreen::Editor || (inCatalog && tradeCatalogBack==TradeScreen::Editor);
            if (!editing) tradeDraft={};
            tradeWantedSample={};
            tradeReplyItem.clear();
            OpenTradeCatalog(editing ? TradeScreen::Editor : TradeScreen::Browse);
        }

        if (tradeScreen==TradeScreen::Browse) {
            label(tradeMine ? "My Offers" : "Open Offers",26,94,450,GoldColor);
            if (tradePage.offers.empty() && !busy) centered(tradeMine ? "You have no offers." : "No open offers.",263,139);
            for (size_t i=0;i<tradePage.offers.size();++i) {
                const auto& entry=tradePage.offers[i];
                const float top=119+57*static_cast<float>(i);
                const auto title=entry.WantedOnly() ? "Looking for "+entry.wanted : entry.item.name;
                const bool showTrack=TradeAuction::CanTrack(entry);
                const auto& definition=entry.WantedOnly() ? entry.wantedDefinition : entry.item.definition;
                const auto tier=entry.WantedOnly() ? TradeAuction::WantedTier(entry) : entry.item.rollTier;
                const auto metadata=TradeAuction::TierLabel(tier)+"  ·  Listed by "+entry.character+
                    (entry.WantedOnly() || entry.wantedDefinition.empty() ? "" : "  ·  Wants "+entry.wanted);
                Ui::PushID(static_cast<int>(i));
                if (row(title,metadata,definition,top,showTrack ? 371.0f : 478.0f) && !busy) {
                    tradeDraft=entry; tradeScreen=TradeScreen::Details;
                    tradeStatOffset=0; tradeShowingWanted=false;
                }
                if (showTrack) track(entry,402,top+13,26);
                Ui::PopID();
            }
            if (button("refresh","Refresh",24,483,96,!busy)) TradeRefresh();
            if (button("first","First Page",132,483,126,tradeMarketPage>0 && !busy)) TradeRefresh();
            if (button("next","Next Page",270,483,126,!tradePage.next.empty() && !busy)) {
                tradeBefore=tradePage.next;
                tradeMarketTarget=tradeMarketPage+1;
                tradeRefresh=true;
            }
            if (!tradePage.offers.empty()) {
                const auto current=tradeMarketPage+1;
                const auto total=tradePage.pageCount ? std::max(current,static_cast<size_t>(*tradePage.pageCount)) :
                    tradePage.next.empty() ? current : 0;
                pages(current,total,447);
            }
        } else if (tradeScreen==TradeScreen::Picker) {
            if (!tradeCapturing && tradeStorage!=tradeScannedStorage) TradePick();
            label("Choose the item you are offering.",26,94);
            if (!tradeCapturing && tradeChoices.empty() && tradeStatus.empty())
                centered(!tradeReplyItem.empty() ? "No tradable copies of this item found." : "No tradable items found.",263,139);
            const size_t start=tradeChoicePage*6,end=std::min(start+6,tradeChoices.size());
            for (size_t i=start;i<end;++i) {
                const auto& choice=tradeChoices[i];
                Ui::PushID(static_cast<int>(i));
                const float top=119+54*static_cast<float>(i-start);
                const auto metadata=std::string(choice.inventory ? "Inventory" : "Bank")+"  ·  "+TradeAuction::TierLabel(choice.rollTier);
                if (row(choice.name,metadata,choice.definition,top) && !tradeCapturing) {
                    tradeRequest={TradeAuction::Capture::Kind::Item,choice.inventory,choice.id,tradeCharacterId};
                    tradeCapturing=true;
                    tradeStatus="Reading the item tooltip…";
                }
                Ui::PopID();
            }
            if (tradeStatus.empty() && !(tradeStorage&2u)) centered("Open Bank to include bank items.",263,451);
            if (!tradeChoices.empty()) pages(tradeChoicePage+1,TradeAuction::PageCount(tradeChoices.size(),6),270);
            if (button("refresh-items","Refresh Items",24,483,119,!tradeCapturing)) TradePick();
            if (button("previous-items","Previous",153,483,94,tradeChoicePage>0 && !tradeCapturing)) --tradeChoicePage;
            if (button("next-items","Next",293,483,94,end<tradeChoices.size() && !tradeCapturing)) ++tradeChoicePage;
            if (button("back-items","Back",414,483,88,!tradeCapturing))
                tradeScreen=TradeScreen::Browse;
        } else if (tradeScreen==TradeScreen::Catalog) {
            using Group=TradeAuction::CatalogGroup;
            using Type=TradeAuction::CatalogType;
            const auto group=static_cast<size_t>(tradeCatalogGroup);
            if (tradeCatalogPage==TradeCatalogPage::Groups) {
                centered("Choose a category",263,97);
                for (size_t i=1;i<std::size(TradeAuction::GroupNames);++i) {
                    Ui::PushID(static_cast<int>(i));
                    const float rowIndex=static_cast<float>((i-1)/2);
                    const float x=i+1==std::size(TradeAuction::GroupNames) && i%2 ? 147 : (i%2 ? 24 : 270);
                    if (button("group",TradeAuction::GroupNames[i],x,134+rowIndex*42,232)) {
                        tradeCatalogGroup=static_cast<Group>(i);
                        tradeCatalogPage=TradeCatalogPage::Types;
                    }
                    Ui::PopID();
                }
                if (button("search-all","Search All Items",24,290,232)) {
                    tradeCatalogGroup=Group::All;
                    TradeCatalogItems(Type::All,true);
                }
                if (button("my-watches","My Watches",270,290,232)) {
                    tradeWatchStatus.clear();
                    tradeWatchPage=0;
                    tradeScreen=TradeScreen::Watches;
                }
            } else if (tradeCatalogPage==TradeCatalogPage::Types) {
                static const auto types=[] {
                    std::array<std::array<bool,std::size(TradeAuction::TypeNames)>,std::size(TradeAuction::GroupNames)> available{};
                    for (const auto& item:TradeAuction::SortedCatalog()) {
                        const auto section=TradeAuction::Section(*item.entry);
                        available[static_cast<size_t>(section.group)][static_cast<size_t>(section.type)]=true;
                    }
                    return available;
                }();
                centered(TradeAuction::GroupNames[group],263,97);
                const auto count=1+static_cast<size_t>(std::count(types[group].begin()+1,types[group].end(),true));
                size_t shown=0;
                for (size_t i=0;i<std::size(TradeAuction::TypeNames);++i) {
                    if (i && !types[group][i]) continue;
                    Ui::PushID(static_cast<int>(i));
                    const float rowIndex=static_cast<float>(shown/2);
                    const float x=shown+1==count && count%2 ? 147 : (shown%2 ? 270 : 24);
                    if (button("type",TradeAuction::TypeNames[i],x,134+rowIndex*42,232))
                        TradeCatalogItems(static_cast<Type>(i),false);
                    Ui::PopID();
                    ++shown;
                }
            } else {
                const auto search=tradeSearch.Name();
                const auto filter=BankLower(search);
                if (tradeSearchFilter!=filter) {
                    tradeSearchFilter=filter;
                    tradeChoicePage=0;
                    tradeCatalog.clear();
                    for (const auto& item:TradeAuction::SortedCatalog()) {
                        const auto section=TradeAuction::Section(*item.entry);
                        if (tradeCatalogGroup!=Group::All && section.group!=tradeCatalogGroup) continue;
                        if (tradeCatalogType!=Type::All && section.type!=tradeCatalogType) continue;
                        if (BankLower(item.entry->label).find(filter)!=std::string::npos) tradeCatalog.push_back(item);
                    }
                }
                const auto path=std::string(TradeAuction::GroupNames[group])+
                    (tradeCatalogType==Type::All ? "" : "  /  "+std::string(TradeAuction::TypeNames[static_cast<size_t>(tradeCatalogType)]));
                cell(path,26,87,474,29,GoldColor);
                const auto searchAt=point(24,119);
                const UiPoint searchSize(478*scale.x,30*scale.y);
                tradeSearchArea=Rectangle(searchAt,searchSize);
                Ui::SetCursorScreenPos(searchAt);
                const bool wasTyping=tradeTyping;
                if (Ui::InvisibleButton("search",searchSize)) tradeTyping=true;
                nativeSkin.Row(draw,searchAt,searchSize,scale,0,tradeTyping || Ui::IsItemHovered());
                const float searchTextY=searchAt.y+(searchSize.y-textSize)*.5f;
                if (!tradeTyping) {
                    if (search.empty()) centered("Search Items",263,(searchTextY-origin.y)/scale.y);
                    else cell(search,34,119,458,30);
                } else {
                    const auto prefix=LoadoutNameInput::Utf8(tradeSearch.text.substr(0,tradeSearch.caret));
                    const auto anchor=LoadoutNameInput::Utf8(tradeSearch.text.substr(0,tradeSearch.anchor));
                    const float caret=font->CalcTextSizeA(textSize,FLT_MAX,0,prefix.c_str()).x;
                    const float selected=font->CalcTextSizeA(textSize,FLT_MAX,0,anchor.c_str()).x;
                    const float offset=std::max(0.0f,caret-456*scale.x);
                    const float left=point(34,119).x-offset;
                    draw->PushClipRect(point(33,122),point(493,146));
                    if (caret!=selected)
                        draw->AddRectFilled({left+std::min(caret,selected),point(24,123).y},
                            {left+std::max(caret,selected),point(24,145).y},UI_COLOR(115,86,19,180));
                    const auto typed=LoadoutNameInput::Utf8(tradeSearch.text);
                    textAt(typed.c_str(),{left,searchTextY},BodyColor);
                    if (!wasTyping || now/500%2==0)
                        draw->AddLine({left+caret,point(24,124).y},{left+caret,point(24,144).y},GoldColor,scale.x);
                    draw->PopClipRect();
                }
                const size_t start=tradeChoicePage*5,end=std::min(start+5,tradeCatalog.size());
                if (tradeCatalog.empty()) centered("No matching items.",263,171);
                else pages(tradeChoicePage+1,TradeAuction::PageCount(tradeCatalog.size(),5),270);
                for (size_t i=start;i<end;++i) {
                    const auto& item=tradeCatalog[i];
                    Ui::PushID(static_cast<int>(i));
                    const float top=159+57*static_cast<float>(i-start);
                    if (row(item.entry->label,std::string(item.rarity)+"  ·  Tier: Any",item.entry->key,top)) {
                        PickTradeVariant(item);
                    }
                    Ui::PopID();
                }
                if (button("previous-catalog","Previous",153,483,94,tradeChoicePage>0)) --tradeChoicePage;
                if (button("next-catalog","Next",293,483,94,end<tradeCatalog.size())) ++tradeChoicePage;
            }
            if (button("back-catalog","Back",414,483,88)) {
                tradeTyping=false;
                if (tradeCatalogPage==TradeCatalogPage::Groups) tradeScreen=tradeCatalogBack;
                else if (tradeCatalogPage==TradeCatalogPage::Types || tradeCatalogGroup==Group::All) tradeCatalogPage=TradeCatalogPage::Groups;
                else tradeCatalogPage=TradeCatalogPage::Types;
            }
        } else if (tradeScreen==TradeScreen::Variant || tradeScreen==TradeScreen::RollChoices) {
            const auto& recipe=*tradeRecipes[tradeRecipeIndex];
            if (tradeScreen==TradeScreen::Variant) {
                std::string variant=recipe.rarity;
                if (tradeRecipes.size()>1) {
                    std::string material;
                    if (std::any_of(tradeRecipes.begin(),tradeRecipes.end(),[&](const auto* other) {
                        return TradeAuction::CanonicalDefinition(recipe.item)!=TradeAuction::CanonicalDefinition(other->item);
                    })) if (const auto* definition=FindBankDefinition(TradeAuction::CanonicalDefinition(recipe.item).data()))
                        material=TradeAuction::VariantMaterial(*definition);
                    variant+=material.empty() ? " · Variant "+std::to_string(tradeRecipeIndex+1)+"/"+std::to_string(tradeRecipes.size()) : " · "+material;
                    if (button("prev-recipe","<",398,144,46,tradeRecipeIndex>0)) { --tradeRecipeIndex; tradeRolls={}; tradeWatchStatus.clear(); }
                    if (button("next-recipe",">",454,144,46,tradeRecipeIndex+1<tradeRecipes.size())) { ++tradeRecipeIndex; tradeRolls={}; tradeWatchStatus.clear(); }
                }
                itemHeader(tradeVariantItem->label,tradeVariantItem->key,variant);
                const auto tierText=tradeSelectedTier ? TradeAuction::TierLabel(tradeSelectedTier) : "Tier: Any";
                if (button("roll-tier",tierText.c_str(),199,144,128)) {
                    tradeSelectedTier=(tradeSelectedTier+1)%4;
                    tradeWatchStatus.clear();
                }
                draw->AddLine(point(24,183),point(502,183),UI_COLOR(115,86,45,180),std::max(1.0f,scale.y));
                float y=190;
                const bool shield=tradeVariantItem->slot==11;
                if (const auto* armor=TradeAuction::FindArmorRating(recipe.item)) {
                    const unsigned quality=std::string_view(recipe.rarity)=="Mythic" ? 7 : 5;
                    cell("Defense Rating",26,y,136,26,GoldColor);
                    if (tradeSelectedTier) {
                        cell(std::to_string(TradeAuction::DefenseRating(*armor,tradeSelectedTier,quality)),171,y,328,26);
                    } else {
                        for (unsigned tier=1;tier<=3;++tier) {
                            const auto text="T"+std::to_string(tier)+": "+std::to_string(TradeAuction::DefenseRating(*armor,tier,quality));
                            cell(text,171+110.f*(tier-1),y,104,26);
                        }
                    }
                    y+=30;
                }
                if (shield) y+=formValue("Block",TradeAuction::ShieldBlockLabel(recipe,tradeSelectedTier,tradeRolls),y,477);
                bool allSelected=tradeSelectedTier!=0;
                for (size_t i=0;i<recipe.slots.size() && recipe.slots[i];++i) {
                    const auto* roll=TradeAuction::RollChoice(recipe.slots[i],tradeRolls[i]);
                    allSelected&=roll!=nullptr;
                    cell(TradeRollCaption(recipe,i),26,y,134,28,GoldColor);
                    Ui::PushID(static_cast<int>(i));
                    if (button("select-roll",roll ? roll->label : "Any",171,y,329,true,28)) {
                        tradeRollSlot=i;
                        tradeRollPage=0;
                        tradeScreen=TradeScreen::RollChoices;
                    }
                    Ui::PopID();
                    y+=32;
                }
                const auto fixed=shield ? TradeShieldFixedBonuses(recipe.fixed) : recipe.fixed;
                if (!fixed.empty()) formValue("Fixed Bonuses",fixed,y+3,477);
                const auto watch=TradeAuction::MakeLocalWatch(recipe,tradeSelectedTier,tradeRolls);
                const bool tracked=watch && tradeWatchList.Contains(watch->roll);
                privateHelp=!tradeWatchList.Ready() ? "The private watch list could not be loaded." :
                    !tradeWatchAvailable || !recipe.exact || (allSelected && !watch) ?
                        "Exact tracking is unavailable for this variant." :
                    !watch ? "Choose a tier and every roll to track privately." : "Private watches are saved on this PC.";
                if (button("use-variant","Use Variant",24,483,113)) ApplyTradeVariant(false);
                if (button("any-variant","Any Variant",145,483,115)) ApplyTradeVariant(true);
                if (button("private-track",tracked ? "Untrack Me" : "Track for Me",268,483,134,
                    tradeWatchList.Ready() && (tracked || (tradeWatchAvailable && watch.has_value()))))
                    tradeWatchStatus=tradeWatchList.Toggle(*watch);
                if (button("back-variant","Back",414,483,88)) tradeScreen=TradeScreen::Catalog;
            } else {
                const auto group=recipe.slots[tradeRollSlot];
                const auto& slot=TradeRollGroups[group];
                itemHeader(tradeVariantItem->label,tradeVariantItem->key,TradeRollCaption(recipe,tradeRollSlot));
                std::vector<unsigned> choices{0};
                auto candidate=tradeRolls;
                for (unsigned i=1;i<=slot.count;++i) {
                    candidate[tradeRollSlot]=i;
                    if (TradeAuction::CompatibleRolls(recipe,candidate)) choices.push_back(i);
                }
                const size_t start=tradeRollPage*7,end=std::min(start+7,choices.size());
                pages(tradeRollPage+1,TradeAuction::PageCount(choices.size(),7),270);
                for (size_t i=start;i<end;++i) {
                    const auto* roll=TradeAuction::RollChoice(group,choices[i]);
                    Ui::PushID(static_cast<int>(i));
                    if (button("roll-option",roll ? roll->label : "Any",24,155+44*static_cast<float>(i-start),478)) {
                        tradeRolls[tradeRollSlot]=choices[i];
                        tradeWatchStatus.clear();
                        tradeScreen=TradeScreen::Variant;
                    }
                    Ui::PopID();
                }
                if (button("prev-roll","Previous",153,483,94,tradeRollPage>0)) --tradeRollPage;
                if (button("next-roll","Next",293,483,94,end<choices.size())) ++tradeRollPage;
                if (button("back-roll","Back",414,483,88)) tradeScreen=TradeScreen::Variant;
            }
        } else if (tradeScreen==TradeScreen::Watches) {
            label("My Watches",26,97,475,GoldColor);
            privateHelp=tradeWatchList.Ready() ? "Private watches are saved on this PC." : "The private watch list could not be loaded.";
            const auto watches=tradeWatchList.Snapshot();
            const size_t count=watches ? watches->size() : 0;
            const size_t start=tradeWatchPage*6,end=std::min(start+6,count);
            if (!count && tradeWatchList.Ready()) centered("You have no private watches.",263,139);
            if (count) pages(tradeWatchPage+1,TradeAuction::PageCount(count,6),270);
            for (size_t i=start;i<end;++i) {
                const auto& watch=(*watches)[i];
                const auto* item=FindBankDefinition(watch.roll.definition.c_str());
                auto metadata=std::string(watch.roll.quality==7 ? "Mythic" : "Unique")+"  ·  "+TradeAuction::TierLabel(watch.roll.tier);
                if (!watch.labels.empty()) metadata+="  ·  "+watch.labels.front();
                Ui::PushID(static_cast<int>(i));
                if (row(item ? item->label : watch.roll.definition,metadata,watch.roll.definition,126+57*static_cast<float>(i-start))) {
                    tradeWatchSelection=watch;
                    tradeWatchStatus.clear();
                    tradeStatOffset=0;
                    tradeScreen=TradeScreen::WatchDetails;
                }
                Ui::PopID();
            }
            if (button("previous-watches","Previous",153,483,94,tradeWatchPage>0)) --tradeWatchPage;
            if (button("next-watches","Next",293,483,94,end<count)) ++tradeWatchPage;
            if (button("back-watches","Back",414,483,88)) tradeScreen=TradeScreen::Catalog;
        } else if (tradeScreen==TradeScreen::WatchDetails) {
            const auto& watch=tradeWatchSelection;
            const auto* item=FindBankDefinition(watch.roll.definition.c_str());
            itemHeader(item ? item->label : watch.roll.definition,watch.roll.definition,
                std::string(watch.roll.quality==7 ? "Mythic" : "Unique")+"  ·  "+TradeAuction::TierLabel(watch.roll.tier));
            std::vector<std::pair<std::string,std::string>> fields;
            if (watch.roll.defense) fields.emplace_back("Defense Rating",std::to_string(watch.roll.defense));
            const bool shield=item && item->slot==11;
            if (shield) {
                int block=0;
                for (const auto& effect:watch.roll.effects)
                    block+=TradeAuction::ShieldBlockEffect(effect,watch.roll.tier,watch.roll.quality);
                fields.emplace_back("Block",std::to_string(block)+"%");
            }
            for (const auto& entry:watch.labels) {
                const auto separator=entry.find(" · ");
                if (separator==std::string::npos) fields.emplace_back("",entry);
                else {
                    const auto name=entry.substr(0,separator);
                    const auto value=entry.substr(separator+4);
                    auto shown=shield && name=="Fixed Bonuses" ? TradeShieldFixedBonuses(value) : value;
                    if (!shown.empty()) fields.emplace_back(name,std::move(shown));
                }
            }
            if (std::count_if(fields.begin(),fields.end(),[](const auto& field){return field.first=="Bonuses";})>1) {
                unsigned bonus=0;
                for (auto& field:fields) if (field.first=="Bonuses") field.first="Bonus "+std::to_string(++bonus);
            }
            constexpr float top=152,bottom=448;
            auto pageEnd=[&](size_t start) {
                float y=top;
                size_t end=start;
                while (end<fields.size()) {
                    y+=formHeight(fields[end].second);
                    if (end!=start && y>bottom) break;
                    ++end;
                }
                return end;
            };
            const bool scrollable=pageEnd(0)<fields.size();
            if (scrollable) {
                tradeScrollArea=Rectangle(point(24,top),{478*scale.x,(bottom-top)*scale.y});
                if (Ui::IsMouseHoveringRect(point(24,top),point(502,bottom))) {
                    if (Ui::MouseWheel()>0 && tradeStatOffset) --tradeStatOffset;
                    if (Ui::MouseWheel()<0 && pageEnd(tradeStatOffset)<fields.size()) ++tradeStatOffset;
                }
                centered("Scroll for more stats",263,453);
            }
            float y=top;
            const auto end=pageEnd(tradeStatOffset);
            for (size_t i=tradeStatOffset;i<end;++i) {
                if (fields[i].first.empty()) {
                    cell(fields[i].second,26,y,474,30);
                    y+=30;
                } else {
                    y+=formValue(fields[i].first,fields[i].second,y,bottom);
                }
            }
            privateHelp="This watch only alerts you.";
            if (button("remove-watch","Untrack Me",24,483,152,tradeWatchList.Contains(watch.roll))) {
                tradeWatchStatus=tradeWatchList.Toggle(watch);
                if (!tradeWatchList.Contains(watch.roll)) {
                    const auto watches=tradeWatchList.Snapshot();
                    if (tradeWatchPage && (!watches || watches->size()<=tradeWatchPage*6)) --tradeWatchPage;
                    tradeScreen=TradeScreen::Watches;
                }
            }
            if (button("back-watch","Back",414,483,88)) tradeScreen=TradeScreen::Watches;
        } else {
            const bool request=tradeDraft.WantedOnly();
            const bool showingWanted=request || (tradeScreen==TradeScreen::Details && tradeShowingWanted);
            const auto& itemKey=showingWanted ? tradeDraft.wantedDefinition : tradeDraft.item.definition;
            const auto& itemName=showingWanted ? tradeDraft.wanted : tradeDraft.item.name;
            std::vector<std::string> wantedStats;
            if (showingWanted)
                for (const auto& option:tradeDraft.wantedOptions)
                    if (option.rfind("Roll tier: ",0)!=0) wantedStats.push_back(option);
            const auto& stats=showingWanted ? wantedStats : tradeDraft.item.stats;
            const auto tier=showingWanted ? TradeAuction::WantedTier(tradeDraft) : tradeDraft.item.rollTier;
            itemHeader(itemName,itemKey,TradeAuction::TierLabel(tier)+
                (tradeDraft.character.empty() ? "" : "  ·  Listed by "+tradeDraft.character));
            const size_t visibleStats=tradeScreen==TradeScreen::Editor ? 8 : 10;
            const float statsBottom=158+24*static_cast<float>(visibleStats);
            if (stats.size()>visibleStats) tradeScrollArea=Rectangle(point(26,161),{464*scale.x,(statsBottom-161)*scale.y});
            if (Ui::IsMouseHoveringRect(point(26,161),point(490,statsBottom))) {
                const float wheel=Ui::MouseWheel();
                if (wheel>0 && tradeStatOffset) --tradeStatOffset;
                if (wheel<0 && tradeStatOffset+visibleStats<stats.size()) ++tradeStatOffset;
            }
            draw->PushClipRect(point(26,158),point(494,statsBottom),true);
            const size_t end=std::min(tradeStatOffset+visibleStats,stats.size());
            for (size_t i=tradeStatOffset;i<end;++i) {
                const float y=158+24*static_cast<float>(i-tradeStatOffset);
                cell(stats[i],28,y,463,24);
            }
            draw->PopClipRect();
            if (stats.size()>visibleStats) centered("Scroll for more stats",263,statsBottom+4);

            if (tradeScreen==TradeScreen::Editor) {
                const bool wanted=!tradeDraft.wantedDefinition.empty() && !tradeDraft.wanted.empty();
                if (!request) {
                    if (wanted) {
                        cell("Looking for",26,394,134,34,GoldColor);
                        tradeIcons.Draw(draw,graphics,tradeDraft.wantedDefinition,point(171,396),{29*scale.x,30*scale.y});
                        cell(tradeDraft.wanted,208,394,291,34);
                    } else cell("Open to item offers.",26,394,474,34,MutedColor);
                }
                if (!tradeDraft.wantedSignature.empty()) centered("Exact drop alerts enabled.",263,450);
                if (button("save",tradeDraft.revision ? "Save Changes" : request ? "Post Request" : "Publish Offer",24,483,152,
                    (!request || wanted) && !busy && tradeClient->Configured())) {
                    tradeTyping=false;
                    if (!tradeDraft.revision) {
                        tradeRequest=request ? TradeAuction::Capture{TradeAuction::Capture::Kind::Character} : tradeOfferSource;
                        if (!request) tradeRequest.kind=TradeAuction::Capture::Kind::Confirm;
                        tradeCapturing=true;
                        tradeStatus="Checking the item…";
                    } else if (tradeClient->Request({},tradeDraft)) tradeStatus="Saving offer…";
                }
                if (button("cancel-edit","Back",414,483,88,!busy)) {
                    tradeTyping=false;
                    if (tradeDraft.revision) tradeScreen=TradeScreen::Details;
                    else if (request) tradeScreen=TradeScreen::Browse;
                    else TradePick();
                }
            } else {
                if (!request) {
                    cell(tradeDraft.wantedDefinition.empty() ? "Open to item offers." :
                        "Looking for "+tradeDraft.wanted,26,432,314,30,GoldColor);
                    if (!tradeDraft.wantedOptions.empty() && button("details-stats",tradeShowingWanted ? "Offered Stats" : "Wanted Stats",350,432,152)) {
                        tradeShowingWanted=!tradeShowingWanted;
                        tradeStatOffset=0;
                    }
                }
                if (tradeMine && !tradeDraft.closed) {
                    if (button("edit","Edit Offer",24,483,122,!busy)) {
                        tradeScreen=TradeScreen::Editor;
                    }
                    if (button("withdraw","Close Offer",158,483,135,!busy)) {
                        auto closed=tradeDraft;
                        closed.closed=true;
                        if (tradeClient->Request({},closed)) tradeStatus="Closing offer…";
                    }
                }
                if (button("back-details","Back",414,483,88,!busy)) tradeScreen=TradeScreen::Browse;
                if (!tradeMine && !tradeDraft.closed && button("counter-offer","Make Offer",24,483,142,!busy)) {
                    tradeWantedSample=tradeDraft.item;
                    tradeReplyItem=request ? tradeDraft.wantedDefinition : std::string{};
                    TradePick();
                }
                if (TradeAuction::CanTrack(tradeDraft)) {
                    if (tradeMine) track(tradeDraft,306,483,30);
                    else track(tradeDraft,178,483,30);
                }
            }
        }

        if (privateHelp) centered(tradeWatchStatus.empty() ? privateHelp : tradeWatchStatus,263,523);
        else if (tradeScreen!=TradeScreen::Catalog && tradeScreen!=TradeScreen::RollChoices) {
            if (!tradeClient->Configured()) centered("Trade Auction service is not configured.",263,523);
            else if (busy && tradeStatus.empty()) centered("Synchronizing offers…",263,523);
            else if (!tradeStatus.empty()) centered(tradeStatus,263,523);
        }
        tradeArea=Rectangle(origin,size);
    }
    Ui::End();
}
