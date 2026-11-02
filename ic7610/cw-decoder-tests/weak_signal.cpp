#include "gen.h"
int ed(const std::string&a,const std::string&b){std::vector<int>p(b.size()+1),c(b.size()+1);for(size_t j=0;j<=b.size();j++)p[j]=j;for(size_t i=1;i<=a.size();i++){c[0]=i;for(size_t j=1;j<=b.size();j++)c[j]=std::min({p[j]+1,c[j-1]+1,p[j-1]+(a[i-1]!=b[j-1])});p=c;}return p[b.size()];}
std::string strip(const std::string&s){std::string o;for(char c:s)if(c!=' '&&c!='\n')o+=c;return o;}
int main(){ std::string msg="CQ CQ DE KC9OXP KC9OXP PSE K  UR RST 579 579 NAME MIKE QTH ELKHART IN ";std::string ref=strip(msg);
 for(float nz:{0.6f,1.0f,1.5f,2.0f}){ for(int fist=0;fist<2;fist++){ Gen g{48000};g.seg(1,false,0,0);hand(g,msg,20,600,0.3,fist?0.15f:0.f,3,fist?2.5f:3.f,0,4);g.seg(1.5,false,0,0);
  std::mt19937 r(9);std::normal_distribution<float> nd(0,nz);for(auto&v:g.a)v+=nd(r);
  double acc[2];for(int e=0;e<2;e++){Ic7610CwCore core;core.setTrackRange(200,1200,-1);if(e)core.setEngine(Ic7610CwCore::Engine::Hand);std::string out;for(size_t i=0;i+960<=g.a.size();i+=960)core.feed(&g.a[i],960,48000,out);acc[e]=100*(1-std::min(1.0,ed(strip(out),ref)/(double)ref.size()));}
  // SNR in 500 Hz bandwidth: signal power 0.045 (on), noise nz^2*500/24000
  printf("noise %.1f (SNR in 500 Hz %+5.1f dB) %-7s machine %3.0f%%  hand %3.0f%%\n",nz,10*log10(0.045/(nz*nz*500/24000)),fist?"hand15%":"keyer",acc[0],acc[1]);}}}
