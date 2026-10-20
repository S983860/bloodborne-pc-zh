// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include "minimap_geometry.h"
namespace BbEngine::PcHud {
using Point=BbMinimap::Point;
struct Marker { Point p; char kind='C'; bool elevated=false; };
struct Snapshot { bool valid=false; std::uint32_t block=0; Point player; float yaw=0; std::vector<Marker> markers; std::vector<Point> trail; };
bool Install(std::uint8_t* image,std::size_t size);
void Tick();
Snapshot Read();
Snapshot ReadSummary();
bool InWorld();
bool Paused();
void Pause(bool value);
bool CanFreeze();
void RequestSave();
void RequestTitle();
std::string SaveStatus();
}
