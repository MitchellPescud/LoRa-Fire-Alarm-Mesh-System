#include <Arduino.h>
#include "node_commands.h"
#include "mesh_proto.h"
#include "config.h"
#include "state.h"
#include "alarm.h"
#include "dust_sensor.h"
#include "mesh.h"

// Fire node implementation of the remote commands.
//   TEST_FIRE    - same as the "fire" serial command / dashboard button
//   RECALIBRATE  - same as "baseline"; refused while an alarm is active
//   REBOOT       - restarts after a short delay so the ACK can get out first

static const unsigned long REBOOT_DELAY_MS = 3000;

static bool          recalPending = false;
static bool          rebootPending = false;
static unsigned long rebootAtMs = 0;

uint8_t nodeCommandExec(uint8_t cmd) {
  switch (cmd) {
    case MESH_CMD_TEST_FIRE:
      if (fireActive || meshLocalEpisodeActive()) return MESH_ACK_BUSY;
      Serial.println("[CMD] remote TEST FIRE");
      smokePresent = true;
      highReadingCount = 0;
      triggerAlarm();
      return MESH_ACK_OK;

    case MESH_CMD_RECALIBRATE:
      if (fireActive) return MESH_ACK_BUSY;
      Serial.println("[CMD] remote RECALIBRATE scheduled");
      recalPending = true;
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
  if (recalPending) {
    recalPending = false;
    if (!fireActive) calibrateBaseline();   // blocking (~3 s) but keeps servicing the mesh
  }
  if (rebootPending && (int32_t)(millis() - rebootAtMs) >= 0) {
    Serial.println("[CMD] rebooting now");
    Serial.flush();
    ESP.restart();
  }
}
