#ifndef STATE_H
#define STATE_H

#include "circuit_map.h"
#include "circuit_info.h"
#include "net_info.h"

#include <set>

class State {
public:
  enum { S_0, S_1, S_FLOAT };
  circuit_map cmap;
  circuit_info info;
  net_info ninfo;

  int vcc, gnd;
  vector<bool> quasi_vcc;
  vector<bool> oscillator;
  vector<bool> depletion;
  vector<bool> ignored;
  vector<bool> pullup, pulldown;
  vector<bool> display;
  vector<int> forced_power;
  vector<int> power;
  vector<int> power_dist;

  State();

  void reset_to_floating();
  void reset_to_zero();
  void apply_changed(set<int> changed);
};

#endif
