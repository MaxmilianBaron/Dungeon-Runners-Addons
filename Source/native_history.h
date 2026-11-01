#pragma once
#include "meter_history_io.h"
#include "history_writer.h"
#include <ctime>

class DungeonHistory {
    struct Active {
        NativeMeter meter;
        DungeonRecord record;
        std::string family;
        std::map<std::string,uint32_t> seeds;
        std::vector<uint32_t> members;
        uint32_t self = 0;
        bool damage = false;
        bool changed = true;
        std::shared_ptr<const DungeonRecord> snapshot;
    };
    std::unique_ptr<Active> active;
    std::vector<std::shared_ptr<const DungeonRecord>> saved;
    std::unique_ptr<MeterPacket> scratch = std::make_unique<MeterPacket>();
    bool inside = false, dirty = false, urgent = false;
    uint64_t nextId = 1, savedAt = 0;
    std::filesystem::path file;
    std::string status;
    std::unique_ptr<HistoryWriter<DungeonRecord>> writer;

    static void Text(char* out,size_t capacity,const std::string& text) { std::snprintf(out,capacity,"%s",text.c_str()); }
    void Current(uint64_t now) {
        active->meter.Snapshot(*scratch,now);
        active->record.view=scratch->views[1];
        active->record.taken=scratch->views[3];
        active->record.ended=std::max(active->record.started,static_cast<int64_t>(std::time(nullptr)));
    }
public:
    DungeonHistory() = default;
    DungeonHistory(const DungeonHistory&) = delete;
    DungeonHistory& operator=(const DungeonHistory&) = delete;
    ~DungeonHistory() = default;
    void Open(const std::filesystem::path& path) {
        writer.reset();
        file=path;
        saved.clear(); active.reset(); inside=dirty=urgent=false; nextId=1; savedAt=0; status.clear();
        writer=std::make_unique<HistoryWriter<DungeonRecord>>(path,MeterHistoryIO::Magic);
        if (!writer->Available()) status="History saving unavailable or in use by another client; this session stays in memory.";
        std::vector<DungeonRecord> entries;
        std::string readError;
        if (!MeterHistoryIO::Read(path,entries,readError)) { status=readError; return; }
        for (auto& record:entries) {
            if (record.flags&1) { record.flags=(record.flags&4)|2; Text(record.ending,sizeof(record.ending),"Previous session checkpoint"); }
            nextId=std::max(nextId,record.id+1);
        }
        saved.reserve(20);
        for (auto& record:entries) saved.push_back(std::make_shared<const DungeonRecord>(std::move(record)));
    }
    void Finish(const char* reason,uint64_t now) {
        if (active && active->damage) {
            active->meter.Close(); Current(now);
            active->record.flags &= ~1u;
            Text(active->record.ending,sizeof(active->record.ending),reason);
            saved.insert(saved.begin(),std::make_shared<const DungeonRecord>(active->record));
            if (saved.size()>20) saved.resize(20);
            dirty=urgent=true;
        }
        active.reset(); inside=false;
    }
    void Pause(bool incomplete=false) {
        if (active) {
            active->meter.Close();
            if (incomplete && !(active->record.flags&2)) { active->record.flags |= 2; active->changed=true; }
            if (inside) { dirty=urgent=true; active->changed=true; }
        }
        inside=false;
    }
    void LeaveZone() { Pause(active && active->members.size()>1); }
    void Context(const std::string& zone,const std::string& family,const std::string& title,uint32_t seed,uint32_t self,const std::vector<MeterMember>& roster,uint64_t now) {
        std::vector<uint32_t> ids;
        for (const auto& member:roster) ids.push_back(member.id);
        std::sort(ids.begin(),ids.end());
        if (active && (active->self!=self || active->members!=ids)) Finish("Character or party changed",now);
        if (family.empty()) { LeaveZone(); return; }
        if (active && active->family!=family) Finish("Entered another dungeon",now);
        if (active) {
            const auto found=active->seeds.find(zone);
            if (found!=active->seeds.end() && found->second!=seed) Finish("Instance renewed",now);
        }
        if (!active) {
            active=std::make_unique<Active>();
            active->family=family; active->self=self; active->members=ids;
            active->record.id=nextId++;
            active->record.started=std::time(nullptr); active->record.ended=active->record.started;
            active->record.flags=1;
            Text(active->record.title,sizeof(active->record.title),title);
            Text(active->record.ending,sizeof(active->record.ending),"In progress");
        }
        if (active->seeds.size()>=1024 && active->seeds.find(zone)==active->seeds.end()) { Pause(true); return; }
        active->seeds[zone]=seed;
        const bool ready=active->meter.Roster(self,roster);
        if (inside!=ready) active->changed=true;
        inside=ready;
    }
    void Hit(const MeterHit& hit,uint64_t now) {
        if (!inside || !active) return;
        if (active->meter.Hit(hit,now)) { active->damage=true; active->changed=true; dirty=true; }
        if (!active->meter.Enabled()) { active->record.flags |= 2; active->changed=true; }
    }
    void Share(DungeonHistorySnapshot& result,uint64_t now) {
        result.records.clear();
        result.records.reserve(20);
        if (writer && writer->Available() && writer->Error()!=ERROR_SUCCESS && status.empty()) status="Dungeon history could not be saved.";
        Text(result.status,sizeof(result.status),status);
        if (active && active->damage) {
            if (!active->snapshot || active->changed) {
                Current(now);
                Text(active->record.ending,sizeof(active->record.ending),inside ? "In progress" : "Outside dungeon");
                active->snapshot=std::make_shared<const DungeonRecord>(active->record);
                active->changed=false;
            }
            result.records.push_back(active->snapshot);
        }
        for (const auto& record:saved) {
            if (result.records.size()==20) break;
            result.records.push_back(record);
        }
    }
    void Snapshot(DungeonHistoryView& result,uint64_t now) {
        DungeonHistorySnapshot shared;
        Share(shared,now);
        result.records.clear(); result.records.reserve(shared.records.size());
        Text(result.status,sizeof(result.status),shared.status);
        for (const auto& record:shared.records) result.records.push_back(*record);
    }
    bool WaitForSave(DWORD milliseconds=5000) const { return writer && writer->Wait(milliseconds); }
    void Save(uint64_t now) {
        if (!dirty || file.empty() || !writer || !writer->Available() || (!urgent && now>=savedAt && now-savedAt<30000) || !status.empty()) return;
        DungeonHistorySnapshot snapshot;
        Share(snapshot,now);
        if (!status.empty() || !writer->Queue(std::move(snapshot.records))) {
            status="Dungeon history could not be saved."; return;
        }
        dirty=urgent=false; savedAt=now;
    }
};
