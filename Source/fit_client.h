#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Reads https://fit.thetownstons.com/api/fit/character/<name> (thetownstons Fit).
// Lookup is by a typed name or another party member. It does not load the logged-in character.

struct FitLine {
    char text[120];
    int tone; // 0 body, 1 gold, 2 muted, 3 magic, 4 rare, 5 mythic
};

struct FitView {
    char status[160];
    bool ready;
    int count;
    FitLine lines[80];
};

namespace {

constexpr int kFitLines = 80;

struct FitState {
    char query[65];
    char pending[65];
    char others[7][65];
    int otherCount;
    bool typing;
    FitView view;
    volatile LONG busy;
    volatile LONG generation;
    CRITICAL_SECTION gate;
    INIT_ONCE gateOnce;
};

FitState& FitStateSlot() {
    static FitState state{};
    return state;
}

BOOL CALLBACK FitInitGate(PINIT_ONCE, PVOID parameter, PVOID*) {
    InitializeCriticalSection(static_cast<CRITICAL_SECTION*>(parameter));
    return TRUE;
}

void FitLock() {
    auto& state = FitStateSlot();
    InitOnceExecuteOnce(&state.gateOnce, FitInitGate, &state.gate, nullptr);
    EnterCriticalSection(&state.gate);
}

struct FitGuard {
    FitGuard() { FitLock(); }
    ~FitGuard() { LeaveCriticalSection(&FitStateSlot().gate); }
};

bool FitNameOk(const char* name, char* out, size_t outSize) {
    if (!name || !out || outSize < 2) return false;
    size_t n = 0;
    while (name[n] == ' ') ++n;
    size_t end = n;
    while (name[end] && end < 64) {
        const unsigned char c = static_cast<unsigned char>(name[end]);
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ' ')) return false;
        ++end;
    }
    if (name[end]) return false;
    while (end > n && name[end - 1] == ' ') --end;
    if (end <= n || end - n >= outSize) return false;
    memcpy(out, name + n, end - n);
    out[end - n] = 0;
    return true;
}

void FitPush(FitView& view, const char* text, int tone) {
    if (!text || !text[0] || view.count >= kFitLines) return;
    auto& line = view.lines[view.count++];
    line.tone = tone;
    size_t i = 0;
    for (; text[i] && i + 1 < sizeof(line.text); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        line.text[i] = (c < 32 || c > 126) ? ' ' : static_cast<char>(c);
    }
    line.text[i] = 0;
}

int FitTone(const char* quality) {
    if (!quality) return 0;
    if (strstr(quality, "mythic") || strstr(quality, "Mythic")) return 5;
    if (strstr(quality, "unique") || strstr(quality, "Unique") || strstr(quality, "rare") || strstr(quality, "Rare")) return 4;
    if (strstr(quality, "magic") || strstr(quality, "Magic") || strstr(quality, "superior") || strstr(quality, "Superior")) return 3;
    return 0;
}

struct FitCursor {
    const char* p;
    const char* end;
};

void FitSkipWs(FitCursor& c) {
    while (c.p < c.end && (*c.p == ' ' || *c.p == '\t' || *c.p == '\r' || *c.p == '\n')) ++c.p;
}

bool FitString(FitCursor& c, char* out, size_t outSize) {
    FitSkipWs(c);
    if (c.p >= c.end || *c.p != '"' || outSize < 2) return false;
    ++c.p;
    size_t n = 0;
    while (c.p < c.end && *c.p != '"') {
        char ch = *c.p++;
        if (ch == '\\' && c.p < c.end) {
            const char esc = *c.p++;
            if (esc == 'n' || esc == 'r' || esc == 't') ch = ' ';
            else if (esc == 'u') { for (int i = 0; i < 4 && c.p < c.end; ++i) ++c.p; ch = '?'; }
            else ch = esc;
        }
        if (n + 1 < outSize) out[n++] = (static_cast<unsigned char>(ch) < 32) ? ' ' : ch;
    }
    if (c.p >= c.end || *c.p != '"') return false;
    ++c.p;
    out[n] = 0;
    return true;
}

void FitSkipString(FitCursor& c) {
    char dump[4];
    if (c.p < c.end && *c.p == '"') {
        ++c.p;
        while (c.p < c.end && *c.p != '"') {
            if (*c.p == '\\' && c.p + 1 < c.end) c.p += 2;
            else ++c.p;
        }
        if (c.p < c.end) ++c.p;
        (void)dump;
    }
}

void FitSkipValue(FitCursor& c);

