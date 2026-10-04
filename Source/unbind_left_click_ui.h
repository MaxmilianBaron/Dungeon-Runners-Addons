#pragma once

static void OpenUnbindSettings() { draftUnbindOptions = unbindOptions; unbindMessage.clear(); }

static void DrawUnbindSettings(UiPoint origin,UiPoint scale) {
    const auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw = Ui::GetWindowDrawList();
    Heading(draw,"Unbind Left Click",point(34,20),scale,280);
    if (OptionRow("Unbind left auto:",draftUnbindOptions.enabled ? "On" : "Off",point(10,58),scale,"Stops fallback weapon attacks from left click, including an empty skill slot, cooldowns or insufficient mana.",point(355,58),origin.x) && unbindAvailable) draftUnbindOptions.enabled = !draftUnbindOptions.enabled;
    BodyText(draw,"Left click still moves, selects targets and uses its assigned skill. Right click keeps its normal behavior.",point(34,105),scale,BodyColor,280 * scale.x);
    const char* message = unbindAvailable ? unbindMessage.c_str() : "Unavailable: client input code differs. No input changes were applied.";
    if (*message) BodyText(draw,message,point(34,177),UiPoint(scale.x*.85f,scale.y*.85f),GoldColor,282 * scale.x);
    if (DrawSkinControl("Okay",point(24,232),scale,&okayRect)) {
        if (SaveAddonOptions(unbindSettings,draftUnbindOptions)) { unbindOptions = draftUnbindOptions; activeAddon = nullptr; }
        else unbindMessage = "Settings could not be saved.";
    }
    if (DrawSkinControl("Back",point(185,232),scale,&cancelRect)) activeAddon = nullptr;
}
