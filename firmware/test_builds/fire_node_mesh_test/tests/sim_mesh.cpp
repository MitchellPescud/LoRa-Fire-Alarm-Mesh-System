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

void SerialStub::flush(bool nl) {
  if (!nl) return;
  if (sim_verbose) printf("[%6u n%d] %s\n", sim_now, sim_cur_node, line.c_str());
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

struct QItem { bool used; std::vector<uint8_t> d; uint32_t sendAt; uint8_t defers; bool high; };
struct Air { int src; uint32_t start, end; std::vector<uint8_t> d; };
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
    q[idx][i] = {true, std::vector<uint8_t>(buf, buf + len), sim_now + delayMs, 0, high};
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
  air.push_back({idx, sim_now, sim_now + AIRTIME_MS, it.d});
  capture.push_back({sim_now, idx, it.d});
  busyUntil[idx] = sim_now + AIRTIME_MS;
  it.used = false;
}

static void mediumStep() {
  for (size_t i = 0; i < air.size();) {
    Air a = air[i];
    if (a.end > sim_now) { i++; continue; }
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
    // keep recent entries for collision checks; drop old ones
    if (sim_now - a.end > 1000) air.erase(air.begin() + i); else i++;
  }
}

// ---- nodes ----
namespace NodeA {
#define NODE_ADDR 1
#define NODE_IDX 0
#include "node_body.inc"
#undef NODE_ADDR
#undef NODE_IDX
}
namespace NodeB {
#define NODE_ADDR 2
#define NODE_IDX 1
#include "node_body.inc"
#undef NODE_ADDR
#undef NODE_IDX
}
namespace NodeC {
#define NODE_ADDR 3
#define NODE_IDX 2
#include "node_body.inc"
#undef NODE_ADDR
#undef NODE_IDX
}
namespace NodeK {
#define NODE_ADDR 100
#define NODE_IDX 3
#include "node_body.inc"
#undef NODE_ADDR
#undef NODE_IDX
}

struct API {
  const char *name;
  void (*init)(); void (*service)(); void (*fireState)();
  void (*trigger)(); void (*clear)(const char *); void (*silence)();
  bool *fireActive; bool (*localActive)(); bool (*rearmHeld)(); bool (*silenced)();
  int (*alarmCount)(); uint8_t (*firstOrigin)(); int (*peerCount)();
  uint32_t *rxDup, *rxReplay, *rxAuth, *rxForeign, *rxCleared;
  std::string (*json)();
  int (*clearOrigin)(uint8_t); int (*silenceOrigin)(uint8_t);
  int (*sendCmd)(uint8_t, uint8_t); int (*cmdState)(uint8_t, uint8_t, uint8_t *);
  int *cmdExec; uint8_t *cmdLast; uint8_t *cmdResult; uint32_t *rxCmdRejected;
};
#define MK(NS, NM) API{NM, NS::meshInit, NS::meshService, NS::handleFireState, NS::triggerAlarm, NS::clearAlarm, NS::silenceAlarm, \
  &NS::fireActive, NS::meshLocalEpisodeActive, NS::meshRearmHeld, NS::simBuzzerSilenced, NS::meshActiveAlarmCount, \
  NS::meshFirstAlarmOrigin, NS::meshPeerCount, &NS::meshStats.rxDup, &NS::meshStats.rxReplay, &NS::meshStats.rxAuthFail, \
  &NS::meshStats.rxBadMagic, &NS::meshStats.rxClearedDrop, []() { return std::string(NS::meshStatusJson().c_str()); }, NS::meshClearOrigin, NS::meshSilenceOrigin, \
  NS::meshSendCommand, NS::meshCommandState, &NS::simCmdExecCount, &NS::simCmdLast, &NS::simCmdResult, &NS::meshStats.rxCmdRejected}
static API nodes[MAXN] = {MK(NodeA, "A"), MK(NodeB, "B"), MK(NodeC, "C"), MK(NodeK, "K")};
static int NN = 3;

static void run(uint32_t ms) {
  for (uint32_t t = 0; t < ms; t++) {
    sim_now++;
    for (int i = 0; i < NN; i++) { sim_cur_node = i; nodes[i].service(); nodes[i].fireState(); }
    mediumStep();
  }
}
static void call(int i, std::function<void()> f) { sim_cur_node = i; f(); }

