#pragma once

static void OpenLowHpSettings() { draftLowHpOptions = lowHpOptions; lowHpMessage.clear(); lowHpPreviewUntil = 0; }

static void DrawLowHpSettings(UiPoint origin,UiPoint scale) {
    const auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw = Ui::GetWindowDrawList();
    Heading(draw,"Low HP Warning",point(34,20),scale,280);
    if (OptionRow("Warning:",draftLowHpOptions.enabled ? "On" : "Off",point(10,58),scale,"Red screen edges while your character is alive and below the selected HP percentage.",point(355,58),origin.x)) draftLowHpOptions.enabled = !draftLowHpOptions.enabled;
    const auto threshold = std::to_string(draftLowHpOptions.threshold) + "%";
    const int limit = OptionRow("Below HP:",threshold.c_str(),point(10,96),scale,"Percentage of maximum health. The warning clears when health reaches this value.",point(355,96),origin.x);
    if (limit) draftLowHpOptions.threshold = unsigned(std::clamp(int(draftLowHpOptions.threshold) + limit,1,99));
    const auto intensity = std::to_string(draftLowHpOptions.intensity) + "%";
    const int opacity = OptionRow("Intensity:",intensity.c_str(),point(10,134),scale,"Opacity at the screen edges. The center stays clear; the warning does not flash.",point(355,134),origin.x);
    if (opacity) draftLowHpOptions.intensity = unsigned(std::clamp(int(draftLowHpOptions.intensity) + opacity * 5,10,90));
    BodyText(draw,"Works independently of Damage Meter. Preview shows the selected intensity for 5 seconds.",point(34,180),UiPoint(scale.x*.85f,scale.y*.85f),BodyColor,280 * scale.x);
    if (DrawSkinControl("Preview",point(104,224),scale)) lowHpPreviewUntil = GetTickCount64() + 5000;
    if (!lowHpMessage.empty()) BodyText(draw,lowHpMessage.c_str(),point(34,265),scale,GoldColor,282 * scale.x);
    if (DrawSkinControl("Okay",point(24,290),scale,&okayRect)) {
        if (SaveAddonOptions(lowHpSettings,draftLowHpOptions)) { lowHpOptions = draftLowHpOptions; lowHpPreviewUntil = 0; activeAddon = nullptr; }
        else lowHpMessage = "Settings could not be saved.";
    }
    if (DrawSkinControl("Back",point(185,290),scale,&cancelRect)) { lowHpPreviewUntil = 0; activeAddon = nullptr; }
}

static void DrawLowHpWarning(uint64_t now) {
    if (!addonRegistry.LowHp() || !worldVisible || !gameWindow || GetForegroundWindow() != gameWindow) return;
    const bool preview = activeAddon && activeAddon->open == OpenLowHpSettings && now < lowHpPreviewUntil;
    if (preview || healthFrame.Warning(lowHpOptions,now)) healthVignette.Draw(graphics,Ui::GetWindowDrawList(),Ui::GetIO().DisplaySize,preview ? draftLowHpOptions.intensity : lowHpOptions.intensity);
}
