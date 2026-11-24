static MythicCollection::Book collectionBook;
static bool collectionSaveAvailable=true;
static size_t collectionPage=0;
static unsigned collectionFilter=0;

static void OpenMythicCollection() {
    collectionPage=0;
    collectionSaveAvailable=collectionBook.Sync();
}

static void DrawCollectionBody(UiPoint origin,UiPoint size) {
    struct Row {
        const BankCatalogEntry* item;
        std::optional<unsigned> tier;
    };
    static uint64_t displayedRevision=UINT64_MAX;
    static std::vector<Row> rows[3];
    if (displayedRevision!=collectionBook.Revision()) {
        for (auto& list:rows) list.clear();
        for (const auto& item:TradeAuction::SortedCatalog()) if (item.rarity=="Mythic") {
            Row row{item.entry,MythicCollection::CollectedTier(item,collectionBook.Entries())};
            rows[0].push_back(row);
            rows[row.tier ? 1 : 2].push_back(row);
        }
        displayedRevision=collectionBook.Revision();
    }
    constexpr float width=480,height=470,inset=14,rowWidth=width-2*inset;
    const UiPoint scale(size.x/width,size.y/height);
    const float unit=std::min(scale.x,scale.y);
    const UiPoint skinScale(unit,unit);
    auto point=[origin,scale](float x,float y){return At(origin,scale,x,y);};
    auto* draw=Ui::GetWindowDrawList();
    const float textSize=valueFont->SizeForEm(17*unit);
    constexpr auto collectedColor=UI_COLOR(171,219,153,255);
    auto centered=[&](const std::string& text,float x,float y,float w,float h,UiColor color) {
        const auto fitted=FitTextLine(*valueFont,textSize,text.c_str(),(w-4)*scale.x);
        const auto extent=valueFont->CalcTextSizeA(textSize,FLT_MAX,0,fitted.c_str());
        const auto middle=point(x+w*.5f,y+h*.5f);
        draw->AddText(valueFont,textSize,{std::round(middle.x-extent.x*.5f),std::round(middle.y-extent.y*.5f)},color,fitted.c_str());
    };
    auto button=[&](const char* id,const char* label,float x,float y,float w,bool enabled,bool selected=false) {
        const auto at=point(x,y);
        const UiPoint buttonSize(w*scale.x,30*scale.y);
        Ui::BeginDisabled(!enabled);
        Ui::SetCursorScreenPos(at);
        const bool clicked=Ui::InvisibleButton(id,buttonSize);
        const bool hovered=Ui::IsItemHovered();
        nativeSkin.CompactButton(draw,at,buttonSize,skinScale,hovered,Ui::IsItemActive() || selected,"");
        const auto color=!enabled ? UI_COLOR(174,132,69,255) : hovered ? UI_COLOR(255,215,76,255) : GoldColor;
        nativeSkin.AlignedText(draw,label,at,buttonSize,skinScale,true,color);
        Ui::EndDisabled();
        if (!enabled) draw->AddRectFilled(at,point(x+w,y+30),UI_COLOR(12,9,7,68));
        return clicked && enabled;
    };

    const auto progress=std::to_string(rows[1].size())+" / "+std::to_string(rows[0].size())+" Collected";
    centered(progress,inset,12,rowWidth,26,GoldColor);
    constexpr const char* filters[]={"All","Collected","Missing"};
    constexpr float filterGap=7,filterWidth=(rowWidth-2*filterGap)/3;
    for (unsigned i=0;i<std::size(filters);++i) {
        const float x=inset+(filterWidth+filterGap)*i;
        if (button(filters[i],filters[i],x,46,filterWidth,true,i==collectionFilter)) {
            collectionFilter=i;
            collectionPage=0;
        }
        if (i==collectionFilter)
            draw->AddLine(point(x+filterWidth*.32f,79),point(x+filterWidth*.68f,79),UI_COLOR(202,151,50,255),std::max(1.0f,unit));
    }
    constexpr size_t perPage=6;
    constexpr float listTop=94,rowStep=52,rowHeight=45,nameX=62,nameWidth=275,statusX=340,statusWidth=123;
    const auto& list=rows[collectionFilter];
    const size_t pages=std::max<size_t>(1,(list.size()+perPage-1)/perPage);
    collectionPage=std::min(collectionPage,pages-1);
    if (list.empty()) {
        const char* message=collectionFilter==1 ? "No Mythic items collected yet." :
            collectionFilter==2 ? "Collection complete." : "The Mythic catalog is unavailable.";
        centered(message,inset,listTop,rowWidth,perPage*rowStep,BodyColor);
    }
    for (size_t i=0;i<perPage && collectionPage*perPage+i<list.size();++i) {
        const auto& row=list[collectionPage*perPage+i];
        const float top=listTop+rowStep*i;
        nativeSkin.Row(draw,point(inset,top),{rowWidth*scale.x,rowHeight*scale.y},skinScale,0,false);
        const float iconSize=32*unit;
        const auto iconCenter=point(37,top+rowHeight*.5f);
        tradeIcons.Draw(draw,graphics,row.item->key,{iconCenter.x-iconSize*.5f,iconCenter.y-iconSize*.5f},{iconSize,iconSize});
        const float availableWidth=nameWidth*scale.x;
        const float naturalWidth=valueFont->CalcTextSizeA(textSize,FLT_MAX,0,row.item->label).x;
        const float nameSize=naturalWidth>availableWidth ? textSize*std::max(.90f,availableWidth/naturalWidth) : textSize;
        const auto title=FitTextLine(*valueFont,nameSize,row.item->label,availableWidth);
        const auto extent=valueFont->CalcTextSizeA(nameSize,FLT_MAX,0,title.c_str());
        const auto center=point(nameX,top+rowHeight*.5f);
        draw->AddText(valueFont,nameSize,{std::round(center.x),std::round(center.y-extent.y*.5f)},
            row.tier ? GoldColor : BodyColor,title.c_str());
        if (title!=row.item->label && HoverArea(point(nameX,top),{nameWidth*scale.x,rowHeight*scale.y}))
            QueueButtonHelp(row.item->label,point(nameX,top),{nameWidth*scale.x,rowHeight*scale.y},skinScale);
        const std::string status=!row.tier ? "Missing" : *row.tier ? "Collected - T"+std::to_string(*row.tier) : "Collected";
        centered(status,statusX,top,statusWidth,rowHeight,row.tier ? collectedColor : MutedColor);
    }
    constexpr float footerY=421,pageButtonWidth=114;
    if (button("previous","Previous",inset,footerY,pageButtonWidth,collectionPage>0)) --collectionPage;
    if (button("next","Next",width-inset-pageButtonWidth,footerY,pageButtonWidth,collectionPage+1<pages)) ++collectionPage;
    centered(TradeAuction::PageLabel(collectionPage+1,pages),inset+pageButtonWidth,footerY,rowWidth-2*pageButtonWidth,30,BodyColor);
    if (!collectionSaveAvailable) centered("Could not save collection.",inset,451,rowWidth,17,GoldColor);
}

