#pragma once
#include "windows_compat.h"

struct EnhancedToggleDraft { int original, value; };
static std::vector<EnhancedToggleDraft> enhancedToggleDraft;

static float EnhancedExtraHeight() {
    return 38.0f*static_cast<float>(addonRegistry.EnhancedToggles().size()+addonRegistry.TradeAuction()+addonRegistry.SallySam()+addonRegistry.MythicCollection());
}

static void OpenEnhancedSettings() {
    draftEnhancedOptions = {lowHpOptions,unbindOptions.enabled,mouseLookEnabled,enhancedZoomEnabled,chatTimestampsEnabled,tradeEnabled,sallySamEnabled,mythicCollectionEnabled};
    enhancedMessage.clear();
    lowHpPreviewUntil = 0;
    enhancedToggleDraft.clear();
    for (const auto& toggle:addonRegistry.EnhancedToggles()) {
        const auto& extension=addonRegistry.Extensions()[toggle.extension];
        const int value=AddonRegistry::Read(extension,toggle.option);
        enhancedToggleDraft.push_back({value,value});
    }
}

static bool ApplyEnhancedToggles() {
    const auto& toggles=addonRegistry.EnhancedToggles();
    for (size_t i=0;i<toggles.size();++i) {
        auto& draft=enhancedToggleDraft[i];
        if (draft.value==draft.original) continue;
        const auto& toggle=toggles[i];
        const auto& extension=addonRegistry.Extensions()[toggle.extension];
        std::array<int,4> values{};
        for (unsigned index=0;index<extension.optionCount;++index) {
            values[index]=index==toggle.option ? draft.value : AddonRegistry::Read(extension,index);
            if (values[index]<0) return false;
        }
        if (!AddonRegistry::Apply(extension,values.data())) return false;
        draft.original=draft.value;
    }
    return true;
}

