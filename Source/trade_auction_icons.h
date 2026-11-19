#pragma once
#include <d3d9.h>
#include <fstream>
#include "AardvarkUI/ui.h"
#include "bank_catalog.generated.h"
#include "trade_icons.generated.h"
#include "windows_hash.h"

namespace TradeAuction {
using Unpack=bool (*)(uintptr_t,const unsigned char*,unsigned,unsigned char*,unsigned);

class Icons {
    struct Texture {
        IDirect3DTexture9* ptr=nullptr;
        unsigned width=0,height=0;
    };
    std::map<std::string,Texture> cache;
    std::filesystem::path package;
    uintptr_t image=0;
    Unpack unpack=nullptr;

    static uint32_t Read(const std::vector<unsigned char>& bytes,size_t offset) {
        uint32_t value=0;
        std::memcpy(&value,bytes.data()+offset,4);
        return value;
    }
    static Texture Upload(IDirect3DDevice9* device,const std::vector<unsigned char>& dds) {
        if (dds.size()<128 || std::memcmp(dds.data(),"DDS ",4) || Read(dds,4)!=124) return {};
        const unsigned height=Read(dds,12),width=Read(dds,16);
        if (!width || width>512 || !height || height>512) return {};
        D3DFORMAT format=D3DFMT_UNKNOWN;
        unsigned pitch=0,rows=0;
        const uint32_t type=Read(dds,84);
        if (type==D3DFMT_DXT1 || type==D3DFMT_DXT3 || type==D3DFMT_DXT5) {
            format=static_cast<D3DFORMAT>(type);
            pitch=((width+3)/4)*(type==D3DFMT_DXT1 ? 8 : 16);
            rows=(height+3)/4;
        } else if (type==0 && Read(dds,88)==32 && Read(dds,92)==0xff0000 && Read(dds,96)==0xff00 && Read(dds,100)==0xff) {
            format=Read(dds,104)==0xff000000 ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
            pitch=width*4; rows=height;
        } else return {};
        if (dds.size()<128+pitch*rows) return {};
        IDirect3DTexture9* texture=nullptr;
        if (FAILED(device->CreateTexture(width,height,1,0,format,D3DPOOL_MANAGED,&texture,nullptr))) return {};
        D3DLOCKED_RECT lock{};
        if (FAILED(texture->LockRect(0,&lock,nullptr,0))) { texture->Release(); return {}; }
        const bool readable=lock.pBits && lock.Pitch>=static_cast<int>(pitch);
        if (readable) for (unsigned y=0;y<rows;++y)
            std::memcpy(static_cast<unsigned char*>(lock.pBits)+y*lock.Pitch,dds.data()+128+y*pitch,pitch);
        texture->UnlockRect(0);
        if (!readable) { texture->Release(); return {}; }
        return {texture,width,height};
    }
    Texture Load(IDirect3DDevice9* device,const std::string& icon) const {
        const auto end=std::end(TradeIconAssets);
        const auto asset=std::lower_bound(std::begin(TradeIconAssets),end,icon,
            [](const TradeIconAsset& row,const std::string& name){return row.name<name;});
        if (asset==end || icon!=asset->name) return {};
        std::ifstream input(package,std::ios::binary);
        std::vector<unsigned char> source(asset->stored);
        if (!input.seekg(static_cast<std::streamoff>(asset->offset)) ||
            !input.read(reinterpret_cast<char*>(source.data()),asset->stored) ||
            WindowsHash::Sha256(source.data(),asset->stored)!=asset->sha256) return {};
        if (!asset->compressed) return Upload(device,source);
        std::vector<unsigned char> decoded(asset->decoded);
        if (!unpack(image,source.data(),asset->stored,decoded.data(),asset->decoded)) return {};
        return Upload(device,decoded);
    }

public:
    void Configure(const std::filesystem::path& path) { package=path; }
    void Bind(uintptr_t base,Unpack inflate) { image=base; unpack=inflate; }
    void Reset() {
        for (const auto& entry:cache) if (entry.second.ptr) entry.second.ptr->Release();
        cache.clear();
    }
    void Draw(UiDrawList* draw,IDirect3DDevice9* device,const std::string& definition,UiPoint at,UiPoint size) {
        if (!device || !image || !unpack) return;
        const auto* row=FindBankDefinition(definition.c_str());
        if (!row) return;
        auto found=cache.find(row->icon);
        if (found==cache.end()) {
            Texture texture;
            try { texture=Load(device,row->icon); } catch (...) {}
            found=cache.emplace(row->icon,texture).first;
        }
        const auto value=found->second;
        if (!value.ptr) return;
        const float scale=std::min(size.x/value.width,size.y/value.height);
        const UiPoint rendered(value.width*scale,value.height*scale);
        const UiPoint start(at.x+(size.x-rendered.x)/2,at.y+(size.y-rendered.y)/2);
        draw->AddImage(value.ptr,start,{start.x+rendered.x,start.y+rendered.y});
    }
};
}
