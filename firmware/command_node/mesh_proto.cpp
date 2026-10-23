#include <string.h>
#include "mesh_proto.h"

static uint8_t  g_key[32];
static size_t   g_keyLen = 0;

void meshSetKey(const uint8_t *key, size_t keyLen) {
  if (keyLen > sizeof(g_key)) keyLen = sizeof(g_key);
  memcpy(g_key, key, keyLen);
  g_keyLen = keyLen;
}

static void put16(uint8_t *b, uint16_t v) { b[0] = v & 0xFF; b[1] = v >> 8; }
static void put32(uint8_t *b, uint32_t v) { b[0] = v; b[1] = v >> 8; b[2] = v >> 16; b[3] = v >> 24; }
static uint16_t get16(const uint8_t *b) { return (uint16_t)b[0] | ((uint16_t)b[1] << 8); }
static uint32_t get32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static size_t payloadLen(uint8_t type) {
  switch (type) {
    case MESH_T_FIRE:      return 4;
    case MESH_T_HEARTBEAT: return 3;
    case MESH_T_CLEAR:
    case MESH_T_SILENCE:   return 3;
    case MESH_T_CMD:       return 3;
    case MESH_T_ACK:       return 4;
    default:               return 0;
  }
}

// Tag over everything except the tag itself, with the TTL byte zeroed so
// relays can decrement it without invalidating the tag.
static bool computeTag(const uint8_t *buf, size_t bodyLen, uint8_t tag[MESH_TAG_LEN]) {
  uint8_t tmp[MESH_MAX_PKT];
  if (bodyLen > sizeof(tmp) || g_keyLen == 0) return false;
  memcpy(tmp, buf, bodyLen);
  tmp[3] = 0;
  uint8_t mac[32];
  if (!meshHmacSha256(g_key, g_keyLen, tmp, bodyLen, mac)) return false;
  memcpy(tag, mac, MESH_TAG_LEN);
  return true;
}

size_t meshEncode(const MeshPacket &p, uint8_t *buf) {
  buf[0] = MESH_MAGIC0;
  buf[1] = MESH_MAGIC1;
  buf[2] = (uint8_t)((MESH_PROTO_VER << 4) | (p.type & 0x0F));
  buf[3] = p.ttl;
  buf[4] = p.origin;
  buf[5] = p.flags;
  put32(buf + 6, p.counter);

  uint8_t *pl = buf + MESH_HDR_LEN;
  switch (p.type) {
    case MESH_T_FIRE:
      put16(pl, p.episode);
      put16(pl + 2, (uint16_t)p.dustDelta);
      break;
    case MESH_T_HEARTBEAT:
      pl[0] = p.battPct;
      put16(pl + 1, (uint16_t)p.tempC10);
      break;
    case MESH_T_CLEAR:
    case MESH_T_SILENCE:
      pl[0] = p.alarmOrigin;
      put16(pl + 1, p.episode);
      break;
    case MESH_T_CMD:
      pl[0] = p.target;
      pl[1] = p.cmd;
      pl[2] = p.reqId;
      break;
    case MESH_T_ACK:
      pl[0] = p.target;
      pl[1] = p.cmd;
      pl[2] = p.reqId;
      pl[3] = p.result;
      break;
    default:
      break;
  }
  size_t body = MESH_HDR_LEN + payloadLen(p.type);
  uint8_t tag[MESH_TAG_LEN] = {0};
  computeTag(buf, body, tag);
  memcpy(buf + body, tag, MESH_TAG_LEN);
  return body + MESH_TAG_LEN;
}

MeshDecodeResult meshDecode(const uint8_t *buf, size_t len, MeshPacket &out) {
  if (len < MESH_HDR_LEN + MESH_TAG_LEN) return MESH_ERR_LEN;
  if (buf[0] != MESH_MAGIC0 || buf[1] != MESH_MAGIC1) return MESH_ERR_MAGIC;
  if ((buf[2] >> 4) != MESH_PROTO_VER) return MESH_ERR_VER;
  uint8_t type = buf[2] & 0x0F;
  if (type < MESH_T_FIRE || type > MESH_T_ACK) return MESH_ERR_TYPE;
  size_t body = MESH_HDR_LEN + payloadLen(type);
  if (len != body + MESH_TAG_LEN) return MESH_ERR_LEN;

  uint8_t tag[MESH_TAG_LEN];
  if (!computeTag(buf, body, tag)) return MESH_ERR_AUTH;
  uint8_t diff = 0;                                   // constant-time compare
  for (int i = 0; i < MESH_TAG_LEN; i++) diff |= tag[i] ^ buf[body + i];
  if (diff != 0) return MESH_ERR_AUTH;

  memset(&out, 0, sizeof(out));
  out.type    = type;
  out.ttl     = buf[3];
  out.origin  = buf[4];
  out.flags   = buf[5];
  out.counter = get32(buf + 6);
  const uint8_t *pl = buf + MESH_HDR_LEN;
  switch (type) {
    case MESH_T_FIRE:
      out.alarmOrigin = out.origin;
      out.episode     = get16(pl);
      out.dustDelta   = (int16_t)get16(pl + 2);
      break;
    case MESH_T_HEARTBEAT:
      out.battPct = pl[0];
      out.tempC10 = (int16_t)get16(pl + 1);
      break;
    case MESH_T_CLEAR:
    case MESH_T_SILENCE:
      out.alarmOrigin = pl[0];
      out.episode     = get16(pl + 1);
      break;
    case MESH_T_CMD:
      out.target = pl[0];
      out.cmd    = pl[1];
      out.reqId  = pl[2];
      break;
    case MESH_T_ACK:
      out.target = pl[0];
      out.cmd    = pl[1];
      out.reqId  = pl[2];
      out.result = pl[3];
      break;
  }
  return MESH_OK;
}

MeshReplayResult meshReplayCheck(MeshReplayWindow &w, uint32_t counter) {
  if (!w.valid) {
    w.valid = true;
    w.highest = counter;
    w.bitmap = 1;
    return MESH_RP_NEW;
  }
  if (counter > w.highest) {
    uint32_t shift = counter - w.highest;
    w.bitmap = (shift >= 32) ? 0 : (w.bitmap << shift);
    w.bitmap |= 1;
    w.highest = counter;
    return MESH_RP_NEW;
  }
  uint32_t back = w.highest - counter;
  if (back >= 32) return MESH_RP_OLD;
  uint32_t bit = (uint32_t)1 << back;
  if (w.bitmap & bit) return MESH_RP_DUP;
  w.bitmap |= bit;
  return MESH_RP_NEW;
}
