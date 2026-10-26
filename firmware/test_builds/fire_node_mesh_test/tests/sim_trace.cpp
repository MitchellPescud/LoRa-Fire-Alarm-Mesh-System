// Host-side mesh simulator. Runs the real mesh.cpp / alarm.cpp / mesh_proto.cpp
// for several nodes against a simulated radio medium (range matrix, airtime,
// half-duplex, collisions, random loss) and checks protocol behaviour.
//   g++ -std=c++14 -Wall -Istubs sim_mesh.cpp -o sim_mesh && ./sim_mesh
#include "stubs/Arduino.h"
#include "stubs/Preferences.h"
#include <vector>
#include <map>
#include <functional>

uint32_t sim_now = 0;
int sim_cur_node = 0;
bool sim_verbose = false;
SerialStub Serial;
std::map<int, std::map<std::string, uint16_t>> sim_nvs;

// Serial output of every simulated node goes into one merged log, formatted like mesh_logger.py's:
//   <UTC timestamp> n<node id> <line>
FILE *sim_log = nullptr;
int sim_node_id[4] = {1, 100, 2, 0};
void SerialStub::flush(bool nl) {
  if (!nl) return;
  if (sim_log) {
    uint32_t ms = sim_now;
    fprintf(sim_log, "2026-10-26T08:%02u:%02u.%03uZ n%d %s\n", (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000,
            sim_node_id[sim_cur_node], line.c_str());
  }
  if (sim_verbose) printf("[%6u n%d] %s\n", sim_now, sim_node_id[sim_cur_node], line.c_str());
  line.clear();
}

#define MAXN 4
int sim_buzz_highs[MAXN];
void pinMode(int, int) {}
void digitalWrite(int pin, int v) { if (pin == 15 && v == HIGH) sim_buzz_highs[sim_cur_node]++; }

// ---- test SHA-256 / HMAC (independent of the firmware) ----
struct Sha { uint32_t h[8]; uint8_t buf[64]; size_t bl; uint64_t total; };
static const uint32_t K[64]={0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x)>>(n))|((x)<<(32-(n))))
static void shaBlock(Sha&s,const uint8_t*p){uint32_t w[64];for(int i=0;i<16;i++)w[i]=(uint32_t)p[4*i]<<24|p[4*i+1]<<16|p[4*i+2]<<8|p[4*i+3];
 for(int i=16;i<64;i++){uint32_t s0=ROR(w[i-15],7)^ROR(w[i-15],18)^(w[i-15]>>3),s1=ROR(w[i-2],17)^ROR(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
 uint32_t a=s.h[0],b=s.h[1],c=s.h[2],d=s.h[3],e=s.h[4],f=s.h[5],g=s.h[6],h=s.h[7];
 for(int i=0;i<64;i++){uint32_t S1=ROR(e,6)^ROR(e,11)^ROR(e,25),ch=(e&f)^(~e&g),t1=h+S1+ch+K[i]+w[i],S0=ROR(a,2)^ROR(a,13)^ROR(a,22),mj=(a&b)^(a&c)^(b&c),t2=S0+mj;h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;}
 s.h[0]+=a;s.h[1]+=b;s.h[2]+=c;s.h[3]+=d;s.h[4]+=e;s.h[5]+=f;s.h[6]+=g;s.h[7]+=h;}
static void shaInit(Sha&s){static const uint32_t i[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};memcpy(s.h,i,32);s.bl=0;s.total=0;}
static void shaUpdate(Sha&s,const uint8_t*d,size_t n){s.total+=n;while(n){size_t c=64-s.bl;if(c>n)c=n;memcpy(s.buf+s.bl,d,c);s.bl+=c;d+=c;n-=c;if(s.bl==64){shaBlock(s,s.buf);s.bl=0;}}}
static void shaFinal(Sha&s,uint8_t*o){uint64_t bits=s.total*8;uint8_t pad=0x80;shaUpdate(s,&pad,1);uint8_t z=0;while(s.bl!=56)shaUpdate(s,&z,1);uint8_t l[8];for(int i=0;i<8;i++)l[i]=bits>>(56-8*i);shaUpdate(s,l,8);for(int i=0;i<8;i++){o[4*i]=s.h[i]>>24;o[4*i+1]=s.h[i]>>16;o[4*i+2]=s.h[i]>>8;o[4*i+3]=s.h[i];}}
bool sim_hmac(const uint8_t*key,size_t kl,const uint8_t*d,size_t n,uint8_t out[32]){
 uint8_t k[64]={0},ip[64],op[64];if(kl>64)return false;memcpy(k,key,kl);for(int i=0;i<64;i++){ip[i]=k[i]^0x36;op[i]=k[i]^0x5c;}
 Sha s;uint8_t in[32];shaInit(s);shaUpdate(s,ip,64);shaUpdate(s,d,n);shaFinal(s,in);shaInit(s);shaUpdate(s,op,64);shaUpdate(s,in,32);shaFinal(s,out);return true;}

// ---- simulated medium ----
typedef void (*RxHandler)(const uint8_t *, uint8_t, int16_t, int8_t);
RxHandler sim_rx_handler[MAXN];
uint8_t sim_node_key[MAXN][16];

struct QItem { bool used; std::vector<uint8_t> d; uint32_t sendAt; uint8_t defers; bool high; uint32_t enq; };
typedef void (*TxHook)(uint8_t *, uint8_t, uint32_t, uint8_t);
TxHook sim_tx_hook[MAXN];
struct Air { int src; uint32_t start, end; std::vector<uint8_t> d; bool done; };
static QItem q[MAXN][8];
static uint32_t busyUntil[MAXN];
static std::vector<Air> air;           // transmissions in flight or recently finished
static int rssiMat[MAXN][MAXN];        // dBm, -999 = no link
static double lossProb = 0.0;
static const uint32_t AIRTIME_MS = 190;
struct Cap { uint32_t t; int src; std::vector<uint8_t> d; };
static std::vector<Cap> capture;

bool sim_queue_tx(int idx, const uint8_t *buf, uint8_t len, uint32_t delayMs, bool high) {
  for (int i = 0; i < 8; i++) if (!q[idx][i].used) {
    q[idx][i] = {true, std::vector<uint8_t>(buf, buf + len), sim_now + delayMs, 0, high, sim_now};
    return true;
  }
  return false;
}

static bool onAirAt(int node, uint32_t t) {   // is any audible transmission on the air at node?
  for (auto &a : air) if (a.src != node && rssiMat[a.src][node] > -95 && a.start <= t && a.end > t) return true;
  return false;
}

void sim_radio_service(int idx) {
  if (sim_now < busyUntil[idx]) return;
  int best = -1;
  for (int i = 0; i < 8; i++) if (q[idx][i].used && q[idx][i].sendAt <= sim_now) {
    if (best < 0 || q[idx][i].sendAt < q[idx][best].sendAt) best = i;
  }
  if (best < 0) return;
  QItem &it = q[idx][best];
  if (it.defers < 6 && onAirAt(idx, sim_now)) { it.defers++; it.sendAt = sim_now + 30 + rand() % 120; return; }
  if (sim_tx_hook[idx]) sim_tx_hook[idx](it.d.data(), (uint8_t)it.d.size(), sim_now - it.enq, it.defers);   // stamp lane + trace
  air.push_back({idx, sim_now, sim_now + AIRTIME_MS, it.d, false});
  capture.push_back({sim_now, idx, it.d});
  busyUntil[idx] = sim_now + AIRTIME_MS;
  it.used = false;
}

static void mediumStep() {
  for (size_t i = 0; i < air.size();) {
    if (air[i].done) { if (sim_now - air[i].end > 1000) air.erase(air.begin() + i); else i++; continue; }
    if (air[i].end > sim_now) { i++; continue; }
    Air a = air[i];
    air[i].done = true;          // deliver each transmission once (the original simulator re-delivers it every ms for 1 s)
    for (int r = 0; r < MAXN; r++) {
      if (r == a.src || rssiMat[a.src][r] <= -200 || !sim_rx_handler[r]) continue;
      bool ok = true;
      for (auto &o : air) {
        if (&o == &air[i]) continue;
        bool overlap = o.start < a.end && o.end > a.start;
        if (overlap && (o.src == r || rssiMat[o.src][r] > -200)) { ok = false; break; }   // half-duplex or collision
      }
      if (ok && lossProb > 0 && (rand() / (double)RAND_MAX) < lossProb) ok = false;
      if (ok) {
        int save = sim_cur_node; sim_cur_node = r;
        sim_rx_handler[r](a.d.data(), (uint8_t)a.d.size(), (int16_t)rssiMat[a.src][r], 8);
        sim_cur_node = save;
      }
    }
    i++;   // kept for collision checks; dropped above once old
  }
}


// ---- nodes: ids 1, 100 (command node, middle of the line) and 2 ----
namespace NodeA {
#define NODE_ADDR 1
#define NODE_IDX 0
#include "node_body_trace.inc"
#undef NODE_ADDR
#undef NODE_IDX
}
namespace NodeK {
#define NODE_ADDR 100
#define NODE_IDX 1
#include "node_body_trace.inc"
#undef NODE_ADDR
#undef NODE_IDX
}
namespace NodeC {
#define NODE_ADDR 2
#define NODE_IDX 2
#include "node_body_trace.inc"
#undef NODE_ADDR
#undef NODE_IDX
}

struct API {
  void (*init)(); void (*service)(); void (*fireState)();
  void (*testInit)(); void (*testService)(); bool (*serial)(const char *);
  bool *fireActive;
};
#define MK(NS) API{NS::meshInit, NS::meshService, NS::handleFireState, NS::testInit, NS::testService, NS::testHandleSerial, &NS::fireActive}
static API nodes[3] = {MK(NodeA), MK(NodeK), MK(NodeC)};

static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t++) {
    sim_now++;
    for (int i = 0; i < 3; i++) { sim_cur_node = i; nodes[i].service(); nodes[i].fireState(); nodes[i].testService(); }
    mediumStep();
  }
}
static void cmd(int i, const char *line) {
  sim_cur_node = i;
  Serial.print("> "); Serial.println(line);          // echo, like a terminal would not - keeps the log readable
  if (!nodes[i].serial(line)) printf("  (not a test command: %s)\n", line);
}

