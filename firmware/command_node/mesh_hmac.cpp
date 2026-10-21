#include <Arduino.h>
#include "mbedtls/md.h"
#include "mesh_proto.h"
#include "secrets.h"

// HMAC-SHA256 via the ESP32's mbedtls (hardware-accelerated SHA).
bool meshHmacSha256(const uint8_t *key, size_t keyLen,
                    const uint8_t *data, size_t len, uint8_t out[32]) {
  const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;
  return mbedtls_md_hmac(info, key, keyLen, data, len, out) == 0;
}

// Loads the pre-shared key from secrets.h. Called once from meshInit().
void meshLoadKey() {
  meshSetKey(MESH_KEY, sizeof(MESH_KEY));
}
