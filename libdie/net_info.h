#ifndef NET_INFO_H
#define NET_INFO_H

#include "circuit_map.h"
#include "circuit_info.h"

#include <map>
#include <string>

class net_info {
public:
  vector<string> names;
  map<string, int> nets;

  net_info(const char *fname, const circuit_map &cmap, const circuit_info &info);
  string net_name(int net) const;
  int find(string name) const;

private:
  char *pos;
  bool has_nl;
  const char *gw();
  void nl();
  int gi();
  double gd();
};

#endif
