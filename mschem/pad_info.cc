#undef _FORTIFY_SOURCE

#include "pad_info.h"

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
  char msg[4096];
  sprintf(msg, "Open %s", fname);
  int fd = open(fname, O_RDONLY);
  if(fd<0) {
    perror(msg);
    exit(2);
  }

  int size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  char *data = (char *)malloc(size+1);
  read(fd, data, size);
  data[size] = 0;
  close(fd);

  pos = data;
  has_nl = false;

  while(*pos) {
    if(*pos == '\n' || *pos == '#') {
      nl();
      continue;
    }

    pads.resize(pads.size()+1);
    pinfo &pi = pads.back();

    pi.x = gi();
    pi.y = gi();
    pi.orientation = gw()[0];
    pi.name = gw();
    nl();

    pi.net = ninfo.find("p_" + pi.name);
  }
  free(data);
  pos = NULL;
}

const char *pad_info::gw()
{
  assert(!has_nl);
  while(*pos == ' ')
    pos++;
  const char *r = pos;
  while(*pos != ' ' && *pos != '\n')
    pos++;
  assert(pos != r);
  has_nl = *pos == '\n';
  *pos++ = 0;
  return r;
}

void pad_info::nl()
{
  if(!has_nl) {
    while(*pos && *pos != '\n')
      pos++;
    if(*pos)
      pos++;
  }
  has_nl = false;
}

int pad_info::gi()
{
  return strtol(gw(), 0, 10);
}

double pad_info::gd()
{
  return strtod(gw(), 0);
}
