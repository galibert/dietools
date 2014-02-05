#undef _FORTIFY_SOURCE

#include "reader.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <zlib.h>
#include <sys/time.h>

reader::reader(const char *fname)
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

  data = (char *)malloc(size+1);
  read(fd, data, size);
  data[size] = 0;
  close(fd);

  pos = data;
  has_nl = false;
}

reader::~reader()
{
  free((void *)data);
}

const char *reader::gw()
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

const char *reader::gwnl()
{
  assert(!has_nl);
  while(*pos == ' ')
    pos++;
  const char *r = pos;
  while(*pos && *pos != '\n')
    pos++;
  if(*pos) {
    has_nl = true;
    *pos++ = 0;
  } else
    has_nl = false;
  return r;
}

void reader::nl()
{
  if(!has_nl) {
    while(*pos && *pos != '\n')
      pos++;
    if(*pos)
      pos++;
  }
  has_nl = false;
}

int reader::gi()
{
  return strtol(gw(), 0, 10);
}

double reader::gd()
{
  return strtod(gw(), 0);
}

bool reader::eof()
{
  return !*pos;
}
