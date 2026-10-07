#pragma once
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace Leaderboard {
constexpr size_t BodyLimit = 128 * 1024;
enum class Feed : unsigned { Level, Gold, Played, Pvp, Week, Count };
enum class Category : unsigned { Level, Gold, Played, Wins, Rating };
struct Row {
    std::string name, characterClass;
    unsigned level = 0, rank = 0;
    std::optional<uint64_t> gold, played, reached, wins, rating;
};
struct Board {
    std::vector<Row> rows;
    uint64_t generated = 0;
    bool stale = false;
};

namespace Json {
struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    std::string text;
    std::vector<Value> array;
    std::map<std::string,Value> object;
    const Value* Find(const char* key) const {
        const auto it = object.find(key);
        return type == Type::Object && it != object.end() ? &it->second : nullptr;
    }
};
class Reader {
    std::string_view source;
    size_t cursor = 0, nodes = 0;
    [[noreturn]] static void Fail() { throw std::runtime_error("Invalid leaderboard response."); }
    void Space() { while (cursor < source.size() && (source[cursor]==' ' || source[cursor]=='\n' || source[cursor]=='\r' || source[cursor]=='\t')) ++cursor; }
    bool Eat(char c) { Space(); if (cursor < source.size() && source[cursor]==c) { ++cursor; return true; } return false; }
    unsigned Hex() {
        if (source.size()-cursor < 4) Fail();
        unsigned value = 0;
        for (unsigned i=0;i<4;++i) {
            const char c = source[cursor++];
            const unsigned digit = c>='0' && c<='9' ? c-'0' : c>='a' && c<='f' ? c-'a'+10 : c>='A' && c<='F' ? c-'A'+10 : 16;
            if (digit==16) Fail();
            value=value*16+digit;
        }
        return value;
    }
    std::string String() {
        if (!Eat('"')) Fail();
        std::string out;
        while (cursor < source.size()) {
            unsigned c = static_cast<unsigned char>(source[cursor++]);
            if (c=='"') return out;
            if (c<32) Fail();
            if (c=='\\') {
                if (cursor==source.size()) Fail();
                c=static_cast<unsigned char>(source[cursor++]);
                if (c=='u') {
                    c=Hex();
                    if (c>=0xd800 && c<=0xdbff) {
                        if (source.size()-cursor<6 || source[cursor++]!='\\' || source[cursor++]!='u') Fail();
                        const unsigned low=Hex();
                        if (low<0xdc00 || low>0xdfff) Fail();
                        c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
                    } else if (c>=0xdc00 && c<=0xdfff) Fail();
                    if (c>=0x10000) { out+=static_cast<char>(0xf0|(c>>18)); out+=static_cast<char>(0x80|((c>>12)&63)); out+=static_cast<char>(0x80|((c>>6)&63)); out+=static_cast<char>(0x80|(c&63)); }
                    else if (c>=0x800) { out+=static_cast<char>(0xe0|(c>>12)); out+=static_cast<char>(0x80|((c>>6)&63)); out+=static_cast<char>(0x80|(c&63)); }
                    else if (c>=0x80) { out+=static_cast<char>(0xc0|(c>>6)); out+=static_cast<char>(0x80|(c&63)); }
                    else out+=static_cast<char>(c);
                } else {
                    if (c=='b') c='\b'; else if (c=='f') c='\f'; else if (c=='n') c='\n'; else if (c=='r') c='\r'; else if (c=='t') c='\t';
                    else if (c!='"' && c!='\\' && c!='/') Fail();
                    out+=static_cast<char>(c);
                }
            } else if (c<128) out+=static_cast<char>(c);
            else {
                const unsigned first=c;
                const unsigned count=c>=0xc2 && c<=0xdf ? 1 : c>=0xe0 && c<=0xef ? 2 : c>=0xf0 && c<=0xf4 ? 3 : 0;
                if (!count || source.size()-cursor<count) Fail();
                out+=static_cast<char>(c);
                unsigned code=c&((1u<<(6-count))-1);
                for (unsigned i=0;i<count;++i) {
                    c=static_cast<unsigned char>(source[cursor++]);
                    if ((c&0xc0)!=0x80) Fail();
                    code=(code<<6)|(c&63); out+=static_cast<char>(c);
                }
                if ((count==2 && code<0x800) || (count==3 && code<0x10000) || code>0x10ffff || (code>=0xd800 && code<=0xdfff) || first<0xc2) Fail();
            }
            if (out.size()>512) Fail();
        }
        Fail();
    }
    Value Read(unsigned depth) {
        if (depth>8 || ++nodes>8192) Fail();
        Space(); if (cursor==source.size()) Fail();
        Value result;
        if (source[cursor]=='"') { result.type=Value::Type::String; result.text=String(); }
        else if (Eat('{')) {
            result.type=Value::Type::Object;
            if (Eat('}')) return result;
            do {
                auto key=String(); if (!Eat(':') || result.object.size()>=64) Fail();
                if (!result.object.emplace(std::move(key),Read(depth+1)).second) Fail();
            } while (Eat(','));
            if (!Eat('}')) Fail();
        } else if (Eat('[')) {
            result.type=Value::Type::Array;
            if (Eat(']')) return result;
            do { if (result.array.size()>=512) Fail(); result.array.push_back(Read(depth+1)); } while (Eat(','));
            if (!Eat(']')) Fail();
        } else {
            for (const char* literal : {"true","false","null"}) {
                const std::string_view word(literal);
                if (source.substr(cursor,word.size())==word) {
                    result.type=word=="null" ? Value::Type::Null : Value::Type::Bool;
                    result.text=word; cursor+=word.size(); return result;
                }
            }
            const size_t start=cursor;
            if (source[cursor]=='-') ++cursor;
            if (cursor==source.size()) Fail();
            if (source[cursor]=='0') ++cursor;
            else {
                if (source[cursor]<'1' || source[cursor]>'9') Fail();
                while (cursor<source.size() && source[cursor]>='0' && source[cursor]<='9') ++cursor;
            }
            if (cursor<source.size() && source[cursor]=='.') {
                const size_t begin=++cursor;
                while (cursor<source.size() && source[cursor]>='0' && source[cursor]<='9') ++cursor;
                if (begin==cursor) Fail();
            }
            if (cursor<source.size() && (source[cursor]=='e' || source[cursor]=='E')) {
                ++cursor;
                if (cursor<source.size() && (source[cursor]=='+' || source[cursor]=='-')) ++cursor;
                const size_t begin=cursor;
                while (cursor<source.size() && source[cursor]>='0' && source[cursor]<='9') ++cursor;
                if (begin==cursor) Fail();
            }
            if (cursor-start>32) Fail();
            result.type=Value::Type::Number; result.text=source.substr(start,cursor-start);
        }
        return result;
    }
public:
    explicit Reader(std::string_view input) : source(input) {}
    Value Parse() { if (source.empty() || source.size()>BodyLimit) Fail(); auto value=Read(0); Space(); if (cursor!=source.size()) Fail(); return value; }
};
inline uint64_t Integer(const Value& value,uint64_t limit=INT64_MAX) {
    uint64_t number=0;
    const auto parsed=std::from_chars(value.text.data(),value.text.data()+value.text.size(),number);
    if (value.type!=Value::Type::Number || parsed.ec!=std::errc{} || parsed.ptr!=value.text.data()+value.text.size() || number>limit) throw std::runtime_error("Invalid leaderboard number.");
    return number;
}
inline std::optional<uint64_t> Number(const Value& object,const char* key,uint64_t limit=INT64_MAX) {
    const auto* field=object.Find(key);
    return !field || field->type==Value::Type::Null ? std::nullopt : std::optional<uint64_t>(Integer(*field,limit));
}
inline std::string Text(const Value& object,const char* key,size_t maximum,bool required) {
    const auto* field=object.Find(key);
    if (!field && !required) return "";
    if (!field || field->type!=Value::Type::String || field->text.empty() || field->text.size()>maximum ||
        !std::all_of(field->text.begin(),field->text.end(),[](unsigned char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c==' '; })) throw std::runtime_error("Invalid leaderboard text.");
    return field->text;
}
}

