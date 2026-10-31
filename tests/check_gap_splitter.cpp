#include <cassert>
#include <cstdio>
#include <vector>
#include "../components/nspanel_terminal/stream_engine.h"
using namespace esphome::nspanel_terminal;
int main() {
  std::vector<uint8_t> frame(454*480*2,0);
  assert(GapSplitter::merge(frame.data(),454,8,0,128,16));
  assert(!GapSplitter::merge(frame.data(),454,8,0,432,16));
  assert(GapSplitter::merge(frame.data(),454,8,0,432,1));
  for(unsigned y=0;y<480;++y) for(unsigned x=0;x<454;++x) {
    frame[2*(y*454+x)]=uint8_t(x); frame[2*(y*454+x)+1]=uint8_t(y);
  }
  assert(!GapSplitter::merge(frame.data(),454,8,0,128,16));
  assert(GapSplitter::merge(frame.data(),454,8,0,16,16));
  assert(!GapSplitter::merge(frame.data(),454,8,0,24,16));
  // A single detailed row between the three anchor rows is still sampled.
  auto saved = frame;
  std::fill(frame.begin(),frame.end(),0);
  for(unsigned x=8;x<136;++x) frame[2*(5*454+x)] = uint8_t(x);
  assert(!GapSplitter::merge(frame.data(),454,8,0,128,16));
  frame = saved;
  // Full-screen geometry and the short final column remain bounded.
  for(unsigned width:{1u,7u,8u,9u,63u,320u,454u}) {
    DirtyRows d;d.set_dimensions(width,480);d.all(); unsigned x,y,w,h,n=0;
    while(d.take_adaptive(x,y,w,h,frame.data())) {assert(x==0 && w==width && h==16);++n;}
    assert(n==30);
  }
  uint32_t rng=11;
  for(unsigned trial=0;trial<500;++trial) {
    unsigned width=trial%2?454:320,height=trial%2?320:480;
    DirtyRows d;d.set_dimensions(width,height);
    for(unsigned y=0;y<height;++y) {
      rng=rng*1664525u+1013904223u;
      d.rows[y]=((uint64_t(rng)<<32)| (rng^0x738519u))&d.full;
      if(y%5) d.rows[y]=0;
    }
    auto expected=d.rows;unsigned x,y,w,h,n=0;
    while(d.take_adaptive(x,y,w,h,frame.data())) {
      assert(w && h && h<=16 && x+w<=width && y+h<=height && ++n<=height*d.columns);
      auto mask=(d.full>>(d.columns-((w+7)/8)))<<(x/8);
      for(unsigned row=y;row<y+h;++row) expected[row]&=~mask;
    }
    for(auto bits:expected) assert(!bits);
  }
  // A fresh mutation after claim survives the in-flight snapshot.
  DirtyRows d;d.mark(8,4);unsigned x,y,w,h;
  assert(d.take_adaptive(x,y,w,h,frame.data()));d.mark(8,4);
  assert(d.take_adaptive(x,y,w,h,frame.data()));assert(d.empty());
  puts("gap splitter: cost decisions, geometry, 500 sparse maps and snapshot mutation passed");
}
