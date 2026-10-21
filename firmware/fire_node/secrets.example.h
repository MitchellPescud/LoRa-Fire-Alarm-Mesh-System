#ifndef SECRETS_H
#define SECRETS_H

// ---------------------------------------------------------------------------
// Pre-shared mesh network key (128-bit), used for the HMAC on every packet.
// It MUST be identical on every node, including the command node.
//
// SETUP: copy this file to `secrets.h` (git-ignored) and replace the bytes
// below with your own random key, e.g.
//   python3 -c "import secrets;print(', '.join('0x%02X'%b for b in secrets.token_bytes(16)))"
// To rotate the key, change it and re-flash ALL nodes.
// ---------------------------------------------------------------------------
static const uint8_t MESH_KEY[16] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00   // CHANGE ME
};

#endif
