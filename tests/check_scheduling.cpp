#include <cassert>
#include <cstdint>
#include <cstdio>
#include <vector>
#include "../components/nspanel_terminal/stream_engine.h"

using namespace esphome::nspanel_terminal;
using Wait = StreamEngine::Wait;
using Stage = StreamEngine::Stage;

static std::vector<uint8_t> reply(unsigned status, uint32_t consumed = 0, unsigned rectangles = 0) {
  std::vector<uint8_t> b(32);
  b[0]='S'; b[1]='R'; b[2]=protocol::VERSION; b[3]=status;
  protocol::put16(b,4,7); protocol::put16(b,8,consumed); protocol::put16(b,10,consumed>>16);
  protocol::put16(b,12,rectangles); protocol::put16(b,26,512);
  protocol::put16(b,30,protocol::crc(b.data(),30));
  return b;
}
static void feed(StreamEngine &e, const std::vector<uint8_t> &bytes, uint32_t now = 1) {
  protocol::Reply r;
  for(auto b:bytes) e.feed(b,now,r);
  assert(!e.failed());
}
static void ready(StreamEngine &e, uint32_t now=0) {
  e.begin(7,now,454,320);
  feed(e,reply(protocol::READY_BRIGHTNESS),now);
}
static void send_record(StreamEngine &e) {
  e.prepare(1);
  while(e.pending()) {
    auto n=e.allowance(); assert(n);
    e.sent(n,1); feed(e,reply(protocol::PROGRESS,e.sent_bytes()));
  }
}
int main() {
  StreamEngine e;
  assert(e.next_work(0,true,2048)==Wait::INACTIVE);
  e.begin(7,0,454,320);
  assert(e.next_work(0,true,2048)==Wait::RX); // queued work cannot bypass READY
  auto frame=reply(protocol::READY_BRIGHTNESS);
  protocol::Reply r;
  for(unsigned i=0;i<31;++i) e.feed(frame[i],1,r);
  assert(e.next_work(1,true,2048)==Wait::RX); // incomplete report cannot grant progress
  e.feed(frame[31],1,r);
  assert(e.next_work(1,false,2048)==Wait::PRODUCER);
  assert(e.next_work(1,true,2048)==Wait::RUNNABLE); // work arrives before wait
  assert(e.next_work(79,false,2048)==Wait::PRODUCER);
  assert(e.next_work(80,false,2048)==Wait::RUNNABLE); // heartbeat deadline

  // Preparing RECT/DATA/END is runnable with no pending wire bytes yet.
  const uint8_t pixel[]={0x34,0x12};
  assert(e.submit(0,0,1,1,pixel,1));
  assert(!e.pending() && e.next_work(1,false,0)==Wait::RUNNABLE);
  e.prepare(1);
  assert(e.pending() && e.next_work(1,false,0)==Wait::TX_SPACE);
  assert(e.next_work(1,false,2048)==Wait::RUNNABLE);
  send_record(e);
  assert(e.stage()==Stage::DATA && !e.pending());
  assert(e.next_work(1,false,2048)==Wait::RUNNABLE);
  for(unsigned i=0;i<10 && e.stage()!=Stage::DONE;++i) send_record(e);
  assert(e.stage()==Stage::DONE && !e.pending());
  assert(e.next_work(1,true,2048)==Wait::RX); // even if another job is queued
  feed(e,reply(protocol::DONE,e.sent_bytes(),1));
  assert(e.next_work(1,true,2048)==Wait::RUNNABLE);

  // A burst can pause for an encoder; an empty BETWEEN is not a busy loop.
  ready(e); assert(e.submit(0,0,1,1,pixel,1,false));
  for(unsigned i=0;i<10 && e.stage()!=Stage::BETWEEN;++i) send_record(e);
  assert(e.stage()==Stage::BETWEEN && !e.pending());
  assert(e.next_work(1,false,2048)==Wait::PRODUCER);
  assert(e.next_work(1,true,2048)==Wait::RUNNABLE);

  // Byte credit wins over driver space and queued jobs, including partial records.
  ready(e);
  std::vector<uint8_t> pixels(454*16*2);
  for(unsigned i=0;i<pixels.size();++i) pixels[i]=uint8_t(i*73+19);
  assert(e.submit(0,0,454,16,pixels.data(),1));
  for(unsigned i=0;i<10;++i) {
    e.prepare(1); auto n=e.allowance(); if(!n) break; e.sent(n,1);
  }
  assert(e.pending() && !e.allowance() && e.sent_bytes()==512);
  assert(e.next_work(1,true,2048)==Wait::RX);
  feed(e,reply(protocol::PROGRESS,1));
  assert(e.allowance()==1 && e.next_work(1,false,2048)==Wait::RUNNABLE);
  assert(e.next_work(1,false,0)==Wait::TX_SPACE);
  e.sent(1,1);
  assert(e.next_work(1,false,2048)==Wait::RX);

  // Reset/cancellation discards old work; READY must be revalidated.
  e.begin(7,1,454,320);
  assert(e.next_work(1,true,2048)==Wait::RX);
  e.fail("cancelled"); assert(e.next_work(1,true,2048)==Wait::INACTIVE);

  // Brightness, exit and SYNC are serviced without waiting for unrelated RX.
  ready(e); e.request_brightness(50);
  assert(e.next_work(1,false,2048)==Wait::RUNNABLE);
  send_record(e); assert(e.next_work(1,false,2048)==Wait::RX);
  feed(e,reply(protocol::BRIGHTNESS_DONE,e.sent_bytes()));
  e.request_exit(); assert(e.next_work(1,false,2048)==Wait::RUNNABLE);
  send_record(e); assert(e.next_work(1,false,2048)==Wait::RX);
  feed(e,reply(protocol::BYE,e.sent_bytes()));
  assert(e.next_work(1,true,2048)==Wait::INACTIVE);
  ready(e); feed(e,reply(protocol::ERROR));
  assert(e.stage()==Stage::SYNC && e.next_work(1,false,2048)==Wait::RUNNABLE);
  send_record(e); assert(e.next_work(1,false,2048)==Wait::RX);
  feed(e,reply(protocol::SYNCED,e.sent_bytes()));
  assert(e.next_work(1,false,2048)==Wait::PRODUCER);

  ready(e,0xfffffff0u);
  assert(e.next_work(0x3fu,false,2048)==Wait::PRODUCER);
  assert(e.next_work(0x40u,false,2048)==Wait::RUNNABLE);
  puts("scheduling: handshake, producer arrival, preparation, partial credit, TX space, bursts, controls, reset and timer wrap passed");
}
