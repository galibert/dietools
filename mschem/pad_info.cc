#undef _FORTIFY_SOURCE

#include "pad_info.h"

#include <reader.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

pad_info::pad_info(const char *fname, const net_info &ninfo)
{
  reader rd(fname);

  while(!rd.eof()) {
    if(rd.peek() == '\n' || rd.peek() == '#') {
      rd.nl();
      continue;
    }

    pads.resize(pads.size()+1);
    pinfo &pi = pads.back();

    pi.x = rd.gi();
    pi.y = rd.gi();
    pi.orientation = rd.gw()[0];
    pi.name = rd.gw();
    rd.nl();

    pi.net = ninfo.find("p_" + pi.name);
  }
}
