// Host-side test for mesh_proto.*: g++ -std=c++11 -I.. test_proto.cpp ../mesh_proto.cpp -o test_proto
// Includes a small SHA-256/HMAC so it needs no external crypto library; the
// golden vector below was generated independently with Python's hmac module.
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "mesh_proto.h"

// ---- minimal SHA-256 / HMAC (test only) ----
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
bool meshHmacSha256(const uint8_t*key,size_t kl,const uint8_t*d,size_t n,uint8_t out[32]){
 uint8_t k[64]={0},ip[64],op[64];if(kl>64)return false;memcpy(k,key,kl);for(int i=0;i<64;i++){ip[i]=k[i]^0x36;op[i]=k[i]^0x5c;}
 Sha s;uint8_t in[32];shaInit(s);shaUpdate(s,ip,64);shaUpdate(s,d,n);shaFinal(s,in);shaInit(s);shaUpdate(s,op,64);shaUpdate(s,in,32);shaFinal(s,out);return true;}

static int fails=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL line %d: %s\n",__LINE__,#c); fails++; } }while(0)

int main(){
  uint8_t key[16]; for(int i=0;i<16;i++) key[i]=0xA0+i;
  meshSetKey(key,16);

  // golden vector (matches tests/golden.py)
  MeshPacket f; memset(&f,0,sizeof f);
  f.type=MESH_T_FIRE; f.ttl=5; f.origin=1; f.flags=0; f.counter=0x00010002; f.episode=7; f.dustDelta=-3;
  uint8_t b[MESH_MAX_PKT]; size_t n=meshEncode(f,b);
  CHECK(n==18);
  printf("FIRE: "); for(size_t i=0;i<n;i++) printf("%02x",b[i]); printf("\n");

  MeshPacket d; CHECK(meshDecode(b,n,d)==MESH_OK);
  CHECK(d.type==MESH_T_FIRE && d.origin==1 && d.alarmOrigin==1 && d.counter==0x00010002 && d.episode==7 && d.dustDelta==-3 && d.ttl==5);

  // relay: TTL change must not break the tag
  uint8_t r[MESH_MAX_PKT]; memcpy(r,b,n); r[3]=4; CHECK(meshDecode(r,n,d)==MESH_OK && d.ttl==4);
  // any other byte flipped must fail auth
  for(size_t i=4;i<n;i++){ memcpy(r,b,n); r[i]^=0x01; MeshDecodeResult x=meshDecode(r,n,d); CHECK(x==MESH_ERR_AUTH); }
  // wrong key
  uint8_t key2[16]; memcpy(key2,key,16); key2[0]^=1; meshSetKey(key2,16); CHECK(meshDecode(b,n,d)==MESH_ERR_AUTH); meshSetKey(key,16);
  // structure errors
  CHECK(meshDecode(b,n-1,d)==MESH_ERR_LEN);
  CHECK(meshDecode(b,5,d)==MESH_ERR_LEN);
  memcpy(r,b,n); r[0]^=1; CHECK(meshDecode(r,n,d)==MESH_ERR_MAGIC);
  memcpy(r,b,n); r[2]=(2<<4)|1; CHECK(meshDecode(r,n,d)==MESH_ERR_VER);
  memcpy(r,b,n); r[2]=(1<<4)|9; CHECK(meshDecode(r,n,d)==MESH_ERR_TYPE);

  // other types round trip
  MeshPacket h; memset(&h,0,sizeof h); h.type=MESH_T_HEARTBEAT; h.ttl=3; h.origin=2; h.flags=MESH_F_EXT_POWER; h.counter=5; h.battPct=87; h.tempC10=-125;
  n=meshEncode(h,b); CHECK(n==17); CHECK(meshDecode(b,n,d)==MESH_OK && d.battPct==87 && d.tempC10==-125 && (d.flags&MESH_F_EXT_POWER));
  printf("HB:   "); for(size_t i=0;i<n;i++) printf("%02x",b[i]); printf("\n");
  MeshPacket c; memset(&c,0,sizeof c); c.type=MESH_T_CLEAR; c.ttl=5; c.origin=100; c.counter=0x00030001; c.alarmOrigin=2; c.episode=65535;
  n=meshEncode(c,b); CHECK(n==17); CHECK(meshDecode(b,n,d)==MESH_OK && d.alarmOrigin==2 && d.episode==65535 && d.origin==100);
  c.type=MESH_T_SILENCE; n=meshEncode(c,b); CHECK(meshDecode(b,n,d)==MESH_OK && d.type==MESH_T_SILENCE);
  // a CLEAR must not authenticate as a SILENCE (type is covered by the tag)
  c.type=MESH_T_CLEAR; n=meshEncode(c,b); b[2]=(1<<4)|MESH_T_SILENCE; CHECK(meshDecode(b,n,d)==MESH_ERR_AUTH);

  // remote command packets
  MeshPacket q; memset(&q,0,sizeof q); q.type=MESH_T_CMD; q.ttl=5; q.origin=100; q.counter=0x00020003; q.target=7; q.cmd=MESH_CMD_REBOOT; q.reqId=200;
  n=meshEncode(q,b); CHECK(n==17); CHECK(meshDecode(b,n,d)==MESH_OK && d.type==MESH_T_CMD && d.target==7 && d.cmd==MESH_CMD_REBOOT && d.reqId==200 && d.origin==100);
  printf("CMD:  "); for(size_t i=0;i<n;i++) printf("%02x",b[i]); printf("\n");
  for(size_t i=4;i<n;i++){ memcpy(r,b,n); r[i]^=0x01; CHECK(meshDecode(r,n,d)==MESH_ERR_AUTH); }   // target/cmd/reqId are all covered by the tag
  memcpy(r,b,n); r[2]=(1<<4)|MESH_T_ACK; CHECK(meshDecode(r,n,d)!=MESH_OK);                        // CMD cannot be turned into an ACK
  MeshPacket k; memset(&k,0,sizeof k); k.type=MESH_T_ACK; k.ttl=5; k.origin=7; k.counter=9; k.target=100; k.cmd=MESH_CMD_REBOOT; k.reqId=200; k.result=MESH_ACK_BUSY;
  n=meshEncode(k,b); CHECK(n==18); CHECK(meshDecode(b,n,d)==MESH_OK && d.type==MESH_T_ACK && d.target==100 && d.reqId==200 && d.result==MESH_ACK_BUSY && d.origin==7);
  CHECK(meshDecode(b,n-1,d)==MESH_ERR_LEN);
  memcpy(r,b,n); r[2]=(1<<4)|7; CHECK(meshDecode(r,n,d)==MESH_ERR_TYPE);

  // replay window
  MeshReplayWindow w; memset(&w,0,sizeof w);
  CHECK(meshReplayCheck(w,100)==MESH_RP_NEW);
  CHECK(meshReplayCheck(w,100)==MESH_RP_DUP);
  CHECK(meshReplayCheck(w,101)==MESH_RP_NEW);
  CHECK(meshReplayCheck(w,99)==MESH_RP_NEW);      // late but inside window
  CHECK(meshReplayCheck(w,99)==MESH_RP_DUP);
  CHECK(meshReplayCheck(w,130)==MESH_RP_NEW);
  CHECK(meshReplayCheck(w,100)==MESH_RP_DUP);     // still inside window (130-100=30)
  CHECK(meshReplayCheck(w,98)==MESH_RP_OLD);      // 32 behind -> too old
  CHECK(meshReplayCheck(w,130+65536)==MESH_RP_NEW);  // epoch bump (reboot)
  CHECK(meshReplayCheck(w,131)==MESH_RP_OLD);        // pre-reboot packet replayed
  CHECK(meshReplayCheck(w,130+65536)==MESH_RP_DUP);

  // episode wrap
  CHECK(meshEpisodeDiff(1,65535)>0); CHECK(meshEpisodeDiff(65535,1)<0); CHECK(meshEpisodeDiff(7,7)==0);

  printf(fails? "%d FAILURES\n":"ALL TESTS PASSED\n", fails);
  return fails?1:0;
}
