#ifndef NET_INFO_H
#define NET_INFO_H

#include "circuit_map.h"
#include "circuit_info.h"

#include <map>
#include <string>

class net_info {
public:
  std::vector<std::string> names;
  std::map<std::string, int> nets;

  net_info(const char *fname, const circuit_map &cmap, const circuit_info &info);
  std::string net_name(int net) const;
  int find(std::string name) const;

private:
  char *pos;
  bool has_nl;
  const char *gw();
  void nl();
  int gi();
  double gd();
};

#endif