static int fails = 0, checks = 0;
#define CHECK(c, msg) do { checks++; if (!(c)) { fails++; printf("  FAIL: %s  [%s:%d]\n", msg, __FILE__, __LINE__); } } while (0)

static void links(int ab, int bc, int ac) {   // set symmetric link strengths (-999 = none)
  for (int i = 0; i < MAXN; i++) for (int j = 0; j < MAXN; j++) rssiMat[i][j] = -999;
  auto set = [](int a, int b, int v) { rssiMat[a][b] = rssiMat[b][a] = v; };
  set(0, 1, ab); set(1, 2, bc); set(0, 2, ac);
  set(3, 2, -70);   // command node K is next to C
  set(3, 1, -999); set(3, 0, -999);
}

static void bootAll() {
  for (int i = 0; i < NN; i++) { sim_cur_node = i; nodes[i].init(); }
}

int main(int argc, char **argv) {
  sim_verbose = (argc > 1);
  srand(12345);
  for (int n = 0; n < MAXN; n++) for (int b = 0; b < 16; b++) sim_node_key[n][b] = 0xA0 + b;   // shared PSK
  links(-80, -80, -999);   // A -- B -- C line, A and C cannot hear each other
  bootAll();

  printf("S1  heartbeats build the peer table across a line topology\n");
  run(70000);
  CHECK(nodes[0].peerCount() == 2, "A sees B and (relayed) C");
  CHECK(nodes[2].peerCount() == 2, "C sees B and (relayed) A");
  CHECK(nodes[1].peerCount() == 2, "B sees A and C");
  CHECK(!*nodes[0].fireActive && !*nodes[1].fireActive && !*nodes[2].fireActive, "no alarms yet");

  printf("S2  fire at A reaches C only through B\n");
  call(0, nodes[0].trigger);
  run(4000);
  CHECK(*nodes[0].fireActive && nodes[0].localActive(), "A alarm active and local");
  CHECK(*nodes[1].fireActive && !nodes[1].localActive(), "B alarm active (remote)");
  CHECK(*nodes[2].fireActive && !nodes[2].localActive(), "C alarm active (remote, 2 hops)");
  CHECK(nodes[2].firstOrigin() == 1, "C knows the alarm came from node 1");
  CHECK(sim_buzz_highs[2] > 0, "C buzzer chirped");
  run(12000);
  CHECK(nodes[2].alarmCount() == 1 && nodes[1].alarmCount() == 1, "re-announcements do not create duplicate alarms");
  CHECK(*nodes[2].rxDup > 0 || *nodes[0].rxDup > 0, "flood duplicates were detected and dropped");

  printf("S3  silence from C mutes everyone, alarm stays\n");
  int b1 = sim_buzz_highs[1], b0 = sim_buzz_highs[0];
  call(2, nodes[2].silence);
  run(6000);
  CHECK(nodes[0].silenced() && nodes[1].silenced() && nodes[2].silenced(), "A, B, C silenced");
  CHECK(*nodes[0].fireActive && *nodes[1].fireActive && *nodes[2].fireActive, "alarm still active everywhere");
  int b1a = sim_buzz_highs[1], b0a = sim_buzz_highs[0];
  run(5000);
  CHECK(sim_buzz_highs[1] == b1a && sim_buzz_highs[0] == b0a, "no further chirps on A/B after silence");
  (void)b1; (void)b0;

  printf("S4  clear from B ends the alarm network-wide; sensor hold-off at A\n");
  call(1, [] { nodes[1].clear("[test] clear"); });
  run(6000);
  CHECK(!*nodes[0].fireActive && !*nodes[1].fireActive && !*nodes[2].fireActive, "all alarms cleared");
  CHECK(!nodes[0].localActive(), "A stopped its episode");
  CHECK(nodes[0].rearmHeld(), "A is in re-arm hold-off");
  uint32_t clearedBefore = *nodes[2].rxCleared;
  run(30000);
  CHECK(!*nodes[2].fireActive && !*nodes[1].fireActive, "no zombie alarm after clear");
  CHECK(nodes[0].rearmHeld(), "hold-off still running at +36 s");
  run(30000);
  CHECK(!nodes[0].rearmHeld(), "hold-off over after 60 s");
  (void)clearedBefore;

  printf("S5  a NEW episode after a clear is not suppressed\n");
  call(0, nodes[0].trigger);
  run(4000);
  CHECK(*nodes[1].fireActive && *nodes[2].fireActive, "episode 2 reaches B and C");
  CHECK(nodes[0].json().find("\"localEpisode\":2") != std::string::npos, "A is on episode 2");
  CHECK(!nodes[2].silenced(), "new episode is not silenced");

  printf("S6  replayed old CLEAR (captured earlier) does not end the new alarm\n");
  std::vector<uint8_t> oldClear;
  for (auto &c : capture) if (c.d.size() > 2 && (c.d[2] & 0x0F) == 3 /*CLEAR*/ ) { oldClear = c.d; break; }
  CHECK(!oldClear.empty(), "captured a CLEAR");
  uint32_t dupBefore = *nodes[2].rxDup + *nodes[2].rxReplay;
  sim_cur_node = 2; sim_rx_handler[2](oldClear.data(), (uint8_t)oldClear.size(), -60, 8);
  CHECK(*nodes[2].rxDup + *nodes[2].rxReplay == dupBefore + 1, "replay rejected by the replay window");
  CHECK(*nodes[2].fireActive, "alarm on C unaffected");
  // Even after C reboots (window lost), an OLD clear can only clear OLD episodes:
  sim_cur_node = 2; nodes[2].init();
  sim_cur_node = 2; sim_rx_handler[2](oldClear.data(), (uint8_t)oldClear.size(), -60, 8);
  run(7000);
  CHECK(*nodes[2].fireActive && nodes[2].firstOrigin() == 1, "episode 2 still (re)learned and active after reboot + old CLEAR");

  printf("S7  forged and foreign packets are dropped\n");
  uint32_t authBefore = *nodes[1].rxAuth, foreignBefore = *nodes[1].rxForeign;
  std::vector<uint8_t> forged = oldClear; forged[forged.size() - 1] ^= 0xFF;       // bad tag
  sim_cur_node = 1; sim_rx_handler[1](forged.data(), (uint8_t)forged.size(), -50, 8);
  CHECK(*nodes[1].rxAuth == authBefore + 1, "bad HMAC counted and dropped");
  std::vector<uint8_t> foreign = oldClear; foreign[0] ^= 0x01;
  sim_cur_node = 1; sim_rx_handler[1](foreign.data(), (uint8_t)foreign.size(), -50, 8);
  CHECK(*nodes[1].rxForeign == foreignBefore + 1, "foreign network packet counted and dropped");
  CHECK(*nodes[1].fireActive, "alarm on B unaffected");

  printf("S8  command node K (next to C) clears everything\n");
  NN = 4; sim_cur_node = 3; nodes[3].init();
  run(40000);                                  // let K learn the network
  CHECK(nodes[3].peerCount() >= 1, "K hears the network");
  CHECK(*nodes[3].fireActive, "K itself sounds/raises the alarm too");
  call(3, [] { nodes[3].clear("[test] clear from command node"); });
  run(8000);
  CHECK(!*nodes[0].fireActive && !*nodes[1].fireActive && !*nodes[2].fireActive && !*nodes[3].fireActive, "K's CLEAR reached A, B, C (A is 3 hops from K)");
  NN = 3;
  run(65000);

  printf("S9  two simultaneous alarms from hidden nodes are both tracked; clear is network-wide\n");
  call(0, nodes[0].trigger);
  call(2, nodes[2].trigger);
  run(14000);   // first FIRE from A and C collide at B (hidden nodes); jittered re-announces then get through
  CHECK(nodes[1].alarmCount() == 2, "B tracks both alarms");
  CHECK(nodes[0].alarmCount() == 2 && nodes[2].alarmCount() == 2, "A and C track both alarms");
  // node C clears only... clear is network-wide (all active) by design; verify both go
  call(2, [] { nodes[2].clear("[test]"); });
  run(6000);
  CHECK(nodes[1].alarmCount() == 0 && *nodes[0].fireActive == false, "clear is network-wide for all active alarms");
  run(65000);

  printf("S9b command node clears / silences ONE node's alarm; others keep ringing\n");
  NN = 4;
  call(0, nodes[0].trigger);
  call(2, nodes[2].trigger);
  run(16000);
  CHECK(nodes[3].alarmCount() == 2, "K knows both alarms");
  call(3, [] { nodes[3].silenceOrigin(1); });
  run(8000);
  {
    std::string jb = nodes[1].json(), jc = nodes[2].json();
    auto count = [](const std::string &h, const char *n) { int c = 0; for (size_t p = h.find(n); p != std::string::npos; p = h.find(n, p + 1)) c++; return c; };
    CHECK(count(jb, "\"silenced\":true") == 1 && count(jb, "\"silenced\":false") == 1, "B: only node 1's alarm is silenced");
    CHECK(count(jc, "\"silenced\":true") == 1 && count(jc, "\"silenced\":false") == 1, "C: only node 1's alarm is silenced");
    CHECK(!nodes[1].silenced(), "B buzzer still sounds for node 3's alarm");
  }
  call(3, [] { nodes[3].clearOrigin(1); });
  run(8000);
  CHECK(nodes[1].alarmCount() == 1 && nodes[1].firstOrigin() == 3, "B only has node 3's alarm left");
  CHECK(!nodes[0].localActive() && nodes[2].localActive(), "A ended its episode, C keeps announcing its own");
  CHECK(*nodes[1].fireActive && *nodes[3].fireActive, "network still alarming for node 3");
  call(3, [] { nodes[3].clearOrigin(0); });
  run(8000);
  CHECK(!*nodes[0].fireActive && !*nodes[1].fireActive && !*nodes[2].fireActive && !*nodes[3].fireActive, "clear-all finishes the job");
  NN = 3;
  run(65000);

  printf("S10 30%% packet loss + hidden-node collisions: 25 trials\n");
  lossProb = 0.30;
  int reached = 0, cleared = 0;
  for (int trial = 0; trial < 25; trial++) {
    call(0, nodes[0].trigger);
    run(20000);
    if (*nodes[2].fireActive) reached++;
    call(2, [] { nodes[2].clear("[test]"); });
    run(15000);
    if (!*nodes[0].fireActive && !*nodes[1].fireActive && !*nodes[2].fireActive) cleared++;
    else { for (int i = 0; i < 3; i++) if (*nodes[i].fireActive) { call(i, [i] { nodes[i].clear("[test] retry"); }); } run(10000); }
    run(61000);   // re-arm hold-off
  }
  printf("    FIRE reached C within 20 s: %d/25    network-wide CLEAR within 15 s: %d/25\n", reached, cleared);
  CHECK(reached >= 24, "FIRE delivery >= 24/25 at 30% loss");
  CHECK(cleared >= 23, "CLEAR delivery >= 23/25 at 30% loss");
  lossProb = 0;

  printf("S11 remote commands: A -> C across B (line topology)\n");
  run(40000);   // fresh heartbeats so every node has a replay baseline for every other node
  {
    uint8_t res = 99;
    int r1 = 0, r2 = 0, r3 = 0, r4 = 0;
    call(0, [&] { r1 = nodes[0].sendCmd(1 /*self*/, 2); r2 = nodes[0].sendCmd(3, 9 /*bad cmd*/); r3 = nodes[0].sendCmd(0, 2); });
    CHECK(r1 == -1 && r2 == -1 && r3 == -1, "self / unknown command / node 0 are refused");

    // normal request: executed exactly once, ACK comes back over two hops
    int c0 = *nodes[0].cmdExec, c1 = *nodes[1].cmdExec, c2 = *nodes[2].cmdExec;
    call(0, [&] { r1 = nodes[0].sendCmd(3, 2 /*RECALIBRATE*/); r2 = nodes[0].sendCmd(3, 2); });
    CHECK(r1 == 0 && r2 == 1, "first request queued, identical pending one reported");
    CHECK(nodes[0].cmdState(3, 2, &res) == 0, "state is PENDING right after sending");
    run(6000);
    CHECK(nodes[0].cmdState(3, 2, &res) == 1 && res == 0, "ACK received: DONE / OK");
    CHECK(*nodes[2].cmdExec == c2 + 1 && *nodes[2].cmdLast == 2, "C ran RECALIBRATE exactly once");
    CHECK(*nodes[1].cmdExec == c1 && *nodes[0].cmdExec == c0, "B (relay) and A (sender) did not execute it");

    // result codes travel back
    *nodes[2].cmdResult = 2;   // BUSY
    call(0, [&] { r1 = nodes[0].sendCmd(3, 1 /*TEST_FIRE*/); });
    run(6000);
    CHECK(nodes[0].cmdState(3, 1, &res) == 1 && res == 2, "BUSY result delivered to the asker");
    *nodes[2].cmdResult = 0;

    // lost ACKs: B -> A link cut, so A gets no ACK. All repeats reach C but it must still run once.
    rssiMat[1][0] = -999;
    c2 = *nodes[2].cmdExec;
    call(0, [&] { r1 = nodes[0].sendCmd(3, 3 /*REBOOT*/); });
    run(16000);
    CHECK(*nodes[2].cmdExec == c2 + 1, "3 repeated copies -> executed once (de-duplicated)");
    CHECK(nodes[0].cmdState(3, 3, &res) == 2, "no ACK within the wait -> NO REPLY");
    rssiMat[1][0] = -80;

    // the same request sent again later is a NEW request (new reqId) and runs again
    call(0, [&] { r1 = nodes[0].sendCmd(3, 3); });
    run(6000);
    CHECK(*nodes[2].cmdExec == c2 + 2 && nodes[0].cmdState(3, 3, &res) == 1, "a fresh request runs again and is ACKed");

    // STATUS is answered by the mesh layer itself (no application hook)
    c2 = *nodes[2].cmdExec;
    call(0, [&] { r1 = nodes[0].sendCmd(3, 4); });
    run(6000);
    CHECK(nodes[0].cmdState(3, 4, &res) == 1 && res == 0 && *nodes[2].cmdExec == c2, "STATUS acked without touching the app hook");

    // captured CMD replayed after the target reboots must not execute
    std::vector<uint8_t> oldCmd;
    for (auto &c : capture) if (c.d.size() > 2 && (c.d[2] & 0x0F) == 5 && c.d[4] == 1 /*from A*/) { oldCmd = c.d; break; }
    CHECK(!oldCmd.empty(), "captured a CMD");
    sim_cur_node = 2; nodes[2].init();                 // C reboots: replay window empty
    c2 = *nodes[2].cmdExec;
    uint32_t rejBefore = *nodes[2].rxCmdRejected;
    sim_cur_node = 2; sim_rx_handler[2](oldCmd.data(), (uint8_t)oldCmd.size(), -60, 8);
    CHECK(*nodes[2].cmdExec == c2 && *nodes[2].rxCmdRejected == rejBefore + 1, "replayed CMD right after reboot refused (no baseline yet)");
    sim_cur_node = 2; sim_rx_handler[2](oldCmd.data(), (uint8_t)oldCmd.size(), -60, 8);
    CHECK(*nodes[2].cmdExec == c2, "...and again: still not executed");

    // forged CMD
    uint32_t authBefore2 = *nodes[2].rxAuth;
    std::vector<uint8_t> forgedCmd = oldCmd; forgedCmd[10] = 3; /* target stays, flip cmd byte */ forgedCmd[11] ^= 0x07;
    sim_cur_node = 2; sim_rx_handler[2](forgedCmd.data(), (uint8_t)forgedCmd.size(), -60, 8);
    CHECK(*nodes[2].rxAuth == authBefore2 + 1 && *nodes[2].cmdExec == c2, "forged CMD dropped by the HMAC");

    // after C has heard A's heartbeat again, genuine commands work again
    run(40000);
    call(0, [&] { r1 = nodes[0].sendCmd(3, 2); });
    run(6000);
    CHECK(nodes[0].cmdState(3, 2, &res) == 1 && *nodes[2].cmdExec == c2 + 1, "after re-learning A, commands to C work again");
  }

  printf("S12 command node K commands fire node A (3 hops away) and gets the ACK\n");
  NN = 4; sim_cur_node = 3; nodes[3].init();
  run(70000);   // K learns the network, A/B/C learn K
  {
    uint8_t res = 99; int r = -9;
    int a0 = *nodes[0].cmdExec;
    call(3, [&] { r = nodes[3].sendCmd(1, 2); });
    run(8000);
    CHECK(r == 0 && nodes[3].cmdState(1, 2, &res) == 1 && res == 0, "K got the ACK from A");
    CHECK(*nodes[0].cmdExec == a0 + 1, "A executed it once");
  }
  NN = 3;

  printf("\n%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
