#include <Arduino.h>
#include "node_commands.h"
#include "mesh_proto.h"
#include "config.h"
#include "state.h"
#include "alarm.h"
#include "mesh.h"

// Command node implementation of the remote commands. It has no dust sensor,
// so RECALIBRATE is not supported; TEST_FIRE raises an alarm episode from this
// node exactly as a fire node would.

static const unsigned long REBOOT_DELAY_MS = 3000;

static bool          rebootPending = false;
static unsigned long rebootAtMs = 0;

uint8_t nodeCommandExec(uint8_t cmd) {
  switch (cmd) {
    case MESH_CMD_TEST_FIRE:
      if (fireActive || meshLocalEpisodeActive()) return MESH_ACK_BUSY;
      Serial.println("[CMD] remote TEST FIRE");
      triggerAlarm();
      return MESH_ACK_OK;

    case MESH_CMD_REBOOT:
      if (!rebootPending) {
        Serial.println("[CMD] remote REBOOT scheduled");
        rebootPending = true;
        rebootAtMs = millis() + REBOOT_DELAY_MS;
      }
      return MESH_ACK_OK;

    default:
      return MESH_ACK_UNSUPPORTED;
  }
}

void nodeCommandService() {
  if (rebootPending && (int32_t)(millis() - rebootAtMs) >= 0) {
    Serial.println("[CMD] rebooting now");
    Serial.flush();
    ESP.restart();
  }
}
