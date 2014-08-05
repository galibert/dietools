#ifndef CIRCUIT_INFO_H
#define CIRCUIT_INFO_H

#include <list>
#include <map>
#include <vector>

using namespace std;

struct cinfo {
  char type;
  int net, netp, trans;
  int x0, y0, x1, y1, surface;
  list<int> neighbors;
};

struct ninfo {
  list<int> circ;
};

struct tinfo {
  int circ, t1, t2, gate, x, y;
  double f;
};

class circuit_info {
public:
  vector<cinfo> circs;
  vector<ninfo> nets;
  vector<tinfo> trans;
  map<int, list<int> > gate_to_trans;
  map<int, list<int> > term_to_trans;

  int sx, sy, nl;

  circuit_info(const char *fname);
};

#endif
