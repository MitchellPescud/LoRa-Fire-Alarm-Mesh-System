#ifndef MESH_PROTO_H
#define MESH_PROTO_H

// ---------------------------------------------------------------------------
// Mesh wire protocol: pure packet logic (encode / decode / authenticate /
// replay window). No radio, no Arduino dependencies - this file is also
// compiled on a PC by tests/test_proto.cpp, so keep it that way.
//
// Wire format (little-endian), 17-18 bytes per packet:
//
//   [0]    magic0            network ID (layer 2 filter, checked first)
//   [1]    magic1
//   [2]    ver<<4 | type
//   [3]    ttl               hops left. Changes at every relay, so it is
//                            EXCLUDED from the authentication tag.
//   [4]    origin            node that created this packet (relays keep it)
//   [5]    flags
//   [6..9] counter           (boot epoch << 16) | per-boot sequence number
//   [10..] payload           depends on type, see below
//   [-4..] tag               HMAC-SHA256(key, packet with ttl=0, tag excluded)
//                            truncated to 4 bytes
//
//   FIRE      payload 4: episode u16, dustDelta i16      (alarm origin = origin)
//   HEARTBEAT payload 3: battPct u8, tempC10 i16
//   CLEAR     payload 3: alarmOrigin u8, episode u16     (origin = who sent it)
//   SILENCE   payload 3: alarmOrigin u8, episode u16
// ---------------------------------------------------------------------------

#include <stdint.h>
#include <stddef.h>

#define MESH_MAGIC0      0xF1
#define MESH_MAGIC1      0xA7   // change per deployment to isolate separate networks
#define MESH_PROTO_VER   1
#define MESH_HDR_LEN     10
#define MESH_TAG_LEN     4
#define MESH_MAX_PKT     32     // buffer size; largest packet actually built is 18 bytes

#define MESH_ADDR_COMMAND 100   // convention: fire nodes are 1..99, command node is 100

enum MeshType : uint8_t {
  MESH_T_FIRE      = 1,
  MESH_T_HEARTBEAT = 2,
  MESH_T_CLEAR     = 3,
  MESH_T_SILENCE   = 4
};

// flags
#define MESH_F_SILENCED   0x01   // FIRE: the origin's alarm has been silenced
#define MESH_F_ALARM      0x02   // HEARTBEAT: sender has an alarm active
#define MESH_F_EXT_POWER  0x04   // HEARTBEAT: sender is on external 5V

#define MESH_TEMP_NA      ((int16_t)-32768)

struct MeshPacket {
  uint8_t  type;
  uint8_t  ttl;
  uint8_t  origin;
  uint8_t  flags;
  uint32_t counter;

  // FIRE / CLEAR / SILENCE
  uint8_t  alarmOrigin;   // FIRE: set to origin by decode
  uint16_t episode;
  // FIRE
  int16_t  dustDelta;
  // HEARTBEAT
  uint8_t  battPct;
  int16_t  tempC10;
};

enum MeshDecodeResult {
  MESH_OK = 0,
  MESH_ERR_LEN,     // too short / wrong length for its type
  MESH_ERR_MAGIC,   // not our network
  MESH_ERR_VER,     // unsupported protocol version
  MESH_ERR_TYPE,    // unknown type
  MESH_ERR_AUTH     // HMAC tag mismatch
};

// Builds header + payload + tag into buf (>= MESH_MAX_PKT bytes). Returns length.
size_t meshEncode(const MeshPacket &p, uint8_t *buf);

// Checks magic, version, type and length, verifies the tag, then parses.
// `out` is only valid on MESH_OK.
MeshDecodeResult meshDecode(const uint8_t *buf, size_t len, MeshPacket &out);

// HMAC-SHA256 backend (mesh_hmac.cpp on the ESP32, a stub in the PC tests).
bool meshHmacSha256(const uint8_t *key, size_t keyLen,
                    const uint8_t *data, size_t len, uint8_t out[32]);

// Key used for tags. Defaults to MESH_KEY from secrets.h; tests override it.
void meshSetKey(const uint8_t *key, size_t keyLen);

// ESP32 only (mesh_hmac.cpp): loads MESH_KEY from secrets.h via meshSetKey().
void meshLoadKey();

// ---- Replay / duplicate window (per origin) ------------------------------
// Sliding 32-packet window on the 32-bit counter. Call ONLY after the tag has
// been verified. NEW = first time seen (accept); DUP = already seen inside the
// window (flood duplicate, drop); OLD = older than the window (replay, drop).
struct MeshReplayWindow {
  bool     valid;
  uint32_t highest;
  uint32_t bitmap;   // bit n set = (highest - n) has been seen
};
enum MeshReplayResult { MESH_RP_NEW = 0, MESH_RP_DUP, MESH_RP_OLD };
MeshReplayResult meshReplayCheck(MeshReplayWindow &w, uint32_t counter);

// Wrap-safe episode comparison: >0 if a is newer than b, 0 equal, <0 older.
static inline int16_t meshEpisodeDiff(uint16_t a, uint16_t b) { return (int16_t)(uint16_t)(a - b); }

#endif
