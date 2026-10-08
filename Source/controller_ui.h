static void OpenControllerSettings() {
    draftControllerOptions=controllerOptions; controllerPage=0; controllerMessage.clear();
}

static bool SaveControllerSettings() {
    std::error_code error;
    std::filesystem::create_directories(controllerSettings.parent_path(),error);
    if (error) return false;
    auto pending=controllerSettings; pending+=L".pending";
    std::ofstream output(pending,std::ios::trunc);
    const bool written=draftControllerOptions.Write(output);
    output.close();
    return written && output.good() && MoveFileExW(pending.c_str(),controllerSettings.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}

static void DrawControllerSettings(UiPoint origin,UiPoint scale) {
    const auto point=[origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw=Ui::GetWindowDrawList();
    Heading(draw,"Controller",point(34,20),scale,280);
    constexpr const char* pages[]={"Controls","Skills","Layout"};
    const int pageStep=OptionRow("Settings:",pages[controllerPage],point(10,58),scale,"Okay saves every page. Back discards changes.",point(355,58),origin.x,nullptr,true);
    if (pageStep) controllerPage=unsigned((int(controllerPage)+pageStep+3)%3);
    auto toggle=[&](const char* name,bool& value,float y,const char* description) {
        if (OptionRow(name,value ? "On" : "Off",point(10,y),scale,description,point(355,y),origin.x,nullptr,true)) value=!value;
    };
    auto number=[&](const char* name,unsigned& value,float y,unsigned minimum,unsigned maximum,unsigned increment,const char* description) {
        const auto text=std::to_string(value)+"%";
        const int step=OptionRow(name,text.c_str(),point(10,y),scale,description,point(355,y),origin.x,nullptr,true);
        if (step) value=unsigned(std::clamp(int(value)+step*int(increment),int(minimum),int(maximum)));
    };
    if (!controllerPage) {
        toggle("Controller:",draftControllerOptions.enabled,96,"Enables Xbox-compatible controllers and Steam Input gamepads. Mouse and keyboard remain available.");
        toggle("Button hints:",draftControllerOptions.hints,134,"Shows the active combat or cursor bindings above the skill bar.");
        number("Stick deadzone:",draftControllerOptions.deadzone,172,10,40,5,"Increase this if the character or cursor moves while the sticks are released.");
        number("Cursor speed:",draftControllerOptions.speed,210,25,200,25,"Both sticks move the cursor in menus. Hold LB for precision.");
        number("Trigger threshold:",draftControllerOptions.trigger,248,10,80,5,"How far a trigger must be pressed to attack or click.");
        toggle("Invert aim Y:",draftControllerOptions.invertY,286,"Reverses vertical movement of the right stick.");
        MeterText(draw,"Steam Deck: Gamepad template in Steam Input.\nSTEAM + X: keyboard.",point(34,332),scale,BodyColor,282*scale.x);
    } else if (controllerPage==1) {
        constexpr const char* labels[]={"A:","B:","X:","Y:","LB + A:","LB + B:","LB + X:","LB + Y:"};
        for (unsigned i=0;i<8;++i) {
            auto& slot=draftControllerOptions.slots[i];
            const float y=96+32.f*i;
            const auto text="Skill "+std::to_string(slot);
            const int step=OptionRow(labels[i],text.c_str(),point(10,y),scale,"Activates a hotbar slot once per press. Uses the current game key binding; assign a key without Ctrl or Shift.",point(355,y),origin.x,nullptr,true);
            if (step) slot=unsigned((int(slot)-1+step+8)%8)+1;
        }
        MeterText(draw,"Move: left stick. Aim: right stick.\nRT: left attack. LT: right attack.",point(34,362),scale,BodyColor,282*scale.x);
    } else {
        MeterText(draw,
            "Left stick: move\nRight stick: aim / cursor\nRT / LT: left / right mouse\nA B X Y: skills 1-4\nHold LB: skills 5-8\nD-pad left / right: health / mana\nD-pad down: scroll; up: map\nLB + D-pad: turn / zoom camera\nRB: show loot; L3: reset camera\nView / Back: inventory\nMenu / Start: Escape\nR3: toggle cursor mode\n\nMenus: A click, X right click,\nB back, Y Enter, L3 Tab.\nD-pad up / down: scroll.",
            point(34,96),UiPoint(scale.x*.93f,scale.y*.93f),BodyColor,282*scale.x);
    }
    const char* message=controllerMessage.empty() ? controllerStatus.message : controllerMessage.c_str();
    draw->PushClipRect(point(24,412),point(326,446));
    MeterText(draw,message,point(24,412),scale,GoldColor,302*scale.x);
    draw->PopClipRect();
    if (HoverArea(point(24,412),UiPoint(302*scale.x,34*scale.y))) QueueHelp(message,point(355,412),scale,origin.x);
    if (DrawSkinControl("Okay",point(24,452),scale)) {
        if (SaveControllerSettings()) { MeterControllerRelease(); controllerOptions=draftControllerOptions; activeAddon=nullptr; }
        else controllerMessage="Settings could not be saved.";
    }
    if (DrawSkinControl("Back",point(185,452),scale)) activeAddon=nullptr;
}

static void DrawControllerHints() {
    if (!addonRegistry.ControllerEnabled() || !controllerOptions.enabled || !controllerOptions.hints || !controllerStatus.connected ||
        !gameWindow || GetForegroundWindow()!=gameWindow || addonsOpen || leaderboardOpen) return;
    const auto display=Ui::GetIO().DisplaySize;
    if (display.x<320 || display.y<200) return;
    const float factor=std::min({std::max(GameScale().y,1.5f),display.x/680.f,display.y/480.f});
    const UiPoint scale(factor,factor),size(644*factor,47*factor);
    const UiPoint origin((display.x-size.x)*.5f,display.y-128*factor);
    Ui::SetNextWindowPos(origin); Ui::SetNextWindowSize(size);
    if (Ui::Begin("##ControllerHints",nullptr,Ui::NoInputs|Ui::NoSavedSettings)) {
        auto* draw=Ui::GetWindowDrawList();
        draw->AddRectFilled(origin,UiPoint(origin.x+size.x,origin.y+size.y),UI_COLOR(8,7,4,195));
        char text[192]{};
        if (controllerStatus.waiting) std::snprintf(text,sizeof(text),"%s",controllerStatus.message);
        else if (controllerStatus.pointer) std::snprintf(text,sizeof(text),"CURSOR   A Click   X Right click   B Back   Y Enter   LB Precision   R3 Combat");
        else {
            const unsigned offset=controllerStatus.layer ? 4 : 0;
            const auto& slots=controllerOptions.slots;
            std::snprintf(text,sizeof(text),"%sA %u   B %u   X %u   Y %u     RT / LT Attack     R3 Cursor",
                controllerStatus.layer ? "LB + " : "",slots[offset],slots[offset+1],slots[offset+2],slots[offset+3]);
        }
        MeterText(draw,text,At(origin,scale,10,6),scale,GoldColor,624*factor);
        MeterText(draw,controllerStatus.pointer ? "Sticks: cursor     D-pad: scroll     View: inventory     Menu: Escape" :
            "D-pad: health / mana / scroll / map    LB: skills 5-8    View: bag    Menu: Esc",
            At(origin,scale,10,27),UiPoint(factor*.9f,factor*.9f),BodyColor,624*factor);
    }
    Ui::End();
}