void FitSkipContainer(FitCursor& c, char open, char close) {
    if (c.p >= c.end || *c.p != open) return;
    ++c.p;
    int depth = 1;
    while (c.p < c.end && depth) {
        if (*c.p == '"') { FitSkipString(c); continue; }
        if (*c.p == open) ++depth;
        else if (*c.p == close) --depth;
        if (depth) ++c.p;
    }
    if (c.p < c.end && *c.p == close) ++c.p;
}

void FitSkipValue(FitCursor& c) {
    FitSkipWs(c);
    if (c.p >= c.end) return;
    if (*c.p == '"') { FitSkipString(c); return; }
    if (*c.p == '{') { FitSkipContainer(c, '{', '}'); return; }
    if (*c.p == '[') { FitSkipContainer(c, '[', ']'); return; }
    while (c.p < c.end && *c.p != ',' && *c.p != '}' && *c.p != ']' && *c.p != ' ' && *c.p != '\n' && *c.p != '\r' && *c.p != '\t') ++c.p;
}

bool FitKeyIs(const char* key, const char* name) { return strcmp(key, name) == 0; }

bool FitNumber(FitCursor& c, int& value) {
    FitSkipWs(c);
    if (c.p >= c.end || ((*c.p < '0' || *c.p > '9') && *c.p != '-')) return false;
    char buf[16]{};
    size_t n = 0;
    if (*c.p == '-' && n + 1 < sizeof(buf)) buf[n++] = *c.p++;
    while (c.p < c.end && *c.p >= '0' && *c.p <= '9' && n + 1 < sizeof(buf)) buf[n++] = *c.p++;
    value = atoi(buf);
    return n > 0;
}

void FitParseAttributes(FitCursor& c, FitView& view) {
    FitSkipWs(c);
    if (c.p >= c.end || *c.p != '[') { FitSkipValue(c); return; }
    ++c.p;
    while (c.p < c.end) {
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ']') { ++c.p; return; }
        if (c.p >= c.end || *c.p != '{') { FitSkipValue(c); break; }
        ++c.p;
        char label[48]{};
        int value = 0;
        bool gotValue = false;
        while (c.p < c.end) {
            FitSkipWs(c);
            if (c.p < c.end && *c.p == '}') { ++c.p; break; }
            char key[32]{};
            if (!FitString(c, key, sizeof(key))) break;
            FitSkipWs(c);
            if (c.p < c.end && *c.p == ':') ++c.p;
            if (FitKeyIs(key, "label")) FitString(c, label, sizeof(label));
            else if (FitKeyIs(key, "value")) gotValue = FitNumber(c, value);
            else FitSkipValue(c);
            FitSkipWs(c);
            if (c.p < c.end && *c.p == ',') ++c.p;
        }
        if (label[0] && gotValue) {
            char row[80];
            snprintf(row, sizeof(row), "%s  %d", label, value);
            FitPush(view, row, 1);
        }
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ',') ++c.p;
    }
}

void FitParseLines(FitCursor& c, FitView& view) {
    FitSkipWs(c);
    if (c.p >= c.end || *c.p != '[') { FitSkipValue(c); return; }
    ++c.p;
    int shown = 0;
    while (c.p < c.end) {
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ']') { ++c.p; return; }
        char text[96]{};
        if (!FitString(c, text, sizeof(text))) { FitSkipValue(c); break; }
        if (shown < 4 && text[0]) {
            char row[110];
            snprintf(row, sizeof(row), "    %s", text);
            FitPush(view, row, 2);
            ++shown;
        }
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ',') ++c.p;
    }
}

void FitParseGear(FitCursor& c, FitView& view) {
    FitSkipWs(c);
    if (c.p >= c.end || *c.p != '[') { FitSkipValue(c); return; }
    ++c.p;
    FitPush(view, "Gear", 1);
    while (c.p < c.end) {
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ']') { ++c.p; return; }
        if (c.p >= c.end || *c.p != '{') { FitSkipValue(c); break; }
        ++c.p;
        char slot[32]{}, name[72]{}, quality[24]{};
        FitCursor lines = c;
        bool hasLines = false;
        while (c.p < c.end) {
            FitSkipWs(c);
            if (c.p < c.end && *c.p == '}') { ++c.p; break; }
            char key[32]{};
            if (!FitString(c, key, sizeof(key))) break;
            FitSkipWs(c);
            if (c.p < c.end && *c.p == ':') ++c.p;
            if (FitKeyIs(key, "slotLabel")) FitString(c, slot, sizeof(slot));
            else if (FitKeyIs(key, "name")) FitString(c, name, sizeof(name));
            else if (FitKeyIs(key, "quality")) FitString(c, quality, sizeof(quality));
            else if (FitKeyIs(key, "lines")) { lines = c; hasLines = true; FitSkipValue(c); }
            else FitSkipValue(c);
            FitSkipWs(c);
            if (c.p < c.end && *c.p == ',') ++c.p;
        }
        if (name[0]) {
            char row[110];
            snprintf(row, sizeof(row), "%s  %s", slot[0] ? slot : "Item", name);
            FitPush(view, row, FitTone(quality));
            if (hasLines) FitParseLines(lines, view);
        }
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ',') ++c.p;
    }
}

