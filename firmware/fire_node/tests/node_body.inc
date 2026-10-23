// Included once per simulated node, inside that node's namespace. Pulls in the
// REAL firmware sources (mesh.cpp, alarm.cpp, mesh_proto.cpp, state.cpp) with
// the radio and HMAC backends replaced by simulator hooks.
#undef CONFIG_H
#undef STATE_H
#undef MESH_H
#undef ALARM_H
#undef MESH_RADIO_H
#undef MESH_PROTO_H
#undef NODE_COMMANDS_H

#include "../config.h"
#include "../state.h"
#include "../state.cpp"
#include "../mesh_proto.cpp"

bool meshHmacSha256(const uint8_t *key, size_t keyLen, const uint8_t *data, size_t len, uint8_t out[32]) {
  return sim_hmac(key, keyLen, data, len, out);
}
void meshLoadKey() { meshSetKey(sim_node_key[NODE_IDX], 16); }

#include "../mesh_radio.h"
RadioStats radioStats = {0, 0, 0, 0, 0};
bool radioInit() { return true; }
void radioSetRxHandler(RadioRxHandler h) { sim_rx_handler[NODE_IDX] = h; }
bool radioQueueTx(const uint8_t *buf, uint8_t len, uint32_t delayMs, RadioPrio prio) {
  return sim_queue_tx(NODE_IDX, buf, len, delayMs, prio == RADIO_PRIO_HIGH);
}
void radioService() { sim_radio_service(NODE_IDX); }

#include "../node_commands.h"
// Test double for the per-sketch command hook: counts executions, result is settable.
int     simCmdExecCount = 0;
uint8_t simCmdLast = 0;
uint8_t simCmdResult = 0;   // MESH_ACK_OK
uint8_t nodeCommandExec(uint8_t cmd) { simCmdExecCount++; simCmdLast = cmd; return simCmdResult; }
void nodeCommandService() {}

#include "../alarm.cpp"
#include "../mesh.cpp"

bool simBuzzerSilenced() { return buzzerSilenced; }
