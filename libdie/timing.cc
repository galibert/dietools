#include "timing.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void time_info::start(const char *msg)
{
  fprintf(stderr, "%s\n", msg);
  gettimeofday(&start_time, NULL);
  lsec = 0;
}

void time_info::tick(int pos, int max)
{
  double ratio = double(pos+1)/double(max);
  timeval ctime;
  gettimeofday(&ctime, NULL);
  int ms = (ctime.tv_sec - start_time.tv_sec) * 1000 + (ctime.tv_sec - start_time.tv_sec) / 1000;
  int rt = pos+1 == max ? ms : ms/ratio*(1-ratio);
  int rts = rt/1000;
  if(rts != lsec || pos+1 == max) {
    lsec = rts;
    fprintf(stderr, " %3d%% %6d:%02d%c", int(100*ratio+0.5), rts / 60, rts % 60, pos+1 == max ? '\n' : '\r');
  }
}