inline Board Parse(std::string_view input,Feed feed) {
    const auto root=Json::Reader(input).Parse();
    const auto* rows=root.Find("rows");
    const auto* stale=root.Find("stale");
    const auto generated=Json::Number(root,"generated_at",4102444800ULL);
    if (!rows || rows->type!=Json::Value::Type::Array || rows->array.size()>100 || !stale || stale->type!=Json::Value::Type::Bool || !generated || !*generated) throw std::runtime_error("Unsupported leaderboard response.");
    Board board; board.generated=*generated; board.stale=stale->text=="true";
    std::set<std::string> names;
    for (const auto& value:rows->array) {
        Row row; row.name=Json::Text(value,"name",64,true); row.characterClass=Json::Text(value,"class",24,false);
        auto key=row.name; for (auto& c:key) if (c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
        if (!names.insert(key).second) throw std::runtime_error("Duplicate leaderboard player.");
        row.level=static_cast<unsigned>(Json::Number(value,"level",100).value_or(0));
        row.gold=Json::Number(value,"gold"); row.played=Json::Number(value,"played_seconds");
        row.reached=Json::Number(value,"leveled_at",4102444800ULL);
        row.wins=Json::Number(value,"wins"); row.rating=Json::Number(value,"rating");
        row.rank=static_cast<unsigned>(board.rows.size()+1);
        if ((feed==Feed::Level && !row.level) || (feed==Feed::Gold && !row.gold) || (feed==Feed::Played && !row.played)) throw std::runtime_error("Missing leaderboard values.");
        board.rows.push_back(std::move(row));
    }
    return board;
}
inline std::optional<uint64_t> Metric(const Row& row,Category category) {
    switch (category) {
    case Category::Level: return row.reached;
    case Category::Gold: return row.gold;
    case Category::Played: return row.played;
    case Category::Wins: return row.wins;
    default: return row.rating;
    }
}
inline std::vector<Row> View(const Board& board,Category category) {
    std::vector<Row> rows;
    for (const auto& row:board.rows) if (category!=Category::Level || row.level==100) rows.push_back(row);
    if (category==Category::Wins || category==Category::Rating) {
        std::stable_sort(rows.begin(),rows.end(),[category](const Row& a,const Row& b) { return Metric(a,category)>Metric(b,category); });
        for (size_t i=0;i<rows.size();++i) {
            const auto value=Metric(rows[i],category);
            rows[i].rank=!value ? 0 : i && value==Metric(rows[i-1],category) ? rows[i-1].rank : static_cast<unsigned>(i+1);
        }
    }
    return rows;
}
inline std::string Number(uint64_t value) {
    std::string out=std::to_string(value);
    for (int i=static_cast<int>(out.size())-3;i>0;i-=3) out.insert(static_cast<size_t>(i),",");
    return out;
}
inline std::string Duration(uint64_t value) {
    return Number(value/86400)+"d "+std::to_string(value/3600%24)+"h "+std::to_string(value/60%60)+"m";
}
}
