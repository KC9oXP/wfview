#include "gen.h"
std::string dec(Gen&g,float noise,bool handEng,float&wpm,unsigned seed=7){
 std::mt19937 r(seed);std::normal_distribution<float> nd(0,noise);std::vector<float> x=g.a;for(auto&v:x)v+=nd(r);
 Ic7610CwCore core; core.setTrackRange(200,1200,-1); if(handEng) core.setEngine(Ic7610CwCore::Engine::Hand);
 std::string out;int blk=g.sr/50;for(size_t i=0;i+blk<=x.size();i+=blk)core.feed(&x[i],blk,g.sr,out);
 wpm=core.stats().speedWpm; return out;}
// character accuracy via edit distance on letters only (spaces ignored)
int ed(const std::string&a,const std::string&b){std::vector<int>p(b.size()+1),c(b.size()+1);for(size_t j=0;j<=b.size();j++)p[j]=j;for(size_t i=1;i<=a.size();i++){c[0]=i;for(size_t j=1;j<=b.size();j++)c[j]=std::min({p[j]+1,c[j-1]+1,p[j-1]+(a[i-1]!=b[j-1])});p=c;}return p[b.size()];}
std::string strip(const std::string&s){std::string o;for(char c:s)if(c!=' '&&c!='\n')o+=c;return o;}
int main(int argc,char**argv){float sr=48000;float nz=argc>1?atof(argv[1]):0.15;bool verbose=argc>2;
 std::string msg="CQ CQ DE KC9OXP KC9OXP PSE K  UR RST 579 579 NAME MIKE QTH ELKHART IN  RIG IS IC7610 ES ANT IS WIRE  HW CPY  BK ";
 struct C{const char*n;float jit,dr,lg,dr2;} cs[]={{"machine",0,3,3,0},{"good 10%",0.10,3,3,0},{"average 20%",0.20,3.5,2.2,0},{"sloppy 25%",0.25,2.5,2.0,0.15},{"bad 30%",0.30,2.7,1.8,0.2}};
 std::string ref=strip(msg);
 for(auto&c:cs){ for(unsigned seed: {3u,11u}){Gen g{sr};g.seg(1,false,0,0);hand(g,msg,18,600,0.3,c.jit,c.dr,c.lg,c.dr2,seed);g.seg(1.5,false,0,0);
  float w1,w2; std::string m=dec(g,nz,false,w1), h=dec(g,nz,true,w2);
  printf("%-12s seed%-3u machine %3.0f%%  hand %3.0f%% (wpm %.0f)\n",c.n,seed,100.0*(1-std::min(1.0,ed(strip(m),ref)/(double)ref.size())),100.0*(1-std::min(1.0,ed(strip(h),ref)/(double)ref.size())),w2);
  if(verbose){printf("  M[%s]\n  H[%s]\n",m.c_str(),h.c_str());}}}}
