// SPDX-License-Identifier: GPL-2.0-or-later
// Included by texture_cache.cpp after its Vulkan/stream-buffer definitions.
#pragma once
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include "hd_texture_assets.h"

namespace VideoCore {
class HdTextureCache {
    struct Record { u64 hash,prefix;u32 width,height,bytes;std::string file;int loaded=-1; };
    struct Lookup { u64 hash;int record; };
    const Vulkan::Instance& instance;
    Vulkan::Runtime& runtime;
    BufferCache& buffers;
    std::filesystem::path root;
    std::vector<Record> records;
    std::unordered_map<u64,Lookup> lookups;
    u64 allocated=0;
    static constexpr u64 Budget=128ull<<20;
    // Views must die before the images they describe.
    std::vector<std::unique_ptr<Image>> images;
    Common::SlotVector<ImageView> views;
public:
    HdTextureCache(const Vulkan::Instance& i,Vulkan::Runtime& r,BufferCache& b):instance(i),runtime(r),buffers(b) {
        const char* directory=std::getenv("BB_HD_ASSETS");
        if(!directory || !*directory)return;
        root=std::filesystem::u8path(directory);
        std::ifstream file(root/"manifest.tsv");std::string line;
        while(records.size()<2048 && std::getline(file,line)) {
            if(line.empty() || line[0]=='#')continue;
            Record entry{};std::istringstream fields(line);
            if(!(fields>>std::hex>>entry.hash>>entry.prefix>>std::dec>>entry.width>>entry.height>>entry.bytes>>entry.file) ||
               !HdAssets::Filename(entry.file) || entry.width<32 || entry.height<32 || entry.width>4096 ||
               entry.height>4096 || entry.bytes==0 || entry.bytes>(16u<<20))continue;
            records.push_back(std::move(entry));
        }
        if(!records.empty())std::printf("HD textures: %zu fingerprints ready; original logical dimensions retained; budget 128 MiB\n",records.size());
    }
    Image* Find(const Image& original,const ImageViewInfo& view) {
        const auto& info=original.info;
        if(records.empty() || original.usage.storage || original.usage.render_target || original.usage.depth_target ||
           original.binding.is_target || original.binding.force_general || True(original.flags&ImageFlagBits::GpuModified) ||
           info.resources.levels!=1 || info.resources.layers!=1 || info.num_samples!=1 || !info.guest_address ||
           view.is_storage || view.range.base.level || view.range.base.layer || view.range.extent.levels!=1 ||
           view.range.extent.layers!=1 || (info.pixel_format!=vk::Format::eBc7UnormBlock && info.pixel_format!=vk::Format::eBc7SrgbBlock))return nullptr;
        const u64 stamp=original.mip_hashes.empty()?0:original.mip_hashes[0];
        int match=-1;
        auto found=lookups.find(original.image_uid);
        if(found!=lookups.end() && found->second.hash==stamp)match=found->second.record;
        else {
            bool possible=false;
            for(const auto& item:records)possible|=item.width==info.size.width && item.height==info.size.height && item.bytes==info.guest_size;
            if(possible) {
                const auto prefix=HdAssets::Fingerprint({reinterpret_cast<const u8*>(info.guest_address),std::min<std::size_t>(64,info.guest_size)});
                possible=false;
                for(const auto& item:records)possible|=item.prefix==prefix && item.width==info.size.width && item.height==info.size.height && item.bytes==info.guest_size;
            }
            if(possible) {
                const auto hash=HdAssets::Fingerprint({reinterpret_cast<const u8*>(info.guest_address),info.guest_size});
                for(std::size_t n=0;n<records.size();++n) {
                    const auto& item=records[n];
                    if(item.hash==hash && item.width==info.size.width && item.height==info.size.height && item.bytes==info.guest_size){match=static_cast<int>(n);break;}
                }
            }
            if(lookups.size()>4096)lookups.clear();
            lookups[original.image_uid]={stamp,match};
        }
        if(match<0)return nullptr;
        auto& item=records[match];
        if(item.loaded==-2)return nullptr;
        if(item.loaded>=0)return images[item.loaded].get();
        item.loaded=-2; // Invalid/missing assets never trigger repeated disk access on every draw.
        std::ifstream file(root/item.file,std::ios::binary|std::ios::ate);
        if(!file)return nullptr;
        const auto size=file.tellg();if(size<148 || size>(24<<20)+148)return nullptr;
        std::vector<u8> data(static_cast<std::size_t>(size));file.seekg(0);
        if(!file.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(data.size())))return nullptr;
        HdAssets::Dds dds{};
        if(!HdAssets::ReadDds(data,dds) || allocated+dds.blocks.size()>Budget)return nullptr;
        auto high=info;
        high.size={dds.width,dds.height,1};high.pitch=dds.width;high.guest_address=0;high.guest_size=0;
        high.resources.levels=dds.levels;
        high.props.is_tiled=0;high.tile_mode=AmdGpu::TileMode::DisplayLinearAligned;
        high.array_mode=AmdGpu::ArrayMode::ArrayLinearAligned;
        auto image=std::make_unique<Image>(instance,runtime,views,high);
        if(allocated+image->GetHostImageSize()>Budget)return nullptr;
        auto& stream=buffers.GetStreamBuffer();
        const auto offset=stream.Copy(dds.blocks.data(),dds.blocks.size(),256);
        std::vector<vk::BufferImageCopy> regions;u64 mip_offset=offset;u32 w=dds.width,h=dds.height;
        for(u32 mip=0;mip<dds.levels;++mip){
            regions.push_back({.bufferOffset=mip_offset,.imageSubresource={vk::ImageAspectFlagBits::eColor,mip,0,1},.imageExtent={w,h,1}});
            mip_offset+=u64((w+3)/4)*((h+3)/4)*16;w=std::max(w/2,1u);h=std::max(h/2,1u);
        }
        runtime.UploadImage(image.get(),&stream,regions);
        item.loaded=static_cast<int>(images.size());allocated+=image->GetHostImageSize();
        std::printf("HD textures: %s %ux%u -> %ux%u, original UVs retained; cache %.1f MiB\n",item.file.c_str(),info.size.width,info.size.height,dds.width,dds.height,double(allocated)/(1<<20));
        images.push_back(std::move(image));return images.back().get();
    }
};
} // namespace VideoCore
