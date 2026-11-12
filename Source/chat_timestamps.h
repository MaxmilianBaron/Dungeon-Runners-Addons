#pragma once
#include <cstdint>
#include <cstring>

struct ChatTimestampText {
    static constexpr size_t Capacity = 16384;
    static constexpr size_t PrefixLength = 8;
    uint32_t length = 0;
    char text[Capacity];

    bool Assign(unsigned hour,unsigned minute,const char* message,size_t size) {
        if (hour > 23 || minute > 59 || !message || !size || size > Capacity-PrefixLength-1 ||
            std::memchr(message,0,size)) return false;
        text[0]='[';
        text[1]=char('0'+hour/10); text[2]=char('0'+hour%10);
        text[3]=':';
        text[4]=char('0'+minute/10); text[5]=char('0'+minute%10);
        text[6]=']'; text[7]=' ';
        std::memcpy(text+PrefixLength,message,size);
        length=static_cast<uint32_t>(PrefixLength+size);
        text[length]=0;
        return true;
    }
};
