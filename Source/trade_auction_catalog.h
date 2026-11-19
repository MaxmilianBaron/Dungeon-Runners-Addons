#pragma once
#include "bank_sort.h"
#include "trade_auction_watch.h"
#include <climits>
#include <map>
#include <optional>
#include <string_view>

namespace TradeAuction {
enum class CatalogGroup { All, Jewelry, Fighter, Ranger, Mage, Other };
enum class CatalogType {
    All, Rings, Amulets, OneHanded, TwoHanded, Shields, Pistols, Crossbows, Shotguns, Cannons, Wands, Staves,
    BodyArmor, Helmets, Shoulders, Gloves, Boots, Potions, Scrolls, QuestItems, Other
};

inline constexpr const char* GroupNames[]={"All Items","Jewelry","Fighter","Ranger","Mage","Other"};
inline constexpr const char* TypeNames[]={
    "All Types","Rings","Amulets","1H Weapons","2H Weapons","Shields","Pistols","Crossbows","Shotguns","Cannons","Wands","Staves",
    "Body Armor","Helmets","Shoulders","Gloves","Boots","Potions","Scrolls","Quest Items","Other Items"
};
struct CatalogSection {
    CatalogGroup group;
    CatalogType type;
};

inline CatalogSection Section(const BankCatalogEntry& entry) {
    using Group=CatalogGroup;
    using Type=CatalogType;
    constexpr Group roles[]={Group::Fighter,Group::Ranger,Group::Mage,Group::Other};
    switch (entry.category) {
    case 0:
        if (std::strcmp(entry.kind,"0-scroll")==0) return {Group::Other,Type::Scrolls};
        if (std::strcmp(entry.kind,"1-potion")==0) return {Group::Other,Type::Potions};
        return {Group::Other,Type::QuestItems};
    case 1: return {Group::Jewelry,Type::Rings};
    case 2: return {Group::Jewelry,Type::Amulets};
    case 3:
    case 4: {
        if (std::strcmp(entry.kind,"shield")==0) return {roles[entry.role<3 ? entry.role : 0],Type::Shields};
        struct Weapon {
            const char* kind;
            CatalogSection section;
        };
        constexpr Weapon types[]={
            {"1hgun",{Group::Ranger,Type::Pistols}},{"1hcrossbow",{Group::Ranger,Type::Crossbows}},
            {"2hcrossbow",{Group::Ranger,Type::Crossbows}},
            {"2hgun",{Group::Ranger,Type::Shotguns}},{"2hcannon",{Group::Ranger,Type::Cannons}},
            {"1hstaff",{Group::Mage,Type::Wands}},{"2hstaff",{Group::Mage,Type::Staves}}
        };
        for (const auto& weapon:types) if (std::strcmp(entry.kind,weapon.kind)==0) return weapon.section;
        return {Group::Fighter,entry.category==3 ? Type::OneHanded : Type::TwoHanded};
    }
    case 5: {
        constexpr Type slots[]={Type::Helmets,Type::BodyArmor,Type::Shoulders,Type::Gloves,Type::Boots};
        const auto slot=BankEquipmentOrder(entry.slot);
        if (slot<std::size(slots)) return {roles[std::min<unsigned>(entry.role,3)],slots[slot]};
        break;
    }
    }
    return {Group::Other,Type::Other};
}

inline const TradeRollRecipe* FirstRecipe(const char* item) {
    return std::lower_bound(std::begin(TradeRollRecipes),std::end(TradeRollRecipes),item,
        [](const TradeRollRecipe& recipe,const char* key){return std::strcmp(recipe.item,key)<0;});
}

struct CatalogItem {
    const BankCatalogEntry* entry;
    std::string_view rarity;
    std::vector<const TradeRollRecipe*> recipes;
};

inline const std::vector<CatalogItem>& SortedCatalog() {
    static const auto entries=[] {
        std::vector<CatalogItem> sorted;
        using Key=std::tuple<std::string_view,std::string,std::string_view,CatalogGroup,CatalogType,unsigned>;
        std::map<Key,size_t> groups;
        for (const auto& entry:BankItemCatalog) {
            if (!*entry.label || !*entry.icon) continue;
            const auto section=Section(entry);
            const auto name=BankLower(entry.label);
            for (auto recipe=FirstRecipe(entry.key);recipe!=std::end(TradeRollRecipes) && std::strcmp(recipe->item,entry.key)==0;++recipe) {
                const std::string_view rarity=recipe->rarity;
                if (rarity!="Mythic" && rarity!="Unique") continue;
                const auto grouped=groups.try_emplace(Key{rarity,name,entry.icon,section.group,section.type,entry.slot},sorted.size());
                if (grouped.second) sorted.push_back({&entry,rarity,{}});
                auto& variants=sorted[grouped.first->second].recipes;
                const bool covered=std::any_of(variants.begin(),variants.end(),[recipe](const auto* variant) {
                    return CanonicalDefinition(recipe->item)==CanonicalDefinition(variant->item) && recipe->slots==variant->slots &&
                        recipe->fixedSet==variant->fixedSet && recipe->exact==variant->exact &&
                        recipe->low>=variant->low && recipe->high<=variant->high;
                });
                if (!covered) variants.push_back(recipe);
            }
        }
        auto key=[](const CatalogItem& item) {
            return std::make_tuple(item.rarity,BankLower(item.entry->label),std::string_view(item.entry->key));
        };
        std::sort(sorted.begin(),sorted.end(),[&](const auto& left,const auto& right) { return key(left)<key(right); });
        return sorted;
    }();
    return entries;
}

inline std::string VariantMaterial(const BankCatalogEntry& entry) {
    std::string_view material=entry.family;
    material=material.substr(0,material.find('_'));
    if (material.size()>6 && (material.substr(material.size()-6)=="mythic" ||
        material.substr(material.size()-6)=="unique")) material.remove_suffix(6);
    if (material.empty()) return {};
    std::string label(material);
    if (label.front()>='a' && label.front()<='z') label.front()=static_cast<char>(label.front()-'a'+'A');
    return label;
}

inline const TradeRollOption* RollChoice(unsigned group,unsigned number) {
    const auto& slot=TradeRollGroups[group];
    return number && number<=slot.count ? &TradeRollOptions[slot.first+number-1] : nullptr;
}

inline bool CompatibleRolls(const TradeRollRecipe& recipe,const std::array<unsigned,6>& chosen) {
    unsigned low=recipe.low,high=recipe.high;
    for (size_t i=0;i<chosen.size();++i) if (chosen[i]) {
        const auto* option=RollChoice(recipe.slots[i],chosen[i]);
        if (!option) return false;
        low=std::max(low,option->low);
        high=std::min(high,option->high);
    }
    return low<=high;
}

inline int ShieldBlockEffect(std::string_view effect,unsigned tier,unsigned quality) {
    const auto end=std::end(TradeBlockEffects);
    const auto found=std::lower_bound(std::begin(TradeBlockEffects),end,effect,
        [](const TradeBlockEffect& row,std::string_view key){return row.effect<key;});
    return found!=end && effect==found->effect ? found->values[(quality==7 ? 3u : 0u)+tier-1] : 0;
}

inline std::string ShieldBlockLabel(const TradeRollRecipe& recipe,unsigned tier,const std::array<unsigned,6>& chosen) {
    int minimum=INT_MAX,maximum=INT_MIN;
    const auto& fixed=TradeFixedEffectSets[recipe.fixedSet];
    const unsigned quality=std::string_view(recipe.rarity)=="Mythic" ? 7 : 5;
    for (unsigned level=tier ? tier : 1;level<=(tier ? tier : 3);++level) {
        int first=0,last=0;
        for (unsigned i=0;i<fixed.count;++i)
            first+=ShieldBlockEffect(TradeFixedEffectIds[fixed.first+i],level,quality);
        last=first;
        auto candidate=chosen;
        for (size_t i=0;i<recipe.slots.size() && recipe.slots[i];++i) {
            const auto group=recipe.slots[i];
            if (const auto* roll=RollChoice(group,chosen[i])) {
                const int block=ShieldBlockEffect(roll->effect ? roll->effect : "",level,quality);
                first+=block;
                last+=block;
                continue;
            }
            int low=INT_MAX,high=INT_MIN;
            for (unsigned option=1;option<=TradeRollGroups[group].count;++option) {
                candidate[i]=option;
                if (!CompatibleRolls(recipe,candidate)) continue;
                const auto* roll=RollChoice(group,option);
                const int block=ShieldBlockEffect(roll->effect ? roll->effect : "",level,quality);
                low=std::min(low,block);
                high=std::max(high,block);
            }
            candidate[i]=chosen[i];
            if (low<=high) {
                first+=low;
                last+=high;
            }
        }
        minimum=std::min(minimum,first);
        maximum=std::max(maximum,last);
    }
    return std::to_string(minimum)+(minimum==maximum ? "" : "–"+std::to_string(maximum))+"%";
}

inline std::optional<LocalWatch> MakeLocalWatch(const TradeRollRecipe& recipe,unsigned tier,
    const std::array<unsigned,6>& chosen) {
    if (!recipe.exact || tier<1 || tier>3 || !CompatibleRolls(recipe,chosen)) return std::nullopt;
    const std::string_view rarity=recipe.rarity;
    const unsigned quality=rarity=="Mythic" ? 7 : rarity=="Unique" ? 5 : 0;
    const auto& fixed=TradeFixedEffectSets[recipe.fixedSet];
    LocalWatch watch{{std::string(CanonicalDefinition(recipe.item)),tier,quality,{}},{}};
    if (const auto* armor=FindArmorRating(watch.roll.definition))
        watch.roll.defense=DefenseRating(*armor,tier,quality);
    watch.roll.effects.assign(TradeFixedEffectIds+fixed.first,TradeFixedEffectIds+fixed.first+fixed.count);
    for (size_t i=0;i<recipe.slots.size() && recipe.slots[i];++i) {
        const auto group=recipe.slots[i];
        const auto* option=RollChoice(group,chosen[i]);
        if (!option || !option->effect) return std::nullopt;
        if (*option->effect) watch.roll.effects.emplace_back(option->effect);
        watch.labels.push_back(std::string(TradeRollGroups[group].label)+" · "+option->label);
    }
    if (*recipe.fixed) {
        std::string_view bonuses=recipe.fixed;
        constexpr std::string_view separator=" · ";
        std::string line="Fixed Bonuses";
        while (!bonuses.empty()) {
            const auto end=bonuses.find(separator);
            const auto bonus=bonuses.substr(0,end);
            if (line.size()+separator.size()+bonus.size()<=192) {
                line.append(separator);
                line.append(bonus);
            }
            if (end==std::string_view::npos) break;
            bonuses.remove_prefix(end+separator.size());
        }
        watch.labels.push_back(std::move(line));
    }
    std::sort(watch.roll.effects.begin(),watch.roll.effects.end());
    if (!watch.roll.Valid() ||
        std::any_of(watch.labels.begin(),watch.labels.end(),[](const auto& label){return label.size()>192;}))
        return std::nullopt;
    return watch;
}
}
