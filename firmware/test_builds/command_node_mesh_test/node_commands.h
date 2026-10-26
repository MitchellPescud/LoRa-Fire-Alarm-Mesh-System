#ifndef NODE_COMMANDS_H
#define NODE_COMMANDS_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// What this node does when another node sends it a remote command (see CMD in
// mesh_proto.h). mesh.cpp authenticates and de-duplicates the request, then
// calls nodeCommandExec(); the returned MESH_ACK_* code goes back in the ACK.
// The implementation (node_commands.cpp) is different for fire nodes and for
// the command node, because only fire nodes have a dust sensor.
//
// MESH_CMD_STATUS is handled inside mesh.cpp and never reaches this hook.
// ---------------------------------------------------------------------------

// Called from the radio receive path. Must return quickly: anything slow
// (recalibrating, rebooting) is only scheduled here and carried out by
// nodeCommandService() from loop().
uint8_t nodeCommandExec(uint8_t cmd);

// Call every loop() pass. Runs scheduled work (baseline recalibration, reboot).
void nodeCommandService();

#endif