static bool QuestCollectionVisible() {
    return mythicCollectionEnabled && !addonsOpen && !tradeOpen && !reportOpen && !leaderboardOpen &&
        !moveEditing && nativeSkin.Ready() && valueFont && questLogFrame.panel[2]>0;
}

static void DrawQuestCollection() {
    collectionLogArea=questArrows[0]=questArrows[1]={};
    if (!QuestCollectionVisible()) return;
    const auto display=Ui::GetIO().DisplaySize;
    if (display.x<=0 || display.y<=0) return;
    const auto& frame=questLogFrame;
    const UiPoint panel(frame.panel[0]*display.x,frame.panel[1]*display.y);
    const UiPoint size(frame.panel[2]*display.x,frame.panel[3]*display.y);
    const UiPoint title(frame.title[0]*display.x,frame.title[1]*display.y);
    const UiPoint titleSize(frame.title[2]*display.x,frame.title[3]*display.y);
    if (titleSize.y<=0 || titleSize.x<=0 || size.x<=0 || size.y<=0) return;
    const UiPoint middle(title.x+titleSize.x*.5f,title.y+titleSize.y*.5f);
    const float gap=titleSize.x*.40f;
    const float arrowHit=titleSize.y*.8f;
    const UiPoint arrowSize(arrowHit,arrowHit);
    const UiPoint arrowCenters[]={{middle.x-gap,middle.y},{middle.x+gap,middle.y}};
    Ui::SetNextWindowPos(panel);
    Ui::SetNextWindowSize(size);
    if (Ui::Begin("##QuestCollection",nullptr,SurfaceFlags|Ui::NoSavedSettings)) {
        auto* draw=Ui::GetWindowDrawList();
        if (collectionLogOpen) {
            const float horizontalInset=size.x*.041f;
            const float top=std::max(title.y+titleSize.y,(frame.close[1]+frame.close[3])*display.y)+size.y*.016f;
            const UiPoint body(panel.x+horizontalInset,top);
            const UiPoint bodySize(size.x-2*horizontalInset,panel.y+size.y*.967f-top);
            if (bodySize.x>0 && bodySize.y>0) {
                const UiPoint end(body.x+bodySize.x,body.y+bodySize.y);
                const float border=std::min(bodySize.x/460,bodySize.y/470)*.67f;
                draw->AddRectFilled(body,end,UI_COLOR(26,21,16,255));
                nativeSkin.Frame(draw,body,bodySize,{border,border});
                collectionLogArea=Rectangle(body,bodySize);
                draw->PushClipRect(body,end);
                DrawCollectionBody(body,bodySize);
                draw->PopClipRect();
            }
        }
        const float arrowEdge=titleSize.y*.43f;
        const UiPoint arrowScale(arrowEdge/20,arrowEdge/20);
        for (unsigned i=0;i<2;++i) {
            const auto center=arrowCenters[i];
            const UiPoint at(center.x-arrowHit*.5f,center.y-arrowHit*.5f);
            Ui::PushID(static_cast<int>(i));
            Ui::SetCursorScreenPos(at);
            if (Ui::InvisibleButton("switch-log",arrowSize)) {
                collectionLogOpen=!collectionLogOpen;
                if (collectionLogOpen) OpenMythicCollection();
            }
            const UiPoint arrow(center.x-arrowEdge*.5f,center.y-arrowEdge*.5f);
            nativeSkin.Arrow(draw,arrow,arrowScale,i!=0,Ui::IsItemActive());
            questArrows[i]=Rectangle(at,arrowSize);
            Ui::PopID();
        }
    }
    Ui::End();
}
