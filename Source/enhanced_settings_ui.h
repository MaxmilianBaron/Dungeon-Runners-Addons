#pragma once

static void OpenEnhancedSettings() {
    draftEnhancedOptions = {lowHpOptions,unbindOptions.enabled,mouseLookEnabled};
    enhancedMessage.clear();
    lowHpPreviewUntil = 0;
}

static void DrawEnhancedSettings(UiPoint origin,UiPoint scale) {
    const auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw = Ui::GetWindowDrawList();
    Heading(draw,"Enhanced Settings",point(34,20),scale,280);
    if (OptionRow("Mouse look:",draftEnhancedOptions.mouseLook ? "On" : "Off",point(10,58),scale,
        mouseLookAvailable ? "Hold the right mouse button over the world to look around. Right-click skills are replaced by camera control; inventory and menu clicks stay unchanged." : "Unavailable: client camera code differs. No camera changes were applied.",point(355,58),origin.x) && mouseLookAvailable)
        draftEnhancedOptions.mouseLook = !draftEnhancedOptions.mouseLook;
    if (OptionRow("Unbind left auto:",draftEnhancedOptions.unbindLeft ? "On" : "Off",point(10,96),scale,
        unbindAvailable ? "Stops fallback weapon attacks from left click, including an empty skill slot, cooldowns or insufficient mana. Movement and the assigned skill remain available." : "Unavailable: client input code differs. No input changes were applied.",point(355,96),origin.x) && unbindAvailable)
        draftEnhancedOptions.unbindLeft = !draftEnhancedOptions.unbindLeft;
    auto& health = draftEnhancedOptions.health;
    if (OptionRow("Low HP warning:",health.enabled ? "On" : "Off",point(10,134),scale,"Red screen edges while your character is alive and below the selected HP percentage.",point(355,134),origin.x)) health.enabled = !health.enabled;
    const auto threshold = std::to_string(health.threshold) + "%";
    const int limit = OptionRow("Below HP:",threshold.c_str(),point(10,172),scale,"The warning clears when health reaches this percentage of maximum HP.",point(355,172),origin.x);
    if (limit) health.threshold = unsigned(std::clamp(int(health.threshold) + limit,1,99));
    const auto intensity = std::to_string(health.intensity) + "%";
    const int opacity = OptionRow("Intensity:",intensity.c_str(),point(10,210),scale,"Opacity at the screen edges. The center stays clear; the warning does not flash.",point(355,210),origin.x);
    if (opacity) health.intensity = unsigned(std::clamp(int(health.intensity) + opacity * 5,10,90));
    BodyText(draw,"Each option works independently. Mouse look uses the game's camera controls. Preview shows the HP warning for 5 seconds.",point(34,258),UiPoint(scale.x*.85f,scale.y*.85f),BodyColor,280 * scale.x);
    if (DrawSkinControl("Preview",point(104,324),scale)) lowHpPreviewUntil = GetTickCount64() + 5000;
    if (!enhancedMessage.empty()) BodyText(draw,enhancedMessage.c_str(),point(34,371),UiPoint(scale.x*.85f,scale.y*.85f),GoldColor,282 * scale.x);
    if (DrawSkinControl("Okay",point(24,400),scale,&okayRect)) {
        if (SaveAddonOptions(enhancedSettings,draftEnhancedOptions)) {
            MeterMouseLookRelease();
            lowHpOptions = draftEnhancedOptions.health;
            unbindOptions.enabled = draftEnhancedOptions.unbindLeft;
            mouseLookEnabled = draftEnhancedOptions.mouseLook;
            lowHpPreviewUntil = 0;
            activeAddon = nullptr;
        } else enhancedMessage = "Settings could not be saved.";
    }
    if (DrawSkinControl("Back",point(185,400),scale,&cancelRect)) { lowHpPreviewUntil = 0; activeAddon = nullptr; }
}

static void DrawLowHpWarning(uint64_t now) {
    if (!addonRegistry.LowHp() || !worldVisible || !gameWindow || GetForegroundWindow() != gameWindow) return;
    const bool preview = activeAddon && activeAddon->open == OpenEnhancedSettings && now < lowHpPreviewUntil;
    if (preview || healthFrame.Warning(lowHpOptions,now)) healthVignette.Draw(graphics,Ui::GetWindowDrawList(),Ui::GetIO().DisplaySize,preview ? draftEnhancedOptions.health.intensity : lowHpOptions.intensity);
}
