#ifndef PAD_INFO_H
#define PAD_INFO_H

#include <net_info.h>

#include <map>
#include <string>

struct pinfo {
  std::string name;
  int x, y, net;
  char orientation;
};

class pad_info {
public:
  std::vector<pinfo> pads;

  pad_info(const char *fname, const net_info &info);
};

#endif
