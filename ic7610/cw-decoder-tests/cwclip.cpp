// Offline decoder for recorded clips: cwclip file.wav [pitchHz]
#include "ic7610cwcore.h"
#include <cstdio>
#include <cstdint>
#include <fstream>
int main(int argc,char**argv){ if(argc<2){fprintf(stderr,"usage: cwclip file.wav [pitch]\n");return 1;}
 std::ifstream f(argv[1],std::ios::binary); std::vector<char> b((std::istreambuf_iterator<char>(f)),{});
 auto rd32=[&](size_t o){return (uint32_t)(uint8_t)b[o]|((uint32_t)(uint8_t)b[o+1]<<8)|((uint32_t)(uint8_t)b[o+2]<<16)|((uint32_t)(uint8_t)b[o+3]<<24);};
 auto rd16=[&](size_t o){return (uint16_t)((uint8_t)b[o]|((uint8_t)b[o+1]<<8));};
 size_t p=12; uint32_t sr=0; int ch=1,bits=16; size_t dOff=0,dLen=0;
 while(p+8<=b.size()){uint32_t len=rd32(p+4); if(!memcmp(&b[p],"fmt ",4)){ch=rd16(p+10);sr=rd32(p+12);bits=rd16(p+22);} if(!memcmp(&b[p],"data",4)){dOff=p+8;dLen=std::min<size_t>(len,b.size()-dOff);break;} p+=8+len+(len&1);}
 if(!sr||bits!=16){fprintf(stderr,"need 16-bit PCM\n");return 1;}
 std::vector<float> x; for(size_t i=dOff;i+2*ch<=dOff+dLen;i+=2*ch){float v=0;for(int c=0;c<ch;c++)v+=(int16_t)rd16(i+2*c)/32768.0f;x.push_back(v/ch);}
 float pitch=argc>2?atof(argv[2]):-1;
 for(int e=0;e<2;e++){Ic7610CwCore core; if(pitch>0) core.setTrackRange(pitch-150,pitch+150,pitch); else core.setTrackRange(200,1200,-1);
  if(e) core.setEngine(Ic7610CwCore::Engine::Hand); std::string out; int blk=sr/50;
  for(size_t i=0;i+blk<=x.size();i+=blk) core.feed(&x[i],blk,(float)sr,out);
  std::vector<float> z(sr*2,0.0f); core.feed(z.data(),z.size(),(float)sr,out);
  auto s=core.stats(); printf("%s (pitch %.0f Hz, %.1f WPM, signal %.0f dB):\n%s\n\n",e?"HAND":"MACHINE",s.pitchHz,s.speedWpm,s.snrDb,out.c_str());}}
