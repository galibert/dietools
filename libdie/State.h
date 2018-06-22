#ifndef STATE_H
#define STATE_H

#include "circuit_map.h"
#include "circuit_info.h"
#include "net_info.h"

#include <set>

class State {
public:
  enum { S_0, S_1, S_FLOAT };
  enum { T_NMOS, T_PMOS, T_NDEPL };
  circuit_info info;
  circuit_map cmap;
  net_info ninfo;

  int vcc, gnd;
  bool cmos;
  std::vector<bool> quasi_vcc;
  std::vector<bool> oscillator;
  std::vector<int> ttype;
  std::vector<bool> ignored;
  std::vector<bool> pullup, pulldown;
  std::vector<bool> display;
  std::vector<int> forced_power;
  std::vector<int> power;
  std::vector<int> power_dist;

  State(const char *info_path, const char *cmap_path, const char *pins_path, bool cmos);

  void reset_to_floating();
  void reset_to_zero();
  void apply_changed(std::set<int> changed);
};

#endif
