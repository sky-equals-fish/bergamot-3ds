// Read-only artwork reader. No Runtime instance is created for library browsing.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>
#include <cstring>
namespace bergamont {
struct GameArt { std::array<std::uint8_t,48*48*4> rgba{}; std::u16string title; };
inline std::uint32_t le32(const std::uint8_t* p) { return p[0]|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24); }
inline std::optional<GameArt> decode_smdh(const std::vector<std::uint8_t>& data) {
 if(data.size()<0x36c0 || std::memcmp(data.data(),"SMDH",4)) return {};
 GameArt out;
 for(unsigned i=0;i<64;++i) { auto c=char16_t(data[0x208+i*2]|(data[0x209+i*2]<<8));if(!c)break;out.title+=c; }
 for(unsigned y=0;y<48;++y)for(unsigned x=0;x<48;++x) {
  unsigned morton=0;for(unsigned b=0;b<3;++b){morton|=((x>>b)&1)<<(b*2);morton|=((y>>b)&1)<<(b*2+1);}
  const auto at=0x24c0+(((y/8)*6+x/8)*64+morton)*2;
  const unsigned v=data[at]|(data[at+1]<<8);const auto dst=(y*48+x)*4;
  out.rgba[dst]=std::uint8_t(((v>>11)&31)*255/31);out.rgba[dst+1]=std::uint8_t(((v>>5)&63)*255/63);out.rgba[dst+2]=std::uint8_t((v&31)*255/31);out.rgba[dst+3]=255;
 }
 return out;
}
inline std::optional<GameArt> read_game_art(const std::filesystem::path& path) {
 std::ifstream in(path,std::ios::binary);if(!in)return {};
 in.seekg(0,std::ios::end);const auto end=in.tellg();if(end<0)return {};const auto size=std::uint64_t(end);
 auto read=[&](std::uint64_t offset,std::size_t count){std::vector<std::uint8_t> b;if(offset>size || count>size-offset)return b;b.resize(count);in.clear();in.seekg(std::streamoff(offset));in.read(reinterpret_cast<char*>(b.data()),std::streamsize(count));if(!in)b.clear();return b;};
 auto h=read(0,0x200);if(h.size()!=0x200)return {};
 if(!std::memcmp(h.data(),"SMDH",4))return decode_smdh(read(0,0x36c0));
 std::uint64_t base=0;
 if(!std::memcmp(h.data()+0x100,"NCSD",4)){base=std::uint64_t(le32(h.data()+0x120))*0x200;h=read(base,0x200);}
 if(h.size()!=0x200 || std::memcmp(h.data()+0x100,"NCCH",4))return {};
 // Only plaintext NCCH. Encrypted packages get a placeholder or a user-supplied sidecar.
 if(!(h[0x18f]&4) || h[0x18e]>8)return {};
 const auto unit=std::uint64_t(0x200)<<h[0x18e];const auto exefs=base+std::uint64_t(le32(h.data()+0x1a0))*unit;
 auto ex=read(exefs,0x200);if(ex.size()!=0x200)return {};
 for(unsigned i=0;i<8;++i){auto p=ex.data()+i*16;if(!std::memcmp(p,"icon\0\0\0\0\0",8) && le32(p+12)>=0x36c0)return decode_smdh(read(exefs+0x200+le32(p+8),0x36c0));}
 return {};
}
}
