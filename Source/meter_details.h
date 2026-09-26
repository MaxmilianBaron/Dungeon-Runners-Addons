#pragma once
#include "overlay_protocol.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

struct MeterDetailRow : SkillRow {
    uint64_t counterpartId = 0;
    char counterpart[64]{};
    bool limited = false;
};
inline bool MeterSkillDetail(const MeterView& view,const PlayerRow& owner,uint64_t key,uint64_t actor,const std::string& actorName,MeterDetailRow& result) {
    result = {};
    if (actorName.empty()) {
        for (uint32_t i=0;i<owner.skillCount;++i) {
            const auto& skill = view.skills[owner.skillFirst+i];
            if (skill.key != key) continue;
            static_cast<SkillRow&>(result) = skill;
            return true;
        }
    } else {
        for (uint32_t i=0;i<owner.targetCount;++i) {
            const auto& target = view.targets[owner.targetFirst+i];
            if (target.counterpartId != actor || target.counterpart != actorName) continue;
            for (uint32_t j=0;j<target.detailCount;++j) {
                const auto& detail = view.details[target.detailFirst+j];
                const auto& skill = view.skills[detail.skill];
                if (skill.key != key) continue;
                static_cast<SkillRow&>(result) = skill;
                result.damage = detail.damage; result.hits = detail.hits; result.critical = detail.critical; result.values = detail.values;
                result.dps = detail.damage/std::max(1.0,view.duration);
                return true;
            }
            break;
        }
    }
    return false;
}
inline void MeterDetailRows(const MeterView& view,const PlayerRow& player,bool actors,uint64_t actor,const std::string& actorName,std::vector<MeterDetailRow>& rows) {
    rows.clear();
    const auto missing=[&](double damage,uint32_t hits,const char* label) {
        if (!damage && !hits) return;
        MeterDetailRow row{};
        std::snprintf(row.name,sizeof(row.name),"%s",label);
        std::snprintf(row.component,sizeof(row.component),"%s","Detail limit reached");
        std::snprintf(row.damageType,sizeof(row.damageType),"%s","Unrecorded");
        row.damage=damage; row.dps=damage/std::max(1.0,view.duration); row.hits=hits; row.limited=true;
        rows.push_back(row);
    };
    if (actors) {
        for (uint32_t i=0;i<player.targetCount;++i) {
            const auto& target=view.targets[player.targetFirst+i];
            MeterDetailRow row{};
            std::snprintf(row.name,sizeof(row.name),"%s",target.counterpart);
            std::snprintf(row.component,sizeof(row.component),"%s",target.counterpartId==UINT64_MAX ? "Environment" : target.counterpartId>>63 ? "Creature" : target.counterpartId ? "Player" : "Unresolved");
            std::snprintf(row.damageType,sizeof(row.damageType),"%s","All");
            row.damage=target.damage; row.dps=target.dps; row.hits=target.hits;
            row.counterpartId=target.counterpartId; std::memcpy(row.counterpart,target.counterpart,sizeof(row.counterpart));
            rows.push_back(row);
        }
        missing(player.unrecordedTargetDamage,player.unrecordedTargetHits,"Unrecorded targets");
    } else if (actorName.empty()) {
        for (uint32_t i=0;i<player.skillCount;++i) {
            MeterDetailRow row{}; static_cast<SkillRow&>(row)=view.skills[player.skillFirst+i]; rows.push_back(row);
        }
        missing(player.unrecordedSkillDamage,player.unrecordedSkillHits,"Unrecorded skills");
    } else {
        for (uint32_t i=0;i<player.targetCount;++i) {
            const auto& target=view.targets[player.targetFirst+i];
            if (target.counterpartId!=actor || target.counterpart!=actorName) continue;
            for (uint32_t j=0;j<target.detailCount;++j) {
                const auto& detail=view.details[target.detailFirst+j];
                MeterDetailRow row{}; static_cast<SkillRow&>(row)=view.skills[detail.skill];
                row.damage=detail.damage; row.dps=detail.damage/std::max(1.0,view.duration); row.hits=detail.hits; row.critical=detail.critical; row.values=detail.values;
                rows.push_back(row);
            }
            missing(target.unrecordedDamage,target.unrecordedHits,"Unrecorded skills");
            break;
        }
    }
}
