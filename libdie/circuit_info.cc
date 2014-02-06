#undef _FORTIFY_SOURCE

#include "circuit_info.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

circuit_info::circuit_info(const char *fname)
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

  int ne = gi();
  nl();
  circs.resize(ne);
  for(int i=0; i != ne; i++) {
    int id = gi();
    assert(id == i);
    cinfo &ci = circs[i];
    ci.type = gw()[0];
    ci.net = gi();
    ci.netp = gi();
    ci.trans = -1;
    ci.x0 = gi();
    ci.y0 = gi();
    ci.x1 = gi();
    ci.y1 = gi();
    ci.surface = gi();
    while(!has_nl)
      ci.neighbors.push_back(strtol(gw()+1, 0, 10));
    nl();
  }

  ne = gi();
  nl();
  nets.resize(ne);
  for(int i=0; i != ne; i++) {
    int id = gi();
    assert(id == i);
    ninfo &ni = nets[i];
    while(!has_nl)
      ni.circ.push_back(gi());
    nl();
  }

  ne = gi();
  nl();
  trans.resize(ne);
  for(int i=0; i != ne; i++) {
    int id = gi();
    assert(id == i);
    tinfo &ti = trans[i];
    ti.circ = gi();
    circs[ti.circ].trans = i;
    ti.x = gi();
    ti.y = gi();
    ti.t1 = gi();
    ti.gate = gi();
    ti.t2 = gi();
    ti.f = gd();
    nl();
    gate_to_trans[ti.gate].push_back(i);
    term_to_trans[ti.t1].push_back(i);
    term_to_trans[ti.t2].push_back(i);
  }
  free(data);
  pos = NULL;
}

const char *circuit_info::gw()
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

void circuit_info::nl()
{
  while(!has_nl)
    gw();
  has_nl = false;
}

int circuit_info::gi()
{
  return strtol(gw(), 0, 10);
}

double circuit_info::gd()
{
  return strtod(gw(), 0);
}

