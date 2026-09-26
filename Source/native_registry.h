#pragma once
#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#include "extension_api.h"

class AddonRegistry {
    std::vector<ExtensionDefinition> extensions;
    std::vector<ExtensionSettings> settings;
    bool damage = false, money = false, cooldowns = false, nameplates = false, characterSheet = false, mythicSounds = false;
    unsigned rejected = 0;

    static bool Plain(const char* value,size_t size) {
        size_t count = 0;
        while (count < size && value[count]) {
            const auto c=static_cast<unsigned char>(value[count++]);
            if (c<32 || c>126) return false;
        }
        return count>0 && count<size;
    }
    static bool Id(const std::string& value) {
        return !value.empty() && value.size()<48 && std::all_of(value.begin(),value.end(),[](char c) { return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='-'; });
    }
    static std::string Property(const std::filesystem::path& path,const char* key) {
        wchar_t output[128]{};
        std::wstring wideKey;
        for (const char* c=key;*c;++c) wideKey.push_back(static_cast<wchar_t>(*c));
        const auto count=GetPrivateProfileStringW(L"Addon",wideKey.c_str(),L"",output,128,path.c_str());
        if (count>=127) return "";
        std::string result;
        for (DWORD i=0;i<count;++i) { if (output[i]>127) return ""; result.push_back(static_cast<char>(output[i])); }
        return result;
    }
    static bool Regular(const std::filesystem::path& path,bool directory=false) {
        const DWORD attributes=GetFileAttributesW(path.c_str());
        return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT) && bool(attributes&FILE_ATTRIBUTE_DIRECTORY)==directory;
    }
    static bool Initialize(ExtensionInitialize initialize,const ExtensionHost* host,ExtensionDefinition* result) {
        __try { return initialize(host,result)!=FALSE; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool InitializeSettings(ExtensionGetSettings initialize,ExtensionSettings* result) {
        __try { return initialize(result)!=FALSE; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
public:
    static bool ValidSettings(const ExtensionSettings& value) {
        if (value.size!=sizeof(value) || value.version!=ExtensionSettingsVersion || !value.read || !value.apply ||
            !value.pageCount || value.pageCount>4 || !value.optionCount || value.optionCount>ExtensionSettingsLimit) return false;
        unsigned rows[4]{};
        for (unsigned i=0;i<value.pageCount;++i) if (!Plain(value.pages[i],sizeof(value.pages[i]))) return false;
        for (unsigned i=0;i<value.optionCount;++i) {
            const auto& option=value.options[i];
            if (!Plain(option.label,sizeof(option.label)) || !Plain(option.help,sizeof(option.help)) || option.page>=value.pageCount ||
                ++rows[option.page]>8 || option.minimum<0 || option.maximum>100000 || option.minimum>option.maximum || option.step<1 ||
                option.step>100000 || option.initial<option.minimum || option.initial>option.maximum ||
                (option.maximum-option.minimum)%option.step || (option.initial-option.minimum)%option.step) return false;
            if (option.format==ExtensionValueFormat::Toggle) {
                if (option.minimum!=0 || option.maximum!=1 || option.step!=1) return false;
            } else if (option.format!=ExtensionValueFormat::Integer && option.format!=ExtensionValueFormat::Percent && option.format!=ExtensionValueFormat::TenthsSeconds) return false;
        }
        for (unsigned i=0;i<value.pageCount;++i) if (!rows[i]) return false;
        return true;
    }
    void Discover(const std::filesystem::path& root) {
        extensions.clear(); settings.clear(); damage=money=cooldowns=nameplates=characterSheet=mythicSounds=false; rejected=0;
        std::error_code error;
        if (!Regular(root,true)) return;
        std::vector<std::filesystem::path> directories;
        for (std::filesystem::directory_iterator it(root,error),end;!error && it!=end;it.increment(error)) {
            if (directories.size()==64) { ++rejected; break; }
            if (Regular(it->path(),true)) directories.push_back(it->path());
        }
        std::sort(directories.begin(),directories.end());
        std::set<std::string> ids;
        for (const auto& directory:directories) {
            const auto manifest=directory/L"addon.ini";
            if (!Regular(manifest)) continue;
            if (std::filesystem::file_size(manifest,error)>4096 || error) { ++rejected; error.clear(); continue; }
            const auto id=Property(manifest,"Id"), builtin=Property(manifest,"Builtin");
            if (Property(manifest,"Api")!="1" || !Id(id) || !ids.insert(id).second) { ++rejected; continue; }
            if (builtin=="DamageMeter" && id=="damage-meter") { damage=true; continue; }
            if (builtin=="CooldownTimers" && id=="cooldown-timers") { cooldowns=true; continue; }
            if (builtin=="BetterCharacterSheet" && id=="better-character-sheet") { characterSheet=true; continue; }
            if (builtin=="MythicDropSounds" && id=="mythic-drop-sounds") { mythicSounds=true; continue; }
            if (builtin=="Nameplates" && id=="nameplates") { nameplates=true; continue; }
            if ((builtin=="HideGoldLabels" && id=="hide-gold-labels") || (builtin=="HideMoneyBoxes" && id=="hide-money-boxes")) { money=true; continue; }
            if (!builtin.empty() || id=="damage-meter" || id=="hide-gold-labels" || id=="hide-money-boxes" || id=="cooldown-timers" || id=="nameplates" || id=="better-character-sheet" || id=="mythic-drop-sounds") { ++rejected; continue; }
            const auto modulePath=directory/L"Addon.dll";
            if (!Regular(modulePath)) { ++rejected; continue; }
            const auto module=LoadLibraryExW(modulePath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
            const auto initialize=module ? reinterpret_cast<ExtensionInitialize>(GetProcAddress(module,"DungeonRunnersAddonInitialize")) : nullptr;
            const auto game=root.parent_path().wstring(),folder=directory.wstring();
            const ExtensionHost host{sizeof(ExtensionHost),ExtensionApiVersion,game.c_str(),folder.c_str()};
            ExtensionDefinition result{};
            result.size=sizeof(result); result.version=ExtensionApiVersion;
            bool valid=initialize && Initialize(initialize,&host,&result);
            valid=valid && result.size==sizeof(result) && result.version==ExtensionApiVersion && result.optionCount<=4;
            valid=valid && Plain(result.id,sizeof(result.id)) && result.id==id && Plain(result.name,sizeof(result.name)) && Plain(result.description,sizeof(result.description));
            valid=valid && (!result.optionCount || (result.read && result.apply));
            for (unsigned i=0;valid && i<result.optionCount;++i) valid=Plain(result.options[i].label,sizeof(result.options[i].label)) && Plain(result.options[i].help,sizeof(result.options[i].help));
            ExtensionSettings advanced{};
            const auto getSettings=module ? reinterpret_cast<ExtensionGetSettings>(GetProcAddress(module,"DungeonRunnersAddonGetSettings")) : nullptr;
            if (valid && getSettings) {
                advanced.size=sizeof(advanced); advanced.version=ExtensionSettingsVersion;
                valid=InitializeSettings(getSettings,&advanced) && ValidSettings(advanced);
            }
            if (valid) { extensions.push_back(result); settings.push_back(advanced); }
            else ++rejected;
        }
    }
    bool Damage() const { return damage; }
    bool Money() const { return money; }
    bool Cooldowns() const { return cooldowns; }
    bool CharacterSheet() const { return characterSheet; }
    bool MythicSounds() const { return mythicSounds; }
    bool Nameplates() const { return nameplates; }
    unsigned Rejected() const { return rejected; }
    const std::vector<ExtensionDefinition>& Extensions() const { return extensions; }
    const ExtensionSettings* Settings(const ExtensionDefinition& item) const {
        for (size_t i=0;i<extensions.size();++i) if (&extensions[i]==&item) return settings[i].size ? &settings[i] : nullptr;
        return nullptr;
    }
    static int ReadSetting(const ExtensionSettings& item,unsigned index) {
        if (index>=item.optionCount) return -1;
        __try {
            const int value=item.read(item.context,index); const auto& option=item.options[index];
            return value>=option.minimum && value<=option.maximum && (value-option.minimum)%option.step==0 ? value : -1;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    static bool ApplySettings(const ExtensionSettings& item,const int* values,char* error,uint32_t capacity) {
        if (!values || !error || capacity<2) return false;
        error[0]=0;
        for (unsigned i=0;i<item.optionCount;++i) {
            const auto& option=item.options[i];
            if (values[i]<option.minimum || values[i]>option.maximum || (values[i]-option.minimum)%option.step) return false;
        }
        __try { const bool ok=item.apply(item.context,values,item.optionCount,error,capacity)!=FALSE; error[capacity-1]=0; return ok; }
        __except(EXCEPTION_EXECUTE_HANDLER) { error[0]=0; return false; }
    }
    static int Read(const ExtensionDefinition& item,unsigned index) {
        __try { const int value=item.read(item.context,index); return value==0 || value==1 ? value : -1; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    static bool Apply(const ExtensionDefinition& item,const int* values) {
        __try { return item.apply(item.context,values,item.optionCount)!=FALSE; }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
};
