#pragma once
#include "overlay_protocol.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

struct MeterMember {
    uint32_t id = 0;
    std::string name;
};

struct MeterHit {
    uint32_t sourceId = 0;
    uint32_t targetId = 0;
    int32_t before = 0;
    int32_t after = 0;
    int32_t amount = 0;
    uint8_t element = 0;
    std::string skillKey;
    std::string skillName;
    uint8_t damageClass = 0;
    uint64_t sourceKey = 0, targetKey = 0;
    std::string sourceName, targetName;
    bool pet = false;
    uint32_t targetPetOwnerId = 0;
    bool criticalKnown = false, critical = false;
    uint8_t criticalSource = 0;
};

class NativeMeter {
    static void AddCritical(CriticalHits& c,const MeterHit& hit) {
        if (!hit.criticalKnown) return;
        ++c.known;
        if (hit.damageClass == 4) { ++c.reflectedHits; return; }
        if (hit.criticalSource == 1) ++c.weaponHits;
        else if (hit.criticalSource == 2) ++c.magicHits;
        else ++c.otherHits;
        if (!hit.critical) return;
        ++c.total;
        if (hit.criticalSource == 1) ++c.weapon;
        else if (hit.criticalSource == 2) ++c.magic;
    }
    struct Count {
        uint64_t amount = 0;
        uint32_t hits = 0;
        CriticalHits critical;
        HitStatistics values;
        void Add(const MeterHit& hit) {
            amount += static_cast<uint32_t>(hit.amount);
            if (hits != UINT32_MAX) {
                ++hits; AddCritical(critical,hit);
                if (hit.criticalKnown) {
                    auto& stats = hit.critical && hit.damageClass != 4 ? values.critical : values.normal;
                    const auto damage = static_cast<uint32_t>(hit.amount);
                    if (!stats.hits || damage < stats.lowest) stats.lowest = damage;
                    if (damage > stats.highest) stats.highest = damage;
                    stats.amount += damage;
                    ++stats.hits;
                }
            }
        }
    };
    using SkillKey = std::tuple<std::string,std::string,uint8_t,uint8_t>;
    struct SkillLess {
        using is_transparent = void;
        template<class A,class B> bool operator()(const A& a,const B& b) const {
            return std::make_tuple(std::string_view(std::get<0>(a)),std::string_view(std::get<1>(a)),std::get<2>(a),std::get<3>(a)) <
                std::make_tuple(std::string_view(std::get<0>(b)),std::string_view(std::get<1>(b)),std::get<2>(b),std::get<3>(b));
        }
    };
    struct TargetLess {
        using is_transparent = void;
        template<class A,class B> bool operator()(const A& a,const B& b) const {
            return a.first != b.first ? a.first < b.first : std::string_view(a.second) < std::string_view(b.second);
        }
    };
    struct Skill {
        std::string name;
        uint8_t element = 0, component = 0;
        uint64_t key = 0;
        Count value;
    };
    struct Target {
        uint64_t id = 0;
        std::string name;
        Count value, unrecorded;
    };
    struct Source {
        uint64_t amount = 0;
        uint32_t hits = 0;
        CriticalHits critical;
        std::map<SkillKey,uint32_t,SkillLess> skillIndex;
        std::map<std::pair<uint64_t,std::string>,uint32_t,TargetLess> targetIndex;
        std::vector<Skill> skills;
        std::vector<Target> targets;
        std::map<uint32_t,Count> details;
        Count unrecordedSkills, unrecordedTargets;
    };
    struct Budget { unsigned skills = 0, targets = 0, details = 0; };
    Budget budgets[4]{};
    std::vector<uint32_t> order, skillMap;
    struct Activity {
        uint64_t first = 0, last = 0;
        double closedSeconds = 0;
        bool hasFight = false, open = false;
        double Seconds() const { return hasFight ? std::max(1.0, static_cast<double>(last - first) / 1000.0) : 0; }
        void Close() { if (open) { closedSeconds += Seconds(); open = false; } }
    };
    std::map<uint32_t, Source> current[2];
    std::map<uint32_t, Source> overall[2];
    using PetIdentity = std::pair<uint32_t, std::string>;
    std::map<PetIdentity, Source> currentPets, overallPets;
    std::vector<MeterMember> roster;
    uint32_t selfId = 0;
    Activity activity{};
    bool enabled = true;
    std::string failure;

