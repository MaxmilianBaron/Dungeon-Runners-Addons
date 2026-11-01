#pragma once
#include <cstdint>
#include <vector>

constexpr uint32_t MeterMagic = 0x324d5244;
constexpr uint32_t MeterVersion = 10;
constexpr unsigned MeterPlayers = 5;
constexpr unsigned MeterSkills = 8192;
constexpr unsigned MeterTargets = 8192;
constexpr unsigned MeterDetails = 32768;
constexpr unsigned MeterPets = 64;

#pragma pack(push, 1)
struct CriticalHits {
    uint32_t known = 0, total = 0, weapon = 0, magic = 0;
    uint32_t weaponHits = 0, magicHits = 0, otherHits = 0, reflectedHits = 0;
};
struct HitValues {
    uint64_t amount = 0;
    uint32_t hits = 0, lowest = 0, highest = 0;
};
struct HitStatistics {
    HitValues normal{}, critical{};
};
struct SkillRow {
    char name[80];
    char damageType[16];
    char component[24];
    double damage;
    double dps;
    uint32_t hits;
    CriticalHits critical{};
    uint64_t key = 0;
    HitStatistics values{};
};
struct TargetRow {
    uint64_t counterpartId;
    char counterpart[64];
    double damage, dps;
    uint32_t hits, detailFirst, detailCount;
    double unrecordedDamage;
    uint32_t unrecordedHits;
};
struct DetailRow {
    uint32_t skill;
    double damage;
    uint32_t hits;
    CriticalHits critical{};
    HitStatistics values{};
};
struct PlayerRow {
    uint32_t characterId;
    char name[64];
    double damage;
    double dps;
    uint32_t hits;
    uint32_t skillCount;
    uint32_t skillFirst, targetCount, targetFirst;
    double unrecordedSkillDamage;
    uint32_t unrecordedSkillHits;
    double unrecordedTargetDamage;
    uint32_t unrecordedTargetHits;
    CriticalHits critical;
};
struct PetRow {
    uint64_t key;
    PlayerRow data;
};
#pragma pack(pop)
struct MeterView {
    uint32_t playerCount = 0;
    double duration = 0;
    PlayerRow players[MeterPlayers]{};
    uint32_t petCount = 0;
    PetRow pets[MeterPets]{};
    std::vector<SkillRow> skills;
    std::vector<TargetRow> targets;
    std::vector<DetailRow> details;
};
struct MeterPacket {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    char status[128];
    MeterView views[4];
};
static_assert(sizeof(SkillRow) == 220);
static_assert(sizeof(TargetRow) == 112);
static_assert(sizeof(DetailRow) == 88);
static_assert(sizeof(HitValues) == 20 && sizeof(HitStatistics) == 40);
static_assert(sizeof(CriticalHits) == 32);
static_assert(sizeof(PlayerRow) == 160);
static_assert(sizeof(PetRow) == 168);