static void DrawEnhancedSettings(UiPoint origin,UiPoint scale) {
    const auto point = [origin,scale](float x,float y) { return At(origin,scale,x,y); };
    auto* draw = Ui::GetWindowDrawList();
    Heading(draw,"Enhanced Settings",point(34,20),scale,280);
    if (OptionRow("Mouse look:",draftEnhancedOptions.mouseLook ? "On" : "Off",point(10,58),scale,
        mouseLookAvailable ? "Hold the right mouse button over the world to look around. Right-click skills are replaced by camera control; inventory and menu clicks stay unchanged." : "Unavailable: client camera code differs. No camera changes were applied.",point(355,58),origin.x) && mouseLookAvailable)
        draftEnhancedOptions.mouseLook = !draftEnhancedOptions.mouseLook;
    if (OptionRow("Enhanced Zoom:",draftEnhancedOptions.enhancedZoom ? "On" : "Off",point(10,96),scale,
        enhancedZoomAvailable ? "Allows zooming out to the map's full camera range. Off restores the normal zoom limit." : "Unavailable: client camera code differs.",point(355,96),origin.x) && enhancedZoomAvailable)
        draftEnhancedOptions.enhancedZoom = !draftEnhancedOptions.enhancedZoom;
    if (OptionRow("Unbind left auto:",draftEnhancedOptions.unbindLeft ? "On" : "Off",point(10,134),scale,
        unbindAvailable ? "Stops fallback weapon attacks from left click, including an empty skill slot, cooldowns or insufficient mana. Movement and the assigned skill remain available." : "Unavailable: client input code differs. No input changes were applied.",point(355,134),origin.x) && unbindAvailable)
        draftEnhancedOptions.unbindLeft = !draftEnhancedOptions.unbindLeft;
    auto& health = draftEnhancedOptions.health;
    if (OptionRow("Low HP warning:",health.enabled ? "On" : "Off",point(10,172),scale,"Red screen edges while your character is alive and below the selected HP percentage.",point(355,172),origin.x)) health.enabled = !health.enabled;
    const auto threshold = std::to_string(health.threshold) + "%";
    const int limit = OptionRow("Below HP:",threshold.c_str(),point(10,210),scale,"The warning clears when health reaches this percentage of maximum HP.",point(355,210),origin.x);
    if (limit) health.threshold = unsigned(std::clamp(int(health.threshold) + limit,1,99));
    const auto intensity = std::to_string(health.intensity) + "%";
    const int opacity = OptionRow("Intensity:",intensity.c_str(),point(10,248),scale,"Opacity at the screen edges. The center stays clear; the warning does not flash.",point(355,248),origin.x);
    if (opacity) health.intensity = unsigned(std::clamp(int(health.intensity) + opacity * 5,10,90));
    if (OptionRow("Chat timestamps:",draftEnhancedOptions.chatTimestamps ? "On" : "Off",point(10,286),scale,
        chatTimestampsAvailable ? "Adds local time [HH:mm] to new chat messages." : "Unavailable: client chat code differs.",point(355,286),origin.x) && chatTimestampsAvailable)
        draftEnhancedOptions.chatTimestamps = !draftEnhancedOptions.chatTimestamps;
    const bool hasTrade=addonRegistry.TradeAuction();
    if (hasTrade && OptionRow("Trade Auction:",draftEnhancedOptions.tradeAuction ? "On" : "Off",point(10,324),scale,
        "Shows Trade Auction under your gold in Inventory and enables exact drop alerts.",point(355,324),origin.x))
        draftEnhancedOptions.tradeAuction = !draftEnhancedOptions.tradeAuction;
    const bool hasSallySam=addonRegistry.SallySam();
    const float vendorY=hasTrade ? 362.f : 324.f;
    if (hasSallySam && OptionRow("Sally & Sam Finder:",draftEnhancedOptions.sallySam ? "On" : "Off",point(10,vendorY),scale,
        "Shows loaded Sally and Sam vendors on the minimap and map.",point(355,vendorY),origin.x))
        draftEnhancedOptions.sallySam = !draftEnhancedOptions.sallySam;
    const bool hasCollection=addonRegistry.MythicCollection();
    const float collectionY=vendorY+(hasSallySam ? 38.f : 0.f);
    if (hasCollection && OptionRow("Mythic Collection:",draftEnhancedOptions.mythicCollection ? "On" : "Off",point(10,collectionY),scale,
        "Adds Collection Log to Quest Log and records collected Mythic items.",point(355,collectionY),origin.x))
        draftEnhancedOptions.mythicCollection = !draftEnhancedOptions.mythicCollection;
    const auto& toggles=addonRegistry.EnhancedToggles();
    for (size_t i=0;i<toggles.size();++i) {
        const auto& toggle=toggles[i];
        const auto& extension=addonRegistry.Extensions()[toggle.extension];
        auto& draft=enhancedToggleDraft[i];
        const float y=collectionY+38.0f*static_cast<float>(i+hasCollection);
        Ui::PushID(static_cast<int>(i));
        if (OptionRow(toggle.label.c_str(),draft.value<0 ? "Unavailable" : draft.value ? "On" : "Off",
            point(10,y),scale,extension.options[toggle.option].help,point(355,y),origin.x) && draft.value>=0)
            draft.value^=1;
        Ui::PopID();
    }
    const float extra=EnhancedExtraHeight();
    BodyText(draw,"Each option works independently. Mouse look uses the game's camera controls. Preview shows the HP warning for 5 seconds.",point(34,334+extra),UiPoint(scale.x*.85f,scale.y*.85f),BodyColor,280 * scale.x);
    if (DrawSkinControl("Preview",point(104,400+extra),scale)) lowHpPreviewUntil = WindowsCompat::Milliseconds() + 5000;
    if (!enhancedMessage.empty()) BodyText(draw,enhancedMessage.c_str(),point(34,447+extra),UiPoint(scale.x*.85f,scale.y*.85f),GoldColor,282 * scale.x);
    if (DrawSkinControl("Okay",point(24,476+extra),scale,&okayRect)) {
        if (SaveAddonOptions(enhancedSettings,draftEnhancedOptions)) {
            MeterMouseLookRelease();
            lowHpOptions = draftEnhancedOptions.health;
            unbindOptions.enabled = draftEnhancedOptions.unbindLeft;
            mouseLookEnabled = draftEnhancedOptions.mouseLook;
            enhancedZoomEnabled = draftEnhancedOptions.enhancedZoom;
            chatTimestampsEnabled = draftEnhancedOptions.chatTimestamps;
            tradeEnabled = draftEnhancedOptions.tradeAuction;
            sallySamEnabled = draftEnhancedOptions.sallySam;
            mythicCollectionEnabled = draftEnhancedOptions.mythicCollection;
            if (!mythicCollectionEnabled) ResetQuestLog();
            if (!tradeEnabled) CloseTrade();
            lowHpPreviewUntil = 0;
            if (ApplyEnhancedToggles()) activeAddon = nullptr;
            else enhancedMessage = "An addon setting could not be applied.";
        } else enhancedMessage = "Settings could not be saved.";
    }
    if (DrawSkinControl("Back",point(185,476+extra),scale,&cancelRect)) { lowHpPreviewUntil = 0; activeAddon = nullptr; }
}

static void DrawLowHpWarning(uint64_t now) {
    if (!addonRegistry.LowHp() || !worldVisible || !gameWindow || GetForegroundWindow() != gameWindow) return;
    const bool preview = activeAddon && activeAddon->open == OpenEnhancedSettings && now < lowHpPreviewUntil;
    if (preview || healthFrame.Warning(lowHpOptions,now)) healthVignette.Draw(graphics,Ui::GetWindowDrawList(),Ui::GetIO().DisplaySize,preview ? draftEnhancedOptions.health.intensity : lowHpOptions.intensity);
}
