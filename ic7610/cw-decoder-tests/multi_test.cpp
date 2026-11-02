#include "gen.h"
#include "ic7610cwmulti.h"
#include <fstream>
#include <cstring>
void show(Ic7610CwMulti&m){ for(auto&c:m.channels()) printf("  ch%-2d %4.0f Hz %4.1f WPM %4.1f dB %s | %s\n",c.id,c.pitchHz,c.wpm,c.snrDb,c.active?"ON ":"off",c.text.c_str()); }
int main(int argc,char**argv){
 if(argc>1){ std::ifstream f(argv[1],std::ios::binary); std::vector<char> b((std::istreambuf_iterator<char>(f)),{}); uint32_t sr=*(uint32_t*)&b[24]; size_t p=12,dOff=0,dLen=0; while(p+8<=b.size()){uint32_t len=*(uint32_t*)&b[p+4]; if(!memcmp(&b[p],"data",4)){dOff=p+8;dLen=std::min<size_t>(len,b.size()-dOff);break;} p+=8+len;}
  std::vector<float> x; for(size_t i=dOff;i+2<=dOff+dLen;i+=2) x.push_back(*(int16_t*)&b[i]/32768.f);
  Ic7610CwMulti m; m.setRange(200,2000); for(size_t i=0;i+960<=x.size();i+=960) m.feed(&x[i],960,sr); std::vector<float> z(sr*2,0); m.feed(z.data(),z.size(),sr); show(m); return 0;}
 float sr=48000; Gen a{sr},b{sr},c{sr};
 a.seg(0.5,false,0,0); hand(a,"CQ TEST DE W1AW W1AW TEST ",24,450,0.25,0.05,3,3,0,1);
 b.seg(3.0,false,0,0); hand(b,"W1AW DE KC9OXP 5NN IN ",28,600,0.15,0.05,3,3,0,2);
 c.seg(1.5,false,0,0); hand(c,"TEST DE N9UV N9UV K ",20,820,0.08,0.15,3.2,2.6,0,3);
 size_t L=std::max({a.a.size(),b.a.size(),c.a.size()})+sr*2; a.a.resize(L);b.a.resize(L);c.a.resize(L);
 std::vector<float> x(L); std::mt19937 r(5); std::normal_distribution<float> nd(0,0.12); for(size_t i=0;i<L;i++) x[i]=a.a[i]+b.a[i]+c.a[i]+nd(r);
 Ic7610CwMulti m; m.setRange(200,2000); for(size_t i=0;i+960<=x.size();i+=960) m.feed(&x[i],960,sr); show(m);
 // noise only
 {std::vector<float> n(sr*20); for(auto&v:n)v=nd(r); Ic7610CwMulti m2; m2.setRange(200,2000); for(size_t i=0;i+960<=n.size();i+=960) m2.feed(&n[i],960,sr); printf("noise only channels: %zu\n",m2.channels().size()); show(m2);}
}