void FitParseTray(FitCursor& c, FitView& view) {
    FitSkipWs(c);
    if (c.p >= c.end || *c.p != '[') { FitSkipValue(c); return; }
    ++c.p;
    FitPush(view, "Skills", 1);
    while (c.p < c.end) {
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ']') { ++c.p; return; }
        if (c.p >= c.end || *c.p != '{') { FitSkipValue(c); break; }
        ++c.p;
        char key[8]{}, name[72]{};
        int level = 0;
        bool gotLevel = false;
        while (c.p < c.end) {
            FitSkipWs(c);
            if (c.p < c.end && *c.p == '}') { ++c.p; break; }
            char field[32]{};
            if (!FitString(c, field, sizeof(field))) break;
            FitSkipWs(c);
            if (c.p < c.end && *c.p == ':') ++c.p;
            if (FitKeyIs(field, "key")) FitString(c, key, sizeof(key));
            else if (FitKeyIs(field, "name")) FitString(c, name, sizeof(name));
            else if (FitKeyIs(field, "level")) gotLevel = FitNumber(c, level);
            else FitSkipValue(c);
            FitSkipWs(c);
            if (c.p < c.end && *c.p == ',') ++c.p;
        }
        if (name[0]) {
            char row[110];
            if (gotLevel) snprintf(row, sizeof(row), "%s  %s   rank %d", key[0] ? key : "?", name, level);
            else snprintf(row, sizeof(row), "%s  %s", key[0] ? key : "?", name);
            FitPush(view, row, 0);
        }
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ',') ++c.p;
    }
}

bool FitPresent(const char* body, size_t size, FitView& view) {
    FitCursor c{body, body + size};
    FitSkipWs(c);
    if (c.p >= c.end || *c.p != '{') return false;
    ++c.p;
    char name[65]{}, classLabel[48]{};
    int level = 0;
    bool gotLevel = false;
    FitCursor attributes{}, gear{}, tray{};
    bool hasAttributes = false, hasGear = false, hasTray = false;
    while (c.p < c.end) {
        FitSkipWs(c);
        if (c.p < c.end && *c.p == '}') break;
        char key[32]{};
        if (!FitString(c, key, sizeof(key))) return false;
        FitSkipWs(c);
        if (c.p >= c.end || *c.p != ':') return false;
        ++c.p;
        if (FitKeyIs(key, "name")) { if (!FitString(c, name, sizeof(name))) return false; }
        else if (FitKeyIs(key, "classLabel")) { if (!FitString(c, classLabel, sizeof(classLabel))) return false; }
        else if (FitKeyIs(key, "level")) gotLevel = FitNumber(c, level);
        else if (FitKeyIs(key, "attributes")) { attributes = c; hasAttributes = true; FitSkipValue(c); }
        else if (FitKeyIs(key, "gear")) { gear = c; hasGear = true; FitSkipValue(c); }
        else if (FitKeyIs(key, "tray")) { tray = c; hasTray = true; FitSkipValue(c); }
        else FitSkipValue(c);
        FitSkipWs(c);
        if (c.p < c.end && *c.p == ',') ++c.p;
    }
    if (!name[0]) return false;
    char title[120];
    if (gotLevel && classLabel[0]) snprintf(title, sizeof(title), "%s   level %d   %s", name, level, classLabel);
    else if (gotLevel) snprintf(title, sizeof(title), "%s   level %d", name, level);
    else snprintf(title, sizeof(title), "%s", name);
    FitPush(view, title, 1);
    if (hasAttributes) FitParseAttributes(attributes, view);
    if (hasGear) FitParseGear(gear, view);
    if (hasTray) FitParseTray(tray, view);
    view.ready = view.count > 0;
    return view.ready;
}

