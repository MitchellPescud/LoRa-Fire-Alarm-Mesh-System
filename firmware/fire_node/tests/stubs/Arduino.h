// Minimal host stand-in for Arduino.h - just enough to compile the mesh sources on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#include <algorithm>

extern uint32_t sim_now;
extern int sim_cur_node;
extern bool sim_verbose;
inline uint32_t millis() { return sim_now; }
inline void delay(uint32_t) {}
inline long random(long a, long b) { return a + (b > a ? rand() % (b - a) : 0); }
inline long random(long b) { return random(0, b); }
inline uint32_t esp_random() { return (uint32_t)rand(); }
#define constrain(amt, low, high) ((amt) < (low) ? (low) : ((amt) > (high) ? (high) : (amt)))
#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0
#define HEX 16
void pinMode(int, int);
void digitalWrite(int, int);

class String {
 public:
  std::string s;
  String() {}
  String(const char *c) : s(c) {}
  String(int v) : s(std::to_string(v)) {}
  String(unsigned v) : s(std::to_string(v)) {}
  String(long v) : s(std::to_string(v)) {}
  String(unsigned long v) : s(std::to_string(v)) {}
  String(float v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); s = b; }
  String(double v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); s = b; }
  String &operator+=(const String &o) { s += o.s; return *this; }
  String &operator+=(const char *c) { s += c; return *this; }
  const char *c_str() const { return s.c_str(); }
};
inline String operator+(const String &a, const String &b) { String r; r.s = a.s + b.s; return r; }
inline String operator+(const char *a, const String &b) { String r; r.s = std::string(a) + b.s; return r; }
inline String operator+(const String &a, const char *b) { String r; r.s = a.s + b; return r; }

struct SerialStub {
  std::string line;
  void flush(bool nl);
  void print(const char *c) { line += c; }
  void print(const String &c) { line += c.s; }
  void print(int v) { line += std::to_string(v); }
  void print(unsigned v) { line += std::to_string(v); }
  void print(long v) { line += std::to_string(v); }
  void print(unsigned long v) { line += std::to_string(v); }
  void print(uint8_t v) { line += std::to_string((int)v); }
  void print(double v, int d = 2) { char b[32]; snprintf(b, sizeof b, "%.*f", d, v); line += b; }
  void print(unsigned v, int base) { char b[32]; snprintf(b, sizeof b, "%X", v); line += b; }
  void print(uint8_t v, int base) { print((unsigned)v, base); }
  void print(int v, int base) { print((unsigned)v, base); }
  template <class T> void println(T v) { print(v); flush(true); }
  void println() { flush(true); }
  template <class T> void println(T v, int base) { print(v, base); flush(true); }
};
extern SerialStub Serial;
