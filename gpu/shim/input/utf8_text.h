// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

// Menu row storage is UTF-16, whereas labels and SDL key names are UTF-8.
// Never split a surrogate pair or write past the row's fixed-size buffer.
template<std::size_t N> void pc_utf8_to_utf16(char16_t (&out)[N],const char* source) {
    static_assert(N>0);
    const std::string_view text=source?source:"";
    std::size_t used=0;
    for(std::size_t i=0;i<text.size();) {
        const auto first=static_cast<unsigned char>(text[i]);
        std::uint32_t cp=first;std::size_t length=1;
        if(first>=0xc2 && first<=0xdf){cp=first&31;length=2;}
        else if(first>=0xe0 && first<=0xef){cp=first&15;length=3;}
        else if(first>=0xf0 && first<=0xf4){cp=first&7;length=4;}
        else if(first>=0x80)cp=0xfffd;
        bool valid=i+length<=text.size();
        for(std::size_t j=1;valid && j<length;++j) {
            const auto byte=static_cast<unsigned char>(text[i+j]);
            if((byte&0xc0)!=0x80)valid=false;
            else cp=(cp<<6)|(byte&63);
        }
        if(!valid || (length==2 && cp<0x80) || (length==3 && cp<0x800) ||
           (length==4 && cp<0x10000) || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) {
            cp=0xfffd;length=1;
        }
        const std::size_t count=cp>=0x10000?2:1;
        if(used+count>=N)break;
        if(count==2){cp-=0x10000;out[used++]=char16_t(0xd800+(cp>>10));out[used++]=char16_t(0xdc00+(cp&1023));}
        else out[used++]=char16_t(cp);
        i+=length;
    }
    out[used]=0;
}
