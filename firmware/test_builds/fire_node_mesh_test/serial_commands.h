#ifndef SERIAL_COMMANDS_H
#define SERIAL_COMMANDS_H

// Reads and dispatches one line from Serial per call (no-op if nothing is
// available). Commands: fire | clear | baseline
void handleSerialCommand();

#endif
