// SPDX-License-Identifier: GPL-3.0-or-later
#include "../gpu/shim/engine/minimap_geometry.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <chrono>
#include <thread>
#include <cstdio>
using namespace BbMinimap;
void write(const std::filesystem::path& p,bool corrupt=false){
 std::ofstream f(p,std::ios::binary);f.write("BBMAP002",8);std::uint32_t counts[]{2,0,2};f.write(reinterpret_cast<char*>(counts),12);
 Triangle roads[]={{{-10,0,-10},{10,0,-10},{10,0,10}},{{-10,0,-10},{10,0,10},{-10,0,10}}};
 if(corrupt)roads[0].a.x=std::numeric_limits<float>::quiet_NaN();
 Triangle walls[]={{{5,-1,-10},{5,4,-10},{5,4,10}},{{5,-1,-10},{5,4,10},{5,-1,10}}};
 f.write(reinterpret_cast<char*>(roads),sizeof roads);f.write(reinterpret_cast<char*>(walls),sizeof walls);
}
int main(int argc,char**argv){
 assert(ClassifyTeam(6)=='E'&&ClassifyTeam(23)=='E'&&ClassifyTeam(27)=='E'&&ClassifyTeam(29)=='E');
 assert(ClassifyTeam(26)=='N'&&ClassifyTeam(28)=='N'&&ClassifyTeam(8)=='N');
 assert(ClassifyTeam(2)=='F'&&ClassifyTeam(20)=='F');assert(ClassifyTeam(0)==0&&ClassifyTeam(30)==0&&ClassifyTeam(999)=='U');
 Segment cut;assert(SliceWall({{5,-1,-10},{5,4,-10},{5,4,10}},1,cut));
 assert(std::abs(cut.a.x-5)<1e-5f&&std::abs(cut.b.x-5)<1e-5f&&cut.a.y==1&&cut.b.y==1);
 assert(!SliceWall({{5,-1,-10},{5,4,-10},{5,4,10}},10,cut));
 assert(!SliceWall({{0,0,0},{1,0,0},{1,0,1}},0,cut));
 auto folder=std::filesystem::temp_directory_path()/("bb-minimap-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));std::filesystem::create_directories(folder);
 auto path=folder/"m24_01_00_00.bmap";write(path);std::string error;auto map=Load(path,error);assert(map&&error.empty());
 auto raster=Bake(*map,0x18010000,{0,0,0},30);assert(raster->roads==2&&raster->walls==2&&!raster->rects.empty());
 bool has_road=false,has_wall=false;for(auto r:raster->rects){assert(r.x0<r.x1&&r.y0<r.y1&&r.x1<=raster->resolution&&r.y1<=raster->resolution);has_road|=r.shade==2;has_wall|=r.shade==4;}assert(has_road&&has_wall);
 assert(Bake(*map,0x18010000,{0,20,0},30)->rects.empty());
 write(folder/"nan.bmap",true);assert(!Load(folder/"nan.bmap",error));
 {std::ofstream f(folder/"short.bmap",std::ios::binary);f.write("BBMAP002",8);}assert(!Load(folder/"short.bmap",error));
 {std::ofstream f(folder/"count.bmap",std::ios::binary);f.write("BBMAP002",8);std::uint32_t n[]{0xffffffff,0,0};f.write(reinterpret_cast<char*>(n),12);}assert(!Load(folder/"count.bmap",error));
 assert(!Load(folder/"absent.bmap",error));
#ifdef _WIN32
 _putenv_s("BB_MINIMAP_ASSETS",folder.string().c_str());
#else
 setenv("BB_MINIMAP_ASSETS",folder.c_str(),1);
#endif
 Request(0x18010000,{0,0,0},30);Request(0x18010000,{24,0,-16},45);
 std::shared_ptr<const Raster> ready;for(int i=0;i<500;++i){ready=Ready(0x18010000);if(ready&&ready->center.x==24&&ready->center.z==-16)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 assert(ready&&ready->center.x==24&&ready->center.z==-16);assert(!Ready(0x21000000));
 if(argc>1){auto actual=Load(std::filesystem::path(reinterpret_cast<const char8_t*>(argv[1])),error);assert(actual);
  auto r=Bake(*actual,0x18010000,{-176,-44,64},45);assert(r->roads>0&&r->walls>0&&!r->rects.empty());
  std::printf("Actual map: roads=%zu wall-sections=%zu rectangles=%zu resolution=%d bake=%.2fms\n",r->roads,r->walls,r->rects.size(),r->resolution,r->build_ms);
  if(argc>2){std::ofstream f(std::filesystem::path(reinterpret_cast<const char8_t*>(argv[2])));f<<r->resolution<<'\n';for(auto q:r->rects)f<<q.x0<<' '<<q.y0<<' '<<q.x1<<' '<<q.y1<<' '<<int(q.shade)<<'\n';}
 }
 const auto checked=std::filesystem::weakly_canonical(folder);
 assert(checked.parent_path()==std::filesystem::weakly_canonical(std::filesystem::temp_directory_path())&&checked.filename().string().starts_with("bb-minimap-"));
 std::filesystem::remove_all(checked);std::puts("PASS: live team classification, wall slicing, floor separation, geometry validation, asynchronous coalescing and raster cache");
}
