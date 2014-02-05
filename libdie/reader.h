#ifndef READER_H
#define READER_H

struct reader {
  char *data, *pos;
  bool has_nl;
  const char *gw();
  const char *gwnl();
  void nl();
  int gi();
  double gd();

  bool eof();

  reader(const char *fname);
  ~reader();
};

#endif
