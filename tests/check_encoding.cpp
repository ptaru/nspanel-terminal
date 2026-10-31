#include <cassert>
#include <cstdio>
#include "../components/nspanel_terminal/protocol.h"
#include "protocol_reference.h"
struct Pixels {
  const uint8_t *data;
  uint16_t operator()(unsigned i) const { return esphome::nspanel_terminal::protocol::get16(data+2*i); }
};
using namespace esphome::nspanel_terminal;
int main() {
  uint32_t random=12345;
  for(unsigned count : {0u,1u,2u,127u,128u,129u,254u,255u,256u,257u,454u,7264u,145280u}) {
    for(unsigned pattern=0;pattern<6;++pattern) {
      std::vector<uint8_t> source(count*2);
      for(unsigned i=0;i<count;++i) {
        random=random*1664525u+1013904223u;
        const uint16_t v=pattern==0?0:pattern==1?0xffff:pattern==2?i:pattern==3?random>>16:pattern==4?(i/127)%2:i/128;
        source[2*i]=uint8_t(v);source[2*i+1]=uint8_t(v>>8);
      }
      protocol::Encoder<Pixels> fast(count,{source.data()});
      protocol_reference::Encoder<Pixels> ref(count,{source.data()});
      std::vector<uint8_t> a,b,raw,wire;
      unsigned seq=0;
      for(;;) {
        const bool more=fast.next(a);assert(more==ref.next(b));
        assert(a==b && fast.consumed()==ref.consumed());
        if(!more)break;
        protocol::data_record(7,seq,a,raw,wire,7);
        assert(wire==protocol_reference::record(7,seq++,protocol_reference::DATA,0,0,0,0,b,7));
      }
    }
  }
  // Reuse output vectors across every legal payload length and COBS edge cases.
  std::vector<uint8_t> body,raw,wire;
  for(unsigned pattern=0;pattern<4;++pattern) for(unsigned n=0;n<=512;++n) {
    body.resize(n);
    for(unsigned i=0;i<n;++i) {random=random*1664525u+1013904223u;body[i]=pattern==0?0:pattern==1?255:pattern==2?i:random>>24;}
    for(unsigned version:{6u,7u}) {
      protocol::data_record(0xffff,n,body,raw,wire,version);
      assert(wire==protocol_reference::record(0xffff,n,protocol_reference::DATA,0,0,0,0,body,version));
    }
  }
  body.resize(513);
  protocol::data_record(7,0,body,raw,wire);
  assert(raw.empty() && wire.empty());
  puts("encoding: reference-identical payloads, framing, tails and COBS boundaries passed");
}
