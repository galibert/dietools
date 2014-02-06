#ifndef CIRCUIT_MAP_H
#define CIRCUIT_MAP_H

class circuit_map {
public:
  int *data;
  int sx, sy;

  int p(int l, int x, int y) const {
    if(x < 0 || x >= sx || y < 0 || y >= sy)
      return -1;
    return data[l+3*(x+y*sx)];
  }

  void s(int l, int x, int y, int v) {
    data[l+3*(x+y*sx)] = v;
  }

  circuit_map(const char *fname, int sx, int sy, bool create);
  ~circuit_map();
};

#endif