using WinHttpOpenFn = void* (__stdcall*)(const wchar_t*, unsigned long, const wchar_t*, const wchar_t*, unsigned long);
using WinHttpConnectFn = void* (__stdcall*)(void*, const wchar_t*, unsigned short, unsigned long);
using WinHttpOpenRequestFn = void* (__stdcall*)(void*, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*, const wchar_t**, unsigned long);
using WinHttpSendRequestFn = int (__stdcall*)(void*, const wchar_t*, unsigned long, void*, unsigned long, unsigned long, uintptr_t);
using WinHttpReceiveResponseFn = int (__stdcall*)(void*, void*);
using WinHttpQueryHeadersFn = int (__stdcall*)(void*, unsigned long, const wchar_t*, void*, unsigned long*, unsigned long*);
using WinHttpReadDataFn = int (__stdcall*)(void*, void*, unsigned long, unsigned long*);
using WinHttpCloseHandleFn = int (__stdcall*)(void*);
using WinHttpSetTimeoutsFn = int (__stdcall*)(void*, int, int, int, int);

void FitPublish(const FitView& view, LONG generation) {
    FitGuard guard;
    auto& state = FitStateSlot();
    if (generation != state.generation) return;
    state.view = view;
}

DWORD WINAPI FitFetchThread(void*) {
    char name[65]{};
    LONG generation = 0;
    {
        FitGuard guard;
        auto& state = FitStateSlot();
        generation = state.generation;
        memcpy(name, state.pending, sizeof(name));
    }
    FitView view{};
    snprintf(view.status, sizeof(view.status), "Loading %s from Fit...", name);

    HMODULE library = LoadLibraryW(L"winhttp.dll");
    auto open = library ? reinterpret_cast<WinHttpOpenFn>(GetProcAddress(library, "WinHttpOpen")) : nullptr;
    auto connect = library ? reinterpret_cast<WinHttpConnectFn>(GetProcAddress(library, "WinHttpConnect")) : nullptr;
    auto request = library ? reinterpret_cast<WinHttpOpenRequestFn>(GetProcAddress(library, "WinHttpOpenRequest")) : nullptr;
    auto send = library ? reinterpret_cast<WinHttpSendRequestFn>(GetProcAddress(library, "WinHttpSendRequest")) : nullptr;
    auto receive = library ? reinterpret_cast<WinHttpReceiveResponseFn>(GetProcAddress(library, "WinHttpReceiveResponse")) : nullptr;
    auto headers = library ? reinterpret_cast<WinHttpQueryHeadersFn>(GetProcAddress(library, "WinHttpQueryHeaders")) : nullptr;
    auto read = library ? reinterpret_cast<WinHttpReadDataFn>(GetProcAddress(library, "WinHttpReadData")) : nullptr;
    auto close = library ? reinterpret_cast<WinHttpCloseHandleFn>(GetProcAddress(library, "WinHttpCloseHandle")) : nullptr;
    auto timeouts = library ? reinterpret_cast<WinHttpSetTimeoutsFn>(GetProcAddress(library, "WinHttpSetTimeouts")) : nullptr;
    void* session = nullptr;
    void* connection = nullptr;
    void* handle = nullptr;
    std::string body;
    unsigned long status = 0;
    bool ok = open && connect && request && send && receive && headers && read && close;
    if (ok) {
        session = open(L"DungeonRunnersFit/1", 0, nullptr, nullptr, 0);
        ok = session != nullptr;
    }
    if (ok && timeouts) timeouts(session, 4000, 4000, 8000, 8000);
    if (ok) {
        connection = connect(session, L"fit.thetownstons.com", 443, 0);
        ok = connection != nullptr;
    }
    wchar_t path[160] = L"/api/fit/character/";
    size_t pathLen = wcslen(path);
    for (size_t i = 0; name[i] && pathLen + 4 < 158; ++i) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            path[pathLen++] = static_cast<wchar_t>(c);
        } else if (c == ' ') {
            path[pathLen++] = '%'; path[pathLen++] = '2'; path[pathLen++] = '0';
        }
    }
    path[pathLen] = 0;
    if (ok) {
        handle = request(connection, L"GET", path, nullptr, nullptr, nullptr, 0x00800000);
        ok = handle != nullptr;
    }
    if (ok) ok = send(handle, nullptr, 0, nullptr, 0, 0, 0) != 0;
    if (ok) ok = receive(handle, nullptr) != 0;
    if (ok) {
        unsigned long size = sizeof(status);
        unsigned long index = 0;
        ok = headers(handle, 19 | 0x20000000, nullptr, &status, &size, &index) != 0;
    }
    if (ok) {
        body.reserve(8192);
        for (;;) {
            char chunk[4096];
            unsigned long got = 0;
            if (!read(handle, chunk, sizeof(chunk), &got) || !got) break;
            if (body.size() + got > 524288) { ok = false; break; }
            body.append(chunk, chunk + got);
        }
    }
    if (close) {
        if (handle) close(handle);
        if (connection) close(connection);
        if (session) close(session);
    }
    if (library) FreeLibrary(library);

    view = {};
    if (!ok) snprintf(view.status, sizeof(view.status), "Fit request failed for %s.", name);
    else if (status == 404) snprintf(view.status, sizeof(view.status), "%s is not on the public Fit ladder.", name);
    else if (status != 200) snprintf(view.status, sizeof(view.status), "Fit returned HTTP %lu for %s.", status, name);
    else if (!FitPresent(body.data(), body.size(), view)) snprintf(view.status, sizeof(view.status), "Fit response for %s could not be read.", name);
    else view.status[0] = 0;
    FitPublish(view, generation);
    InterlockedExchange(&FitStateSlot().busy, 0);
    return 0;
}