    static void Text(char* target, size_t capacity, std::string_view value) {
        size_t size = std::min(capacity - 1, value.size());
        while (size < value.size() && size && (static_cast<unsigned char>(value[size]) & 0xc0) == 0x80) --size;
        std::memcpy(target, value.data(), size);
        target[size] = 0;
    }
    static void Add(Source& row, Budget& budget, const MeterHit& hit, bool taken) {
        const bool reflected = hit.damageClass == 4;
        const uint64_t counterpartId = taken ? hit.sourceKey : hit.targetKey;
        std::string_view counterpart = taken ? hit.sourceName : hit.targetName;
        if (counterpart.empty()) counterpart = taken ? "Unknown attacker" : "Unknown target";
        const std::string_view skillId = reflected ? std::string_view("reflection") : hit.skillKey;
        const std::string_view pet = hit.pet ? std::string_view(hit.sourceName) : std::string_view();
        const auto key = std::make_tuple(skillId,pet,hit.element,hit.damageClass);
        const auto targetKey = std::make_pair(counterpartId,counterpart);
        auto skillIt = row.skillIndex.find(key);
        auto targetIt = row.targetIndex.find(targetKey);
        if (skillIt == row.skillIndex.end() && budget.skills < MeterSkills && skillId.size() <= 2048 && pet.size() <= 512 && hit.skillName.size() <= 2048) {
            Skill skill;
            skill.name = reflected ? "Damage reflection" : hit.skillName.empty() ? "Unknown effect" : hit.skillName;
            if (!reflected && hit.skillKey == "direct") skill.name = hit.damageClass == 1 || hit.damageClass == 2 ? "Weapon attack" : "Additional damage";
            if (hit.pet) skill.name = hit.sourceName + " / " + skill.name;
            skill.element = hit.element; skill.component = hit.damageClass;
            uint64_t identity = 14695981039346656037ull;
            for (const auto part : {skillId,pet}) {
                for (unsigned char c : part) { identity ^= c; identity *= 1099511628211ull; }
                identity ^= 0xff; identity *= 1099511628211ull;
            }
            identity ^= hit.element; identity *= 1099511628211ull;
            identity ^= hit.damageClass; identity *= 1099511628211ull;
            skill.key = identity ? identity : 1;
            const auto index = static_cast<uint32_t>(row.skills.size());
            row.skills.push_back(std::move(skill));
            try { skillIt = row.skillIndex.emplace(SkillKey{skillId,pet,hit.element,hit.damageClass},index).first; }
            catch (...) { row.skills.pop_back(); throw; }
            ++budget.skills;
        }
        if (targetIt == row.targetIndex.end() && budget.targets < MeterTargets && counterpart.size() <= 512) {
            Target target; target.id = counterpartId; target.name = counterpart;
            const auto index = static_cast<uint32_t>(row.targets.size());
            row.targets.push_back(std::move(target));
            try { targetIt = row.targetIndex.emplace(std::make_pair(counterpartId,std::string(counterpart)),index).first; }
            catch (...) { row.targets.pop_back(); throw; }
            ++budget.targets;
        }
        if (skillIt != row.skillIndex.end()) row.skills[skillIt->second].value.Add(hit);
        else row.unrecordedSkills.Add(hit);
        if (targetIt != row.targetIndex.end()) {
            auto& target = row.targets[targetIt->second];
            target.value.Add(hit);
            const uint32_t detailKey = skillIt == row.skillIndex.end() ? UINT32_MAX : (targetIt->second << 16) | skillIt->second;
            auto detail = row.details.find(detailKey);
            if (detailKey != UINT32_MAX && (detail != row.details.end() || budget.details < MeterDetails)) {
                if (detail == row.details.end()) { detail = row.details.emplace(detailKey,Count{}).first; ++budget.details; }
                detail->second.Add(hit);
            } else target.unrecorded.Add(hit);
        } else row.unrecordedTargets.Add(hit);
        row.amount += static_cast<uint32_t>(hit.amount);
        if (row.hits != UINT32_MAX) {
            ++row.hits;
            AddCritical(row.critical,hit);
        }
    }
    void Begin(uint64_t now) {
        if (!activity.open) {
            for (auto& sources : current) sources.clear();
            currentPets.clear();
            budgets[0] = budgets[2] = {};
            activity.first = now;
            activity.open = activity.hasFight = true;
        }
        activity.last = now;
    }
    bool Member(uint32_t id) const {
        return id && std::any_of(roster.begin(), roster.end(), [id](const auto& member) { return member.id == id; });
    }
    void Fill(MeterView& view, PlayerRow& row, const Source& source, double seconds) {
        row.damage = static_cast<double>(source.amount) / 256.0;
        row.dps = row.damage / std::max(1.0, seconds);
        row.hits = source.hits;
        row.critical = source.critical;
        row.unrecordedSkillDamage = static_cast<double>(source.unrecordedSkills.amount) / 256.0;
        row.unrecordedSkillHits = source.unrecordedSkills.hits;
        row.unrecordedTargetDamage = static_cast<double>(source.unrecordedTargets.amount) / 256.0;
        row.unrecordedTargetHits = source.unrecordedTargets.hits;
        row.skillFirst = static_cast<uint32_t>(view.skills.size());
        row.skillCount = static_cast<uint32_t>(source.skills.size());
        order.resize(source.skills.size()); skillMap.resize(source.skills.size());
        for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b) { const auto& x=source.skills[a]; const auto& y=source.skills[b]; return x.value.amount!=y.value.amount ? x.value.amount>y.value.amount : x.name!=y.name ? x.name<y.name : a<b; });
        const char* elements[] = {"Crushing", "Piercing", "Slashing", "Fire", "Ice", "Poison", "Shadow", "Divine"};
        const char* components[] = {"Unclassified", "Melee weapon", "Ranged weapon", "Spell / bonus", "Reflection"};
        for (uint32_t index : order) {
            const auto& skill = source.skills[index];
            skillMap[index] = static_cast<uint32_t>(view.skills.size());
            SkillRow result{};
            Text(result.name,sizeof(result.name),skill.name);
            Text(result.damageType,sizeof(result.damageType),skill.element < 8 ? elements[skill.element] : "Unknown");
            Text(result.component,sizeof(result.component),skill.component < 5 ? components[skill.component] : "Unclassified");
            result.damage = static_cast<double>(skill.value.amount) / 256.0;
            result.dps = result.damage / std::max(1.0,seconds); result.hits = skill.value.hits;
            result.critical = skill.value.critical; result.key = skill.key;
            result.values = skill.value.values;
            view.skills.push_back(result);
        }
        row.targetFirst = static_cast<uint32_t>(view.targets.size());
        row.targetCount = static_cast<uint32_t>(source.targets.size());
        order.resize(source.targets.size());
        for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort(order.begin(),order.end(),[&](uint32_t a,uint32_t b) { const auto& x=source.targets[a]; const auto& y=source.targets[b]; return x.value.amount!=y.value.amount ? x.value.amount>y.value.amount : x.name!=y.name ? x.name<y.name : a<b; });
        for (uint32_t index : order) {
            const auto& target = source.targets[index];
            TargetRow result{};
            result.counterpartId = target.id; Text(result.counterpart,sizeof(result.counterpart),target.name);
            result.damage = static_cast<double>(target.value.amount) / 256.0;
            result.dps = result.damage / std::max(1.0,seconds); result.hits = target.value.hits;
            result.unrecordedDamage = static_cast<double>(target.unrecorded.amount) / 256.0; result.unrecordedHits = target.unrecorded.hits;
            result.detailFirst = static_cast<uint32_t>(view.details.size());
            for (auto detail = source.details.lower_bound(index << 16); detail != source.details.end() && (detail->first >> 16) == index; ++detail) {
                view.details.push_back({skillMap[detail->first & 0xffff],static_cast<double>(detail->second.amount) / 256.0,detail->second.hits,detail->second.critical,detail->second.values});
            }
            result.detailCount = static_cast<uint32_t>(view.details.size()) - result.detailFirst;
            std::sort(view.details.begin() + result.detailFirst,view.details.end(),[](const auto& a,const auto& b) { return a.damage != b.damage ? a.damage > b.damage : a.skill < b.skill; });
            view.targets.push_back(result);
        }
    }
    void View(MeterView& output, const std::map<uint32_t, Source>& sources, double seconds) {
        output.duration = seconds;
        output.playerCount = static_cast<uint32_t>(roster.size());
        struct Entry { const MeterMember* member; const Source* source; double damage; };
        std::array<Entry,MeterPlayers> ordered{};
        for (size_t i = 0; i < roster.size(); ++i) {
            const auto found = sources.find(roster[i].id);
            const auto* source = found != sources.end() ? &found->second : nullptr;
            ordered[i] = {&roster[i],source,source ? static_cast<double>(source->amount) / 256.0 : 0};
        }
        std::sort(ordered.begin(),ordered.begin() + roster.size(),[](const Entry& a,const Entry& b) { return a.damage != b.damage ? a.damage > b.damage : a.member->id < b.member->id; });
        for (size_t i = 0; i < roster.size(); ++i) {
            auto& row = output.players[i];
            row = {};
            row.characterId = ordered[i].member->id;
            Text(row.name, sizeof(row.name), ordered[i].member->name);
            if (ordered[i].source) Fill(output, row, *ordered[i].source, seconds);
        }
    }
    void Pets(MeterView& output, const std::map<PetIdentity, Source>& sources, double seconds) {
        struct Entry { const std::map<PetIdentity, Source>::value_type* item; char name[64]; double damage; };
        std::array<Entry,MeterPets> ordered{};
        size_t count = 0;
        for (const auto& item : sources) {
            if (!Member(item.first.first)) continue;
            if (count == ordered.size()) break;
            auto& entry = ordered[count++];
            entry.item = &item;
            Text(entry.name, sizeof(entry.name), item.first.second);
            entry.damage = static_cast<double>(item.second.amount) / 256.0;
        }
        std::sort(ordered.begin(),ordered.begin() + count,[](const Entry& a,const Entry& b) {
            if (a.damage != b.damage) return a.damage > b.damage;
            if (a.item->first.first != b.item->first.first) return a.item->first.first < b.item->first.first;
            return std::strcmp(a.name,b.name) < 0;
        });
        for (size_t i = 0; i < count; ++i) {
            const auto& item = *ordered[i].item;
            auto& result = output.pets[output.petCount++];
            result = {};
            uint64_t key = 14695981039346656037ull;
            for (unsigned char c : item.first.second) { key ^= c; key *= 1099511628211ull; }
            result.key = key | 0x8000000000000000ull;
            result.data.characterId = item.first.first;
            Text(result.data.name, sizeof(result.data.name), item.first.second);
            Fill(output, result.data, item.second, seconds);
        }
    }
