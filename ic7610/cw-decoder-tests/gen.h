#pragma once
#include "ic7610cwcore.h"
#include <cstdio>
#include <map>
#include <random>
static std::map<char,const char*> M={{'A',".-"},{'B',"-..."},{'C',"-.-."},{'D',"-.."},{'E',"."},{'K',"-.-"},{'O',"---"},{'P',".--."},{'Q',"--.-"},{'X',"-..-"},{'9',"----."},{'T',"-"},{'S',"..."},{'R',".-."},{'N',"-."},{'5',"....."},{'W',".--"},{'1',".----"},{'U',"..-"},{'L',".-.."},{'I',".."},{'H',"...."},{'G',"--."},{'F',"..-."},{'M',"--"},{'Y',"-.--"},{'V',"...-"},{'J',".---"},{'7',"--..."},{'6',"-...."},{'0',"-----"},{'2',"..---"},{'3',"...--"},{'4',"....-"},{'8',"---.."},{'Z',"--.."}};
struct Gen{float sr; std::vector<float> a; double ph=0;
 void seg(double d,bool on,float f,float amp){int n=d*sr,e=0.005*sr;for(int i=0;i<n;i++){double env=1;if(i<e)env=0.5-0.5*cos(M_PI*i/e);if(i>n-e)env=0.5-0.5*cos(M_PI*(n-i)/e);ph+=2*M_PI*f/sr;a.push_back(on?amp*env*sin(ph):0);}}
 void msg(const std::string&m,float wpm,float f,float amp,float qsb=0){double dot=1.2/wpm;for(char c:m){float A=amp*(qsb>0?(0.55+0.45*sin(2*M_PI*a.size()/sr/qsb)):1);if(c==' '){seg(4*dot,false,f,A);continue;}for(const char*p=M[c];*p;p++){seg(*p=='.'?dot:3*dot,true,f,A);seg(dot,false,f,A);}seg(2*dot,false,f,A);}}
};
void run(const char*name,Gen&g,float noise,int mode,float a,float b,float c,bool sq=true){
 std::mt19937 r(7);std::normal_distribution<float> nd(0,noise);std::vector<float> x=g.a;for(auto&v:x)v+=nd(r);
 Ic7610CwCore core; core.setSquelch(sq);
 if(mode==0)core.setTrackRange(a,b,c); else if(mode==1)core.setFixedPitch(a); else {core.setTrackRange(200,1200,-1);}
 std::string out;int blk=g.sr/50;for(size_t i=0;i+blk<=x.size();i+=blk){core.feed(&x[i],blk,g.sr,out);}
 auto s=core.stats();printf("== %s\n[%s]\n   pitch %.0f snr %.1f dB lock %d\n",name,out.c_str(),s.pitchHz,s.snrDb,s.locked);}

#include <random>
// hand-keyed generator: element jitter, dash ratio, short letter gaps, speed drift
void hand(Gen&g,const std::string&m,float wpm,float f,float amp,float jit,float dashRatio,float lgap,float drift,unsigned seed){
 std::mt19937 r(seed);std::normal_distribution<float> n(0,jit);double t0=g.a.size()/g.sr;
 for(char c:m){double w=wpm*(1+drift*sin((g.a.size()/g.sr-t0)/4.0));double dot=1.2/w;
  if(c==' '){g.seg(std::max(0.5,(double)(7.0-lgap+2*n(r)))*dot,false,f,amp);continue;}
  for(const char*p=M[c];*p;p++){g.seg((*p=='.'?1.0:dashRatio)*dot*std::max(0.4f,1+n(r)),true,f,amp);g.seg(dot*std::max(0.4f,1+n(r)),false,f,amp);}
  g.seg(std::max(0.3,(double)((lgap-1)+ n(r)))*dot,false,f,amp);}
}
