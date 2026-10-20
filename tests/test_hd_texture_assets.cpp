#include "video_core/texture_cache/hd_texture_assets.h"
#include <cassert>
#include <vector>
int main(){
    using namespace VideoCore::HdAssets;
    assert(Filename("MENU_Icon_00001.dds"));
    assert(!Filename("../wrong.dds"));assert(!Filename("a/b.dds"));assert(!Filename("test.png"));
    const std::uint8_t abc[]={'a','b','c'};assert(Fingerprint(abc)==0xe71fa2190541574bull);
    std::vector<std::uint8_t> bytes(148+1024);
    const auto put=[&](std::size_t offset,std::uint32_t value){std::memcpy(bytes.data()+offset,&value,4);};
    std::memcpy(bytes.data(),"DDS ",4);put(4,124);put(12,32);put(16,32);put(28,1);put(76,32);
    put(80,4);put(84,0x30315844);put(128,98);put(132,3);put(140,1);
    Dds result{};assert(ReadDds(bytes,result));assert(result.width==32 && result.blocks.size()==1024);
    put(16,8192);assert(!ReadDds(bytes,result));put(16,32);
    put(28,2);assert(!ReadDds(bytes,result));put(28,1);
    put(128,99);assert(!ReadDds(bytes,result));put(128,98);
    put(140,6);assert(!ReadDds(bytes,result));put(140,1);
    bytes.pop_back();assert(!ReadDds(bytes,result));
    bytes.resize(148+1280);put(28,2);assert(ReadDds(bytes,result));assert(result.levels==2);
    put(28,13);assert(!ReadDds(bytes,result));
    assert(!ReadDds({},result));
}
