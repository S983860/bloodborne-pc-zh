// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace VideoCore::HdAssets {
inline std::uint64_t Fingerprint(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash=0xcbf29ce484222325ull;
    for(auto value:bytes)hash=(hash^value)*0x100000001b3ull;
    return hash;
}
inline bool Filename(std::string_view name) {
    if(name.size()<5 || name.size()>100 || !name.ends_with(".dds"))return false;
    for(char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
    return name.find("..") == std::string_view::npos;
}
struct Dds { std::uint32_t width=0,height=0,levels=0; std::span<const std::uint8_t> blocks; };
inline bool ReadDds(std::span<const std::uint8_t> data,Dds& out) {
    out={};
    if(data.size()<148 || std::memcmp(data.data(),"DDS ",4))return false;
    const auto u32=[&](std::size_t at){std::uint32_t value;std::memcpy(&value,data.data()+at,4);return value;};
    const auto w=u32(16),h=u32(12),levels=u32(28);
    if(u32(4)!=124 || u32(76)!=32 || !(u32(80)&4) || u32(84)!=0x30315844 ||
       u32(128)!=98 || u32(132)!=3 || u32(136)!=0 || u32(140)!=1 || levels<1 || levels>13 ||
       w<32 || h<32 || w>4096 || h>4096 || (w%4) || (h%4))return false;
    std::uint64_t size=0;auto mw=w,mh=h;
    for(std::uint32_t mip=0;mip<levels;++mip){
        size+=std::uint64_t((mw+3)/4)*((mh+3)/4)*16;
        if(mw==1 && mh==1 && mip+1!=levels)return false;
        mw=mw>1?mw/2:1;mh=mh>1?mh/2:1;
    }
    if(data.size()!=148+size)return false;
    out={w,h,levels,data.subspan(148)};return true;
}
} // namespace VideoCore::HdAssets
