// SPDX-License-Identifier: GPL-3.0-or-later
// Offline Havok navmesh + collision export; no SDK is shipped or required by the game.
#include "minimap_geometry.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
namespace BbMinimap {
namespace {
constexpr std::size_t max_bytes=64*1024*1024,max_refs=4000000;
constexpr float cell=16;
bool finite(Point p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z)&&std::abs(p.x)<100000&&std::abs(p.y)<100000&&std::abs(p.z)<100000;}
std::int64_t key(int x,int z){return static_cast<std::int64_t>((std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(z));}
struct Bounds {float x0,x1,y0,y1,z0,z1;};
Bounds bounds(Point a,Point b,Point c){return {std::min({a.x,b.x,c.x}),std::max({a.x,b.x,c.x}),std::min({a.y,b.y,c.y}),std::max({a.y,b.y,c.y}),std::min({a.z,b.z,c.z}),std::max({a.z,b.z,c.z})};}
}
class Geometry {
public:
 std::vector<Triangle> roads,walls;std::vector<Segment> borders;
 std::unordered_map<std::int64_t,std::vector<std::uint32_t>> grid;
 std::size_t bytes=0;
};
bool SliceWall(const Triangle& t,float height,Segment& out){
 if(!finite(t.a)||!finite(t.b)||!finite(t.c)||!std::isfinite(height))return false;
 Point points[3]{t.a,t.b,t.c},hits[3]{};int n=0;
 for(int i=0;i<3;++i){auto a=points[i],b=points[(i+1)%3];float dy=b.y-a.y;
  if(std::abs(dy)<1e-6f)continue;float f=(height-a.y)/dy;
  if(f<0||f>1)continue;Point p{a.x+(b.x-a.x)*f,height,a.z+(b.z-a.z)*f};
  if(n && std::hypot(p.x-hits[0].x,p.z-hits[0].z)<1e-4f)continue;if(n<3)hits[n++]=p;
 }if(n<2||std::hypot(hits[0].x-hits[1].x,hits[0].z-hits[1].z)<.025f)return false;
 out={hits[0],hits[1]};return true;
}
std::shared_ptr<Geometry> Load(const std::filesystem::path& path,std::string& error){
 auto fail=[&](const char* s)->std::shared_ptr<Geometry>{error=s;return {};};
 std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return fail("该区域暂无底图数据");
 const std::streamoff bytes=f.tellg();if(bytes<20||std::uint64_t(bytes)>max_bytes)return fail("底图文件大小异常");f.seekg(0);
 char magic[8];std::uint32_t count[3]{};f.read(magic,8);f.read(reinterpret_cast<char*>(count),12);
 if(std::memcmp(magic,"BBMAP002",8))return fail("底图格式不兼容");
 if(count[0]>1000000||count[1]>1000000||count[2]>1000000||20ULL+36ULL*count[0]+24ULL*count[1]+36ULL*count[2]!=std::uint64_t(bytes))return fail("底图记录损坏");
 auto m=std::make_shared<Geometry>();m->bytes=std::size_t(bytes);m->roads.resize(count[0]);m->borders.resize(count[1]);m->walls.resize(count[2]);
 static_assert(sizeof(Point)==12&&sizeof(Triangle)==36&&sizeof(Segment)==24);
 f.read(reinterpret_cast<char*>(m->roads.data()),count[0]*36ULL);f.read(reinterpret_cast<char*>(m->borders.data()),count[1]*24ULL);f.read(reinterpret_cast<char*>(m->walls.data()),count[2]*36ULL);
 if(!f)return fail("底图读取未完成");std::size_t refs=0;
 auto index=[&](std::uint32_t id,Point a,Point b,Point c){
  if(!finite(a)||!finite(b)||!finite(c))return false;auto box=bounds(a,b,c);
  if(box.x1-box.x0>512||box.z1-box.z0>512)return false;
  int x0=int(std::floor(box.x0/cell)),x1=int(std::floor(box.x1/cell)),z0=int(std::floor(box.z0/cell)),z1=int(std::floor(box.z1/cell));
  if((refs+=std::size_t(x1-x0+1)*(z1-z0+1))>max_refs)return false;
  for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z)m->grid[key(x,z)].push_back(id);return true;
 };
 for(std::uint32_t i=0;i<count[0];++i){auto t=m->roads[i];if(!index(i,t.a,t.b,t.c))return fail("道路坐标或索引异常");}
 for(std::uint32_t i=0;i<count[1];++i){auto s=m->borders[i];if(!index(0x40000000u|i,s.a,s.b,s.b))return fail("边界坐标或索引异常");}
 for(std::uint32_t i=0;i<count[2];++i){auto t=m->walls[i];if(!index(0x80000000u|i,t.a,t.b,t.c))return fail("墙体坐标或索引异常");}
 error.clear();return m;
}
namespace {
struct Canvas {
 int n;Point center;float half;std::vector<std::uint8_t> pixels;
 Canvas(int n,Point p,float half):n(n),center(p),half(half),pixels(std::size_t(n)*n){}
 std::pair<float,float> project(Point p)const{return {((p.x-center.x)/half+1)*.5f*n,(1-(p.z-center.z)/half)*.5f*n};}
 void fill(Triangle t,std::uint8_t color){
  auto [ax,ay]=project(t.a);auto [bx,by]=project(t.b);auto [cx,cy]=project(t.c);
  int first=std::max(0,int(std::floor(std::min({ay,by,cy})))),last=std::min(n-1,int(std::ceil(std::max({ay,by,cy}))));
  float xs[3]{ax,bx,cx},ys[3]{ay,by,cy};
  for(int y=first;y<=last;++y){float at=y+.5f,lo=1e9f,hi=-1e9f;
   for(int e=0;e<3;++e){int j=(e+1)%3;if((ys[e]<=at&&ys[j]>at)||(ys[j]<=at&&ys[e]>at)){float x=xs[e]+(at-ys[e])*(xs[j]-xs[e])/(ys[j]-ys[e]);lo=std::min(lo,x);hi=std::max(hi,x);}}
   int x0=std::max(0,int(std::ceil(lo-.5f))),x1=std::min(n-1,int(std::floor(hi-.5f)));
   for(int x=x0;x<=x1;++x){auto& p=pixels[std::size_t(y)*n+x];p=std::max(p,color);}
  }
 }
 void line(Segment s,std::uint8_t color){
  auto [x0,y0]=project(s.a);auto [x1,y1]=project(s.b);float dx=x1-x0,dy=y1-y0,lo=0,hi=1;
  auto clip=[&](float p,float q){if(std::abs(p)<1e-6f)return q>=0;float t=q/p;if(p<0)lo=std::max(lo,t);else hi=std::min(hi,t);return lo<=hi;};
  if(!clip(-dx,x0)||!clip(dx,n-1-x0)||!clip(-dy,y0)||!clip(dy,n-1-y0))return;
  float ax=x0+dx*lo,ay=y0+dy*lo,bx=x0+dx*hi,by=y0+dy*hi;int steps=std::max(1,int(std::ceil(std::max(std::abs(bx-ax),std::abs(by-ay)))));
  for(int i=0;i<=steps;++i){int x=int(std::lround(ax+(bx-ax)*i/steps)),y=int(std::lround(ay+(by-ay)*i/steps));if(x>=0&&x<n&&y>=0&&y<n){auto& p=pixels[std::size_t(y)*n+x];p=std::max(p,color);}}
 }
 std::vector<Rect> compress(){
  std::vector<Rect> rects;std::unordered_map<std::uint64_t,std::size_t> previous,current;
  for(int y=0;y<n;++y){current.clear();for(int x=0;x<n;){int begin=x;auto color=pixels[std::size_t(y)*n+x++];while(x<n&&pixels[std::size_t(y)*n+x]==color)++x;if(!color)continue;
    const auto k=(std::uint64_t(begin)<<32)|(std::uint64_t(x)<<8)|color;auto it=previous.find(k);std::size_t index;
    if(it!=previous.end()){index=it->second;rects[index].y1=std::uint16_t(y+1);}else {index=rects.size();rects.push_back({std::uint16_t(begin),std::uint16_t(y),std::uint16_t(x),std::uint16_t(y+1),color});}current.emplace(k,index);
   }previous.swap(current);
  }return rects;
 }
};
}
std::shared_ptr<Raster> Bake(const Geometry& map,std::uint32_t block,Point center,float range){
 auto began=std::chrono::steady_clock::now();auto out=std::make_shared<Raster>();out->block=block;out->center=center;out->half_span=std::clamp(range,15.f,120.f)+24;out->source_bytes=map.bytes;
 std::vector<std::uint32_t> ids;int x0=int(std::floor((center.x-out->half_span)/cell)),x1=int(std::floor((center.x+out->half_span)/cell)),z0=int(std::floor((center.z-out->half_span)/cell)),z1=int(std::floor((center.z+out->half_span)/cell));
 for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z){auto it=map.grid.find(key(x,z));if(it!=map.grid.end())ids.insert(ids.end(),it->second.begin(),it->second.end());}
 std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
 std::vector<std::pair<Triangle,std::uint8_t>> roads;std::vector<Segment> borders,walls;
 for(auto id:ids){auto index=id&0x3fffffffu;
  if(id<0x40000000u){auto t=map.roads[index];auto b=bounds(t.a,t.b,t.c);float gap=std::max({b.y0-center.y,center.y-b.y1,0.f});if(gap<=7.f)roads.emplace_back(t,gap<=1.8f?2:1);}
  else if(id<0x80000000u){auto s=map.borders[index];if(std::abs((s.a.y+s.b.y)*.5f-center.y)<=2.5f)borders.push_back(s);}
  else {Segment s;if(SliceWall(map.walls[index],center.y+1.1f,s))walls.push_back(s);}
 }
 out->roads=roads.size();out->walls=walls.size();
 for(int resolution:{512,384,256,192,128}){
  Canvas canvas(resolution,center,out->half_span);for(auto& r:roads)canvas.fill(r.first,r.second);for(auto s:borders)canvas.line(s,3);for(auto s:walls)canvas.line(s,4);
  out->rects=canvas.compress();out->resolution=resolution;if(out->rects.size()<=4500||resolution==128)break;
 }
 out->build_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-began).count();return out;
}
namespace {
struct Job {std::uint32_t block=0;int x=0,y=0,z=0,range=0;bool operator==(const Job&)const=default;};
class Worker {
 std::mutex mutex;std::condition_variable cv;Job wanted;bool pending=false,stop=false;std::thread thread;
 std::shared_ptr<const Raster> ready;
 void run(){
  std::uint32_t loaded=0;std::shared_ptr<Geometry> geometry;std::string error;
  for(;;){Job job;{std::unique_lock lock(mutex);cv.wait(lock,[&]{return pending||stop;});if(stop)return;job=wanted;pending=false;}
   std::shared_ptr<Raster> result;
   try {
   if(loaded!=job.block){loaded=job.block;geometry.reset();const char* root=std::getenv("BB_MINIMAP_ASSETS");
    char name[64];std::snprintf(name,sizeof name,"m%02u_%02u_%02u_%02u.bmap",job.block>>24,(job.block>>16)&255,(job.block>>8)&255,job.block&255);
    if(root)geometry=Load(std::filesystem::path(reinterpret_cast<const char8_t*>(root))/name,error);else error="底图目录未配置";
   }
   if(geometry)result=Bake(*geometry,job.block,{job.x*8.f,float(job.y),job.z*8.f},float(job.range));else {result=std::make_shared<Raster>();result->block=job.block;result->error=error;}
   }catch(const std::exception& e){geometry.reset();result=std::make_shared<Raster>();result->block=job.block;result->error="底图暂时不可用";std::fprintf(stderr,"Minimap background safely disabled: %s\n",e.what());}
   {std::lock_guard lock(mutex);if(stop)return;if(!(job==wanted))continue;ready=result;}
   if(geometry)std::printf("Minimap background: block=%08x bake=%.2f ms roads=%zu wall-sections=%zu cached-rects=%zu res=%d asset=%.2f MiB\n",job.block,result->build_ms,result->roads,result->walls,result->rects.size(),result->resolution,result->source_bytes/1048576.);
  }
 }
public:
 Worker():thread([this]{run();}){}
 ~Worker(){{std::lock_guard lock(mutex);stop=true;}cv.notify_one();thread.join();}
 void request(Job job){std::lock_guard lock(mutex);if(job==wanted)return;wanted=job;pending=true;cv.notify_one();}
 std::shared_ptr<const Raster> get(std::uint32_t block){std::lock_guard lock(mutex);auto r=ready;return r&&r->block==block?r:nullptr;}
};
Worker& worker(){static Worker w;return w;}
}
void Request(std::uint32_t block,Point p,float range){if(!finite(p))return;worker().request({block,int(std::round(p.x/8)),int(std::round(p.y)),int(std::round(p.z/8)),int(std::clamp(range,15.f,120.f))});}
std::shared_ptr<const Raster> Ready(std::uint32_t block){return worker().get(block);}
}
