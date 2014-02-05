#ifndef TIMING_H
#define TIMING_H

#include <sys/time.h>

struct time_info {
  timeval start_time;
  int lsec;

  void start(const char *msg);
  void tick(int pos, int max);
};

#endif