void FitQuery(const char* name) {
    char clean[65]{};
    if (!FitNameOk(name, clean, sizeof(clean))) {
        FitGuard guard;
        auto& state = FitStateSlot();
        state.typing = false;
        state.view = {};
        snprintf(state.view.status, sizeof(state.view.status), "Type a character name. Letters, numbers, spaces, and underscores.");
        return;
    }
    if (InterlockedCompareExchange(&FitStateSlot().busy, 1, 0) != 0) return;
    LONG generation = 0;
    {
        FitGuard guard;
        auto& state = FitStateSlot();
        state.typing = false;
        memcpy(state.query, clean, sizeof(state.query));
        memcpy(state.pending, clean, sizeof(state.pending));
        generation = InterlockedIncrement(&state.generation);
        state.view = {};
        snprintf(state.view.status, sizeof(state.view.status), "Loading %s from Fit...", clean);
    }
    HANDLE thread = CreateThread(nullptr, 0, FitFetchThread, nullptr, 0, nullptr);
    if (!thread) {
        FitView failed{};
        snprintf(failed.status, sizeof(failed.status), "Could not start the Fit request.");
        FitPublish(failed, generation);
        InterlockedExchange(&FitStateSlot().busy, 0);
    } else CloseHandle(thread);
}

void FitSetOthers(const char* packed, int count) {
    FitGuard guard;
    auto& state = FitStateSlot();
    state.otherCount = 0;
    if (!packed || count <= 0) return;
    if (count > 7) count = 7;
    for (int i = 0; i < count; ++i) {
        char clean[65]{};
        if (!FitNameOk(packed + i * 65, clean, sizeof(clean))) continue;
        memcpy(state.others[state.otherCount], clean, 65);
        ++state.otherCount;
    }
}

bool FitTyping() {
    FitGuard guard;
    return FitStateSlot().typing;
}

void FitStopTyping() {
    FitGuard guard;
    FitStateSlot().typing = false;
}

void FitBeginTyping() {
    FitGuard guard;
    FitStateSlot().typing = true;
}

bool FitCapture(UINT message, WPARAM key) {
    bool lookup = false;
    char name[65]{};
    {
        FitGuard guard;
        auto& state = FitStateSlot();
        if (!state.typing) return false;
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
            if (key == VK_ESCAPE || key == VK_RETURN) {
                lookup = key == VK_RETURN;
                memcpy(name, state.query, sizeof(name));
                state.typing = false;
            } else if (key == VK_BACK) {
                const size_t n = strlen(state.query);
                if (n) state.query[n - 1] = 0;
            }
        } else if (message == WM_CHAR) {
            const unsigned c = static_cast<unsigned>(key);
            const size_t n = strlen(state.query);
            if (n + 1 < sizeof(state.query) && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '_')) {
                state.query[n] = static_cast<char>(c);
                state.query[n + 1] = 0;
            }
        } else if (message != WM_KEYUP) return false;
    }
    if (lookup) FitQuery(name);
    return true;
}

void FitSnapshot(FitView& view, char* query, int querySize, char others[7][65], int& otherCount, bool& typing) {
    FitGuard guard;
    const auto& state = FitStateSlot();
    view = state.view;
    if (query && querySize > 0) {
        const size_t n = strlen(state.query);
        const size_t cap = static_cast<size_t>(querySize - 1);
        const size_t copy = n < cap ? n : cap;
        memcpy(query, state.query, copy);
        query[copy] = 0;
    }
    otherCount = state.otherCount;
    for (int i = 0; i < state.otherCount; ++i) memcpy(others[i], state.others[i], 65);
    typing = state.typing;
}

} // namespace
