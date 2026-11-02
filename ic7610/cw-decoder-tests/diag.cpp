#include "ic7610cwcore.h"
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <cstring>
int main(int argc,char**argv){ std::ifstream f(argv[1],std::ios::binary); std::vector<char> b((std::istreambuf_iterator<char>(f)),{});
 uint32_t sr=*(uint32_t*)&b[24]; size_t p=12,dOff=0,dLen=0; while(p+8<=b.size()){uint32_t len=*(uint32_t*)&b[p+4]; if(!memcmp(&b[p],"data",4)){dOff=p+8;dLen=std::min<size_t>(len,b.size()-dOff);break;} p+=8+len;}
 std::vector<float> x; for(size_t i=dOff;i+2<=dOff+dLen;i+=2) x.push_back(*(int16_t*)&b[i]/32768.f);
 float pitch=argc>2?atof(argv[2]):-1; Ic7610CwCore core; core.setTrackRange(pitch-150,pitch+150,pitch); core.setEngine(Ic7610CwCore::Engine::Hand);
 std::string out; size_t last=0; int blk=sr/50;
 for(size_t i=0;i+blk<=x.size();i+=blk){core.feed(&x[i],blk,sr,out); if((i/blk)%100==0){auto s=core.stats(); printf("t=%4.1f pitch %4.0f snr %4.1f lock %d open %d wpm %4.1f | %s\n",i/(float)sr,s.pitchHz,s.snrDb,s.locked,s.open,s.speedWpm,out.substr(last).c_str()); last=out.size();}}
}
