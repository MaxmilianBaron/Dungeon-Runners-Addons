#pragma once
#include <windows.h>
#include <algorithm>
#include <string>

class LoadoutNameInput {
    static bool High(wchar_t c) { return c>=0xd800 && c<=0xdbff; }
    static bool Low(wchar_t c) { return c>=0xdc00 && c<=0xdfff; }
    size_t Previous(size_t p) const { return p>1 && Low(text[p-1]) && High(text[p-2]) ? p-2 : p ? p-1 : 0; }
    size_t Next(size_t p) const { return p+1<text.size() && High(text[p]) && Low(text[p+1]) ? p+2 : std::min(p+1,text.size()); }
public:
    std::wstring text;
    size_t caret=0,anchor=0;
    wchar_t pending=0;
    static std::string Utf8(const std::wstring& value) {
        if (value.empty()) return {};
        for (size_t i=0;i<value.size();i++) {
            if (High(value[i])) { if (i+1>=value.size() || !Low(value[i+1])) return {}; ++i; }
            else if (Low(value[i])) return {};
        }
        const int length=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        if (length<=0) return {};
        std::string out(static_cast<size_t>(length),'\0');
        if (WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),out.data(),length,nullptr,nullptr)!=length) return {};
        return out;
    }
    void Open(const std::string& name) {
        const int length=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),nullptr,0);
        text.assign(static_cast<size_t>(std::max(0,length)),L'\0');
        if (length>0) MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,name.data(),static_cast<int>(name.size()),text.data(),length);
        caret=text.size(); anchor=0; pending=0;
    }
    bool Insert(const std::wstring& value) {
        if (value.empty()) return true;
        for (const auto c:value) if (c<32 || c==127) return false;
        const auto start=std::min(caret,anchor),end=std::max(caret,anchor);
        const auto next=text.substr(0,start)+value+text.substr(end);
        const auto encoded=Utf8(next);
        if (encoded.empty() || encoded.size()>80) return false;
        text=next; caret=anchor=start+value.size(); return true;
    }
    bool Character(wchar_t c) {
        if (High(c)) { pending=c; return true; }
        std::wstring value;
        if (Low(c)) { if (!pending) return false; value.push_back(pending); }
        pending=0; value.push_back(c); return Insert(value);
    }
    void Key(unsigned key,bool shift,bool control) {
        pending=0;
        if (control && key=='A') { anchor=0; caret=text.size(); return; }
        if (key==VK_BACK || key==VK_DELETE) {
            if (caret==anchor) { if (key==VK_BACK) anchor=Previous(caret); else anchor=Next(caret); }
            const auto start=std::min(caret,anchor);
            text.erase(start,std::max(caret,anchor)-start); caret=anchor=start;
        } else if (key==VK_HOME || key==VK_END || key==VK_LEFT || key==VK_RIGHT) {
            if (key==VK_HOME) caret=0;
            else if (key==VK_END) caret=text.size();
            else if (!shift && caret!=anchor) caret=key==VK_LEFT ? std::min(caret,anchor) : std::max(caret,anchor);
            else caret=key==VK_LEFT ? Previous(caret) : Next(caret);
            if (!shift) anchor=caret;
        }
    }
    std::string Name() const {
        const auto first=text.find_first_not_of(L' '),last=text.find_last_not_of(L' ');
        return first==std::wstring::npos ? std::string{} : Utf8(text.substr(first,last-first+1));
    }
};
