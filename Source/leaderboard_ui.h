#pragma once
#include "windows_compat.h"

static Leaderboard::Feed LeaderboardFeed() {
    return Leaderboard::SelectedFeed(leaderboardCategory,leaderboardWeek);
}
static std::string LeaderboardDate(uint64_t unixTime) {
    const uint64_t ticks=(unixTime+11644473600ULL)*10000000ULL;
    const FILETIME file{static_cast<DWORD>(ticks),static_cast<DWORD>(ticks>>32)};
    SYSTEMTIME date{};
    if (!FileTimeToSystemTime(&file,&date)) return "--";
    char text[32]{};
    std::snprintf(text,sizeof(text),"%04u-%02u-%02u %02u:%02u UTC",date.wYear,date.wMonth,date.wDay,date.wHour,date.wMinute);
    return text;
}
static void OpenLeaderboard() {
    addonsOpen=false; activeAddon=nullptr; leaderboardOpen=true;
    hotkeyInput.Reset(); dragging=false;
    if (context) Ui::ClearInput();
}
static void DrawLeaderboard() {
    if (!leaderboardOpen || !leaderboardClient || !addonRegistry.Leaderboard() || !nativeSkin.Ready()) { leaderboardArea={}; return; }
    using namespace Leaderboard;
    const auto category=leaderboardCategory;
    const auto feed=LeaderboardFeed();
    leaderboardClient->Request(feed);
    const Result result=leaderboardClient->Read(feed);
    if (leaderboardBoard!=result.board || leaderboardViewCategory!=category) {
        if (leaderboardViewCategory!=category) leaderboardPage=0;
        leaderboardBoard=result.board; leaderboardViewCategory=category;
        leaderboardRows=result.board ? View(*result.board,category) : std::vector<Row>{};
    }
    const auto gameScale=GameScale();
    const auto scale=FitScale({std::max(1.0f,gameScale.x),std::max(1.0f,gameScale.y)},{660,534});
    const UiPoint size(660*scale.x,534*scale.y),display=Ui::GetIO().DisplaySize;
    const UiPoint origin(std::floor((display.x-size.x)/2),std::floor((display.y-size.y)/2));
    const auto point=[&](float x,float y) { return At(origin,scale,x,y); };
    Ui::SetNextWindowPos(origin); Ui::SetNextWindowSize(size);
    if (Ui::Begin("##Leaderboard",nullptr,SurfaceFlags|Ui::NoSavedSettings)) {
        auto* draw=Ui::GetWindowDrawList();
        nativeSkin.Frame(draw,origin,size,scale);
        Heading(draw,"Leaderboard",point(24,18),scale,480);
        if (SkinControl("leaderboard-close","Close",point(550,17),{86*scale.x,30*scale.y},scale,&leaderboardCloseRect)) { leaderboardOpen=false; Ui::ClearInput(); }
        BodyText(draw,"Dungeon Runners Reborn  /  Official top 100",point(24,50),scale,MutedColor);
        constexpr const char* labels[]={"Level 100","Gold","Time","Rating"};
        static_assert(sizeof(labels)/sizeof(labels[0])==static_cast<size_t>(Category::Count),"Leaderboard category labels must match.");
        for (unsigned i=0;i<static_cast<unsigned>(Category::Count);++i) {
            Ui::PushID(static_cast<int>(i));
            if (SkinControl("category",labels[i],point(24+156.0f*i,77),{144*scale.x,31*scale.y},scale,&leaderboardTabRects[i])) {
                leaderboardCategory=static_cast<Category>(i); leaderboardPage=0;
            }
            if (static_cast<unsigned>(category)==i) draw->AddRectFilled(point(31+156.0f*i,109),point(161+156.0f*i,111),GoldColor);
            Ui::PopID();
        }
        const bool pvp=category==Category::Rating;
        leaderboardPeriodRects={};
        if (pvp) {
            constexpr const char* periods[]={"All time","This week"};
            for (unsigned i=0;i<2;++i) {
                Ui::PushID(static_cast<int>(i));
                if (SkinControl("period",periods[i],point(24+118.0f*i,119),{110*scale.x,29*scale.y},scale,&leaderboardPeriodRects[i])) { leaderboardWeek=i!=0; leaderboardPage=0; }
                if (leaderboardWeek==(i!=0)) draw->AddRectFilled(point(31+118.0f*i,149),point(127+118.0f*i,151),GoldColor);
                Ui::PopID();
            }
            BodyText(draw,"Official PvP rating",point(268,125),scale,MutedColor);
        } else BodyText(draw,category==Category::Level ? "First to reach level 100, in official order" : category==Category::Gold ? "Total gold held by each character" : "Total recorded play time",point(24,125),scale,MutedColor);
        const uint64_t now=WindowsCompat::Milliseconds();
        Ui::BeginDisabled(result.loading || now<result.nextAttempt);
        if (SkinControl("refresh",result.loading ? "Loading" : "Refresh",point(550,119),{86*scale.x,29*scale.y},scale,&leaderboardRefreshRect)) leaderboardClient->Request(feed,true);
        Ui::EndDisabled();
        if (HoverArea(point(550,119),{86*scale.x,29*scale.y})) {
            const auto tip=result.loading ? std::string("Loading from the official service in the background.") : now<result.nextAttempt ? "Refresh available in "+std::to_string((result.nextAttempt-now+999)/1000)+" seconds." : "Refresh this category. Open categories update every 60 seconds.";
            QueueHelp(tip.c_str(),point(667,119),scale,origin.x);
        }
        draw->AddRectFilled(point(24,159),point(636,185),UI_COLOR(42,33,19,240));
        MeterText(draw,"#",point(34,163),scale,GoldColor);
        MeterText(draw,"Player",point(75,163),scale,GoldColor);
        MeterText(draw,"Class",point(pvp ? 365.0f : 302.0f,163),scale,GoldColor);
        if (!pvp) MeterText(draw,"Level",point(389,163),scale,GoldColor);
        const char* column=category==Category::Level ? "Reached level 100 (UTC)" : category==Category::Gold ? "Gold" : category==Category::Played ? "Time played" : "Rating";
        RightMeterText(draw,column,point(626,163),scale,GoldColor,186*scale.x);
        constexpr size_t pageSize=10;
        const size_t pages=std::max<size_t>(1,(leaderboardRows.size()+pageSize-1)/pageSize);
        leaderboardPage=std::min(leaderboardPage,pages-1);
        for (size_t i=0;i<pageSize;++i) {
            const float y=188+24.0f*static_cast<float>(i);
            const size_t index=leaderboardPage*pageSize+i;
            if (index>=leaderboardRows.size()) continue;
            if (!(i%2)) draw->AddRectFilled(point(24,y),point(636,y+24),UI_COLOR(0,0,0,100));
            const auto& row=leaderboardRows[index];
            const auto name=MeterName(row.name.c_str(),scale,(pvp ? 278.0f : 216.0f)*scale.x);
            const auto className=MeterName(row.characterClass.empty() ? "--" : row.characterClass.c_str(),scale,(pvp ? 89.0f : 77.0f)*scale.x);
            const auto value=Metric(row,category);
            const auto formatted=pvp ? (row.rating ? Rating(*row.rating) : "--") : !value ? "--" : category==Category::Level ? LeaderboardDate(*value).substr(0,16) : category==Category::Played ? Duration(*value) : Number(*value);
            MeterText(draw,row.rank ? std::to_string(row.rank).c_str() : "--",point(34,y+4),scale,row.rank && row.rank<=3 ? GoldColor : MutedColor);
            MeterText(draw,name.c_str(),point(75,y+4),scale);
            MeterText(draw,className.c_str(),point(pvp ? 365.0f : 302.0f,y+4),scale,MutedColor);
            if (!pvp) RightMeterText(draw,row.level ? std::to_string(row.level).c_str() : "--",point(421,y+4),scale,MutedColor);
            RightMeterText(draw,formatted.c_str(),point(626,y+4),scale,BodyColor,(pvp ? 155.0f : 194.0f)*scale.x);
            if (HoverArea(point(24,y),{612*scale.x,24*scale.y})) {
                const auto tip=row.name+"\n"+column+": "+formatted+(pvp ? leaderboardWeek ? "\nThis week" : "\nAll time" : "");
                QueueHelp(tip.c_str(),point(667,y),scale,origin.x);
            }
        }
        if (leaderboardRows.empty()) {
            const char* message=result.loading ? "Loading official standings..." : !result.board ? "Standings are unavailable. Refresh will retry automatically." : pvp ? "No rated PvP results have been published yet." : "No level 100 characters in the official top 100.";
            BodyText(draw,message,point(45,249),scale,BodyColor,565*scale.x);
        }
        Ui::BeginDisabled(leaderboardPage==0);
        if (SkinControl("previous","Previous",point(24,439),{98*scale.x,29*scale.y},scale,&leaderboardPreviousRect)) --leaderboardPage;
        Ui::EndDisabled();
        const auto pagination="Page "+std::to_string(leaderboardPage+1)+" / "+std::to_string(pages)+"   |   "+std::to_string(leaderboardRows.size())+" players";
        BodyText(draw,pagination.c_str(),point(227,446),scale,MutedColor);
        Ui::BeginDisabled(leaderboardPage+1>=pages);
        if (SkinControl("next","Next",point(538,439),{98*scale.x,29*scale.y},scale,&leaderboardNextRect)) ++leaderboardPage;
        Ui::EndDisabled();
        std::string status;
        if (!result.error.empty()) status=result.error;
        else if (result.board) status=(result.board->stale ? "Cached by the official service  |  " : "Updated ")+LeaderboardDate(result.board->generated);
        else status="Source: play.dungeonrunnersreborn.com";
        if (result.board && !result.error.empty()) status+=" Last update: "+LeaderboardDate(result.board->generated);
        BodyText(draw,status.c_str(),point(24,479),scale,result.error.empty() ? MutedColor : GoldColor,610*scale.x);
        leaderboardArea=Rectangle(origin,size); RegisterHitArea();
    }
    Ui::End();
}