int main(int argc, char **argv) {
  // argv[1] = output log, argv[2] = "line" (block 1<->2) or "full" (everyone hears everyone), argv[3] = loss prob
  const char *out = argc > 1 ? argv[1] : "sim_all.log";
  bool line = argc > 2 ? strcmp(argv[2], "full") != 0 : true;
  lossProb = argc > 3 ? atof(argv[3]) : 0.0;
  sim_log = fopen(out, "w");
  srand(4242);
  for (int n = 0; n < MAXN; n++) for (int b = 0; b < 16; b++) sim_node_key[n][b] = 0xA0 + b;
  for (int i = 0; i < MAXN; i++) for (int j = 0; j < MAXN; j++) rssiMat[i][j] = -999;
  auto set = [](int a, int b, int v) { rssiMat[a][b] = rssiMat[b][a] = v; };
  set(0, 1, -62); set(1, 2, -71); set(0, 2, -66);     // all three in radio range of each other (a desk)
  for (int i = 0; i < 3; i++) { sim_cur_node = i; nodes[i].init(); nodes[i].testInit(); }
  if (line) { cmd(0, "block 2"); cmd(2, "block 1"); }   // software topology 1 - 100 - 2
  run(70000);                                            // heartbeats

  cmd(1, "test ping 1 12 3");   run(12 * 8000);          // command node -> fire node 1 (adjacent)
  cmd(0, "test ping 2 12 3");   run(12 * 8000);          // fire node 1 -> fire node 2 (2 hops via the command node when blocked)
  cmd(0, "test alarm 3 20 1");  run(3 * 40000 + 5000);   // fire node 1 raises 3 alarm episodes
  cmd(1, "test hb 40 1500");    run(40 * 1600 + 6000);   // command node floods 40 heartbeats
  cmd(2, "stats");
  run(3000);
  fclose(sim_log);
  printf("wrote %s (%s topology, loss %.2f)\n", out, line ? "line 1-100-2" : "full mesh", lossProb);
  return 0;
}
