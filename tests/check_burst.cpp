#include <cassert>
#include <cstdint>
#include <vector>
#include <cstdio>
#include <cstring>
#include "../components/nspanel_terminal/stream_engine.h"
using namespace esphome::nspanel_terminal;
static std::vector<uint8_t> lcd(454 * 320 * 2);
static unsigned px, py, pw, ph, at;
static void window(unsigned x, unsigned y, unsigned w, unsigned h) {
  px=x; py=y; pw=w; ph=h; at=0;
}
static void byte(uint8_t b) {
  assert(at < pw*ph*2);
  lcd[((py+at/(2*pw))*454+px)*2+at%(2*pw)] = b;
  ++at;
}
#define STREAM_WINDOW(x,y,w,h) window(x,y,w,h)
#define STREAM_BYTE(b) byte(b)
#include "../firmware/src/stream_protocol.h"
static unsigned feed_span(StreamPackBits *s, uint8_t b) {
  unsigned used = 0;
  unsigned event = stream_feed_span(s, &b, 1, &used);
  assert(used == 1);
  return event;
}
static std::vector<uint8_t> report(unsigned version, unsigned status, uint32_t consumed, unsigned rects) {
  std::vector<uint8_t> b(32);
  b[0]='S'; b[1]='R'; b[2]=version; b[3]=status;
  protocol::put16(b,4,7); protocol::put16(b,8,consumed); protocol::put16(b,10,consumed>>16);
  protocol::put16(b,12,rects); protocol::put16(b,26,512);
  protocol::put16(b,30,protocol::crc(b.data(),30)); return b;
}
static void feed(StreamEngine &e, const std::vector<uint8_t> &b) {
  protocol::Reply r; for(auto v:b) e.feed(v,1,r);
}
static unsigned record(StreamPackBits &s, unsigned seq, unsigned kind, unsigned x=0,
                       unsigned y=0,unsigned w=0,unsigned h=0) {
  unsigned event=0;
  for(auto b:protocol::record(7,seq,kind,x,y,w,h,{})) event=feed_span(&s,b);
  return event;
}
int main() {
  StreamEngine e; e.begin(7,0,454,320); feed(e,report(8,protocol::READY_BRIGHTNESS,0,0));
  StreamPackBits s; stream_init(&s,7);
  // One snapshot is deliberately overwritten as soon as the next run is accepted.
  std::vector<uint8_t> snapshot(80*8*2);
  unsigned submitted=0, done=0, published=0, consumed=0, stalls=0;
  std::vector<uint8_t> forward;
  for(unsigned loops=0;loops<10000 && !done;++loops) {
    if(submitted<3 && e.can_submit()) {
      ++submitted;
      for(unsigned i=0;i<80*8;++i) {
        uint16_t color=uint16_t(i*73+submitted);
        snapshot[2*i]=color; snapshot[2*i+1]=color>>8;
      }
      assert(e.submit((submitted-1)*100,10,80,8,snapshot.data(),1,submitted==3));
    }
    e.prepare(1);
    auto n=e.allowance();
    if(n) {
      forward.insert(forward.end(),e.data(),e.data()+n); e.sent(n,1);
      assert(e.sent_bytes()-published<=512);
    }
    // Delay the receiver until the sender exhausts credit or finishes an END.
    if(!n || e.stage()==StreamEngine::Stage::BETWEEN || e.stage()==StreamEngine::Stage::DONE) {
      if(!n && e.pending()) ++stalls;
      for(auto b:forward) {
        unsigned event=feed_span(&s,b); ++consumed;
        assert(!s.fault);
        if(event || consumed-published>=256) {
          if(event==STREAM_DONE) { ++done; assert(submitted==3 && s.rectangles==3); }
          feed(e,report(8,event?event:STREAM_PROGRESS,consumed,s.rectangles)); published=consumed;
          assert(!e.failed());
        }
      }
      forward.clear();
    }
  }
  assert(done==1 && e.idle() && e.completed()==3 && stalls);
  for(unsigned run=1;run<=3;++run) for(unsigned i=0;i<80*8;++i) {
    auto offset=((10+i/80)*454+(run-1)*100+i%80)*2;
    auto color=uint16_t(i*73+run);
    assert(lcd[offset]==uint8_t(color>>8) && lcd[offset+1]==uint8_t(color));
  }
  // The clean gaps were never written.
  for(unsigned y=10;y<18;++y) for(unsigned x=80;x<100;++x)
    assert(!lcd[(y*454+x)*2] && !lcd[(y*454+x)*2+1]);
  // A full 256-rectangle burst crosses the byte window many times with one DONE.
  StreamEngine large; large.begin(7,0,454,320);
  feed(large,report(8,protocol::READY_BRIGHTNESS,0,0)); stream_init(&s,7);
  unsigned total=0, large_done=0, offset=0, last_report=0;
  for(unsigned loops=0;loops<10000 && !large_done;++loops) {
    if(total<256 && large.can_submit()) {
      ++total; assert(large.submit(0,0,1,1,snapshot.data(),1,total==256));
    }
    large.prepare(1); auto n=large.allowance();
    std::vector<uint8_t> bytes(large.data(),large.data()+n); large.sent(n,1);
    for(auto b:bytes) {
      auto status=feed_span(&s,b); ++offset;
      if(status || offset-last_report>=256) {
        if(status==STREAM_DONE) ++large_done;
        feed(large,report(8,status?status:STREAM_PROGRESS,offset,s.rectangles)); last_report=offset;
      }
    }
    assert(!large.failed() && !s.fault);
  }
  assert(large_done==1 && large.completed()==256 && large.idle());
  // Reject premature END, invalid final flag and wrong-version records.
  stream_init(&s,7); assert(!record(s,0,STREAM_RECT,0,0,1,1));
  assert(record(s,1,STREAM_END,1)==STREAM_ERROR);
  stream_init(&s,7); assert(!record(s,0,STREAM_RECT,0,0,1,1)); s.remaining=0;
  assert(record(s,1,STREAM_END,2)==STREAM_ERROR);
  stream_init(&s,7);
  // A valid legacy header must still be rejected by the protocol-8 receiver.
  std::vector<uint8_t> legacy(20);
  legacy[0]='R'; legacy[1]='S'; legacy[2]=7; legacy[3]=protocol::PING;
  protocol::put16(legacy,4,7);
  protocol::put16(legacy,18,protocol::crc(legacy.data(),18));
  unsigned event=0; for(auto b:protocol::cobs(legacy)) event=feed_span(&s,b);
  assert(event==STREAM_ERROR);
  // Reverse protocol 6/7 reports never make the sender ready.
  for(unsigned version : {6u,7u}) {
    auto legacy_report=report(version,protocol::READY_BRIGHTNESS,0,0);
    protocol::Reply reply;
    assert(!protocol::reply(legacy_report.data(),7,reply));
    StreamEngine rejected; rejected.begin(7,0,454,320);
    feed(rejected,legacy_report);
    assert(rejected.stage()==StreamEngine::Stage::READY);
    rejected.prepare(1001); assert(rejected.failed());
  }
  // Multi-run corruption abandons the burst; the display owner must redraw all.
  StreamEngine broken; broken.begin(7,0,454,320);
  feed(broken,report(8,protocol::READY_BRIGHTNESS,0,0));
  assert(broken.submit(0,0,1,1,snapshot.data(),1,false));
  feed(broken,report(8,protocol::ERROR,0,0)); assert(broken.failed());
  // A final acknowledgement with the wrong rectangle count cannot release the burst.
  StreamEngine bad_done; bad_done.begin(7,0,454,320);
  feed(bad_done,report(8,protocol::READY_BRIGHTNESS,0,0));
  assert(bad_done.submit(0,0,1,1,snapshot.data(),1));
  for(unsigned i=0;i<20 && (bad_done.stage()!=StreamEngine::Stage::DONE || bad_done.pending());++i) {
    bad_done.prepare(1); auto n=bad_done.allowance(); bad_done.sent(n,1);
  }
  feed(bad_done,report(8,protocol::DONE,bad_done.sent_bytes(),2)); assert(bad_done.failed());
  DirtyRows d; d.mark(0,0); d.mark(16,0); unsigned x,y,w,h;
  assert(d.take(x,y,w,h,0) && x==0 && w==8); assert(d.take(x,y,w,h,0) && x==16 && w==8);
  // Arbitrary sparse dirty maps must emit only dirty tiles and lose no tiles.
  uint32_t random=12345;
  for(unsigned trial=0;trial<1000;++trial) {
    DirtyRows map; map.set_dimensions(64,32);
    for(unsigned row=0;row<32;++row) {
      random=random*1664525u+1013904223u; map.rows[row]=random&255;
    }
    auto expected=map.rows;
    while(map.take(x,y,w,h,0)) {
      auto mask=((uint64_t(1)<<(w/8))-1)<<(x/8);
      for(unsigned row=y;row<y+h;++row) {
        assert((expected[row]&mask)==mask); expected[row]&=~mask;
      }
    }
    for(auto row:expected) assert(!row);
  }
  // An oversized run must fault before any later run.
  stream_init(&s,7); assert(!record(s,0,STREAM_RECT,0,0,1,1));
  auto damaged=protocol::record(7,1,STREAM_DATA,0,0,0,0,{128,0x12,0x34});
  damaged = protocol::record(7,1,STREAM_DATA,0,0,0,0,{129,0x12,0x34});
  event=0; for(auto b:damaged) event=feed_span(&s,b);
  assert(event==STREAM_ERROR && s.fault);
  auto before=lcd;
  assert(!record(s,2,STREAM_RECT,20,20,1,1)); assert(lcd==before);
  assert(record(s,9,STREAM_SYNC)==STREAM_SYNCED && !s.fault);
  // Fragment every COBS/PackBits boundary, including 254-byte COBS runs,
  // implicit zeros, headers, record ends and service/ring-sized spans.
  for (unsigned chunk : {1u,2u,3u,7u,31u,32u,33u,254u,255u,512u,1024u}) {
    for (unsigned pattern = 0; pattern < 4; ++pattern) {
      std::vector<uint8_t> source(600);
      for (unsigned i=0;i<source.size();++i)
        source[i] = pattern==0 ? 0 : pattern==1 ? 255 : pattern==2 ? i : (i*73+19);
      protocol::Encoder encoder(300, [&](unsigned i) { return protocol::get16(&source[2*i]); });
      auto wire = protocol::record(7,0,STREAM_RECT,0,0,300,1,{});
      std::vector<uint8_t> body;
      unsigned seq=1;
      while (encoder.next(body)) {
        auto data=protocol::record(7,seq++,STREAM_DATA,0,0,0,0,body);
        wire.insert(wire.end(),data.begin(),data.end());
      }
      auto end=protocol::record(7,seq,STREAM_END,1,0,0,0,{});
      wire.insert(wire.end(),end.begin(),end.end());
      stream_init(&s,7);
      unsigned offset=0, done_count=0;
      while (offset<wire.size()) {
        unsigned used;
        auto status=stream_feed_span(&s,wire.data()+offset,
            std::min(chunk,unsigned(wire.size()-offset)),&used);
        assert(used && !s.fault); offset+=used;
        if(status==STREAM_DONE) ++done_count;
      }
      assert(done_count==1);
      for(unsigned i=0;i<300;++i) {
        assert(lcd[2*i]==source[2*i+1]); assert(lcd[2*i+1]==source[2*i]);
      }
    }
  }
  // Valid pixel damage is deliberately undetected in protocol 8.
  stream_init(&s,7); assert(!record(s,0,STREAM_RECT,0,0,1,1));
  auto pixel=protocol::record(7,1,STREAM_DATA,0,0,0,0,{128,0x12,0x35});
  for(auto b:pixel) assert(!feed_span(&s,b));
  assert(!s.fault && lcd[0]==0x12 && lcd[1]==0x35);
  assert(record(s,2,STREAM_END,1)==STREAM_DONE);
  // Header corruption, truncated bodies and extra body bytes must fault.
  for(unsigned mode=0;mode<3;++mode) {
    stream_init(&s,7); assert(!record(s,0,STREAM_RECT,0,0,1,1));
    auto raw=std::vector<uint8_t>(20);
    raw[0]='R';raw[1]='S';raw[2]=8;raw[3]=STREAM_DATA;
    protocol::put16(raw,4,7);protocol::put16(raw,6,1);protocol::put16(raw,8,3);
    protocol::put16(raw,18,protocol::crc(raw.data(),18));
    raw.insert(raw.end(),{128,0x12,0x34});
    if(mode==0) raw[8]^=1;
    if(mode==1) raw.pop_back();
    if(mode==2) raw.push_back(1);
    auto wire=protocol::cobs(raw); unsigned used;
    assert(stream_feed_span(&s,wire.data(),wire.size(),&used)==STREAM_ERROR);
    assert(s.fault && used==wire.size());
    assert(record(s,9,STREAM_SYNC)==STREAM_SYNCED);
  }
  // Compare bulk and byte decoders on damaged frames, with identical initial
  // state. This exercises premature zeros, code corruption and partial runs.
  uint32_t seed=4567;
  for(unsigned trial=0;trial<1000;++trial) {
    auto random_byte=[&]() { seed=seed*1664525u+1013904223u; return uint8_t(seed>>24); };
    std::vector<uint8_t> body(3+trial%510);
    for(auto &b:body) b=random_byte();
    auto wire=protocol::record(7,1,STREAM_DATA,0,0,0,0,body);
    if(trial%2) wire[trial%(wire.size()-1)]^=random_byte();
    auto initial=lcd;
    StreamPackBits a{}, b{}; stream_init(&a,7); stream_init(&b,7);
    record(a,0,STREAM_RECT,0,0,300,1);
    std::vector<unsigned> events_a, events_b;
    for(auto v:wire) { auto event=stream_feed(&a,v); if(event) events_a.push_back(event); }
    auto expected=lcd; lcd=initial;
    record(b,0,STREAM_RECT,0,0,300,1);
    unsigned offset=0;
    while(offset<wire.size()) {
      unsigned used, n=std::min(unsigned(wire.size()-offset),1u+unsigned(random_byte()));
      auto event=stream_feed_span(&b,wire.data()+offset,n,&used);
      assert(used); offset+=used;
      if(event) events_b.push_back(event);
    }
    assert(events_a==events_b && lcd==expected);
    assert(std::memcmp(&a,&b,sizeof a)==0);
  }
  puts("burst: pixels, clean gaps, credits, one DONE, validation and fault paths passed");
}
