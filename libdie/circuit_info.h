#ifndef CIRCUIT_INFO_H
#define CIRCUIT_INFO_H

#include <map>
#include <vector>

struct cinfo {
  char type;
  int net, netp, trans;
  int x0, y0, x1, y1, surface;
  std::vector<int> neighbors;
};

struct ninfo {
  std::vector<int> circ;
};

struct tinfo {
  int circ, t1, t2, gate, x, y;
  double f;
};

class circuit_info {
public:
  std::vector<cinfo> circs;
  std::vector<ninfo> nets;
  std::vector<tinfo> trans;
  std::map<int, std::vector<int>> gate_to_trans;
  std::map<int, std::vector<int>> term_to_trans;

  int sx, sy, nl;

  circuit_info(const char *fname);
};

#endif
