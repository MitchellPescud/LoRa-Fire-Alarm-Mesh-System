#pragma once
#include <map>
#include <string>
#include "Arduino.h"
extern std::map<int, std::map<std::string, uint16_t>> sim_nvs;   // per simulated node
class Preferences {
 public:
  bool begin(const char *, bool) { return true; }
  void end() {}
  uint16_t getUShort(const char *k, uint16_t d) { auto &m = sim_nvs[sim_cur_node]; return m.count(k) ? m[k] : d; }
  void putUShort(const char *k, uint16_t v) { sim_nvs[sim_cur_node][k] = v; }
};