public:
    void Close() { activity.Close(); }
    void Reset() {
        for (unsigned i = 0; i < 2; ++i) { current[i].clear(); overall[i].clear(); }
        currentPets.clear(); overallPets.clear();
        for (auto& budget : budgets) budget = {};
        activity = {}; failure.clear();
    }
    void Fail(const char* message) { Close(); failure = message; }
    void Enable(bool value) { if (enabled != value) Close(); enabled = value; }
    bool Enabled() const { return enabled && failure.empty(); }
    bool HasRoster() const { return !roster.empty(); }
    void Zone() { Close(); roster.clear(); }
    bool Roster(uint32_t self, const std::vector<MeterMember>& members) {
        if (!self || members.empty() || members.size() > MeterPlayers) return false;
        std::vector<uint32_t> ids;
        for (const auto& member : members) {
            if (!member.id || std::find(ids.begin(), ids.end(), member.id) != ids.end()) return false;
            ids.push_back(member.id);
        }
        if (std::find(ids.begin(), ids.end(), self) == ids.end()) return false;
        if (selfId && selfId != self) Reset();
        else if (!roster.empty() && (roster.size()!=members.size() || std::any_of(ids.begin(),ids.end(),[this](uint32_t id) { return !Member(id); }))) Close();
        selfId = self;
        roster = members;
        return true;
    }
    void Tick(uint64_t now) {
        if (activity.open && now >= activity.last && now - activity.last >= 10000u) activity.Close();
    }
    bool Hit(const MeterHit& hit, uint64_t now) {
        if (!Enabled()) return false;
        const int64_t delta = static_cast<int64_t>(hit.before) - hit.after;
        if (delta <= 0) return false;
        if (hit.after < 0 || delta != hit.amount || hit.amount > hit.before) { Fail("Measurement stopped: inconsistent HP sample"); return false; }
        if (!Member(hit.sourceId) && !Member(hit.targetId) && !Member(hit.targetPetOwnerId)) return false;
        if (hit.sourceId && hit.sourceId == hit.targetId && !hit.pet) return false;
        if (activity.hasFight && now < activity.last) { Fail("Measurement stopped: clock moved backwards"); return false; }
        Tick(now);
        Begin(now);
        if (Member(hit.targetPetOwnerId)) {
            const PetIdentity identity{hit.targetPetOwnerId,hit.targetName};
            if (overallPets.size() >= MeterPets && overallPets.find(identity) == overallPets.end()) { Fail("Pet history full; reset the meter"); return false; }
            Add(currentPets[identity], budgets[2], hit, true);
            Add(overallPets[identity], budgets[3], hit, true);
        }
        for (unsigned direction = 0; direction < 2; ++direction) {
            const uint32_t id = direction ? hit.targetId : hit.sourceId;
            if (!Member(id)) continue;
            if (overall[direction].size() >= 64 && overall[direction].find(id) == overall[direction].end()) { Fail("Player history full; reset the meter"); return false; }
            Add(current[direction][id], budgets[direction * 2], hit, direction != 0);
            Add(overall[direction][id], budgets[direction * 2 + 1], hit, direction != 0);
        }
        return true;
    }
    void Snapshot(MeterPacket& output, uint64_t now) {
        Tick(now);
        output.flags = 0;
        for (auto& view : output.views) { view.playerCount = view.petCount = 0; view.duration = 0; view.skills.clear(); view.targets.clear(); view.details.clear(); }
        output.magic = MeterMagic;
        output.version = MeterVersion;
        Text(output.status, sizeof(output.status), !failure.empty() ? failure.c_str() : enabled ? "" : "Measurement disabled");
        for (unsigned direction = 0; direction < 2; ++direction) {
            const double seconds = activity.Seconds();
            View(output.views[direction * 2], current[direction], seconds);
            View(output.views[direction * 2 + 1], overall[direction], activity.closedSeconds + (activity.open ? seconds : 0));
        }
        Pets(output.views[2], currentPets, output.views[2].duration);
        Pets(output.views[3], overallPets, output.views[3].duration);
    }
};
