// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
namespace BbMinimap {
struct Point {float x=0,y=0,z=0;};
struct Triangle {Point a,b,c;};
struct Segment {Point a,b;};
// Live teams are authoritative: story events can turn a friendly NPC hostile.
constexpr char ClassifyTeam(std::uint32_t team){
 switch(team){
 case 3:case 6:case 7:case 9:case 10:case 13:case 21:case 23:case 24:case 25:case 27:case 29:return 'E';
 case 8:case 12:case 26:case 28:return 'N';
 case 1:case 2:case 19:case 20:return 'F';
 case 0:case 30:return 0;
 default:return 'U';
 }
}
bool SliceWall(const Triangle& t,float height,Segment& out);
struct Rect {std::uint16_t x0,y0,x1,y1;std::uint8_t shade;};
struct Raster {
 std::uint32_t block=0;Point center;float half_span=0;
 int resolution=0;std::vector<Rect> rects;
 std::size_t source_bytes=0,roads=0,walls=0;
 double build_ms=0;std::string error;
};
class Geometry;
std::shared_ptr<Geometry> Load(const std::filesystem::path& path,std::string& error);
std::shared_ptr<Raster> Bake(const Geometry& map,std::uint32_t block,Point center,float range);
// Nonblocking, latest request wins. Parsing and rasterization never run on a game/present thread.
void Request(std::uint32_t block,Point player,float range);
std::shared_ptr<const Raster> Ready(std::uint32_t block);
}
