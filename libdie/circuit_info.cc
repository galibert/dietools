#undef _FORTIFY_SOURCE

#include "circuit_info.h"
#include "reader.h"

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
  reader rd(fname);

  sx = rd.gi();
  sy = rd.gi();
  nl = rd.gi();
  rd.nl();

  int ne = rd.gi();
  rd.nl();
  circs.resize(ne);
  for(int i=0; i != ne; i++) {
    int id = rd.gi();
    assert(id == i);
    cinfo &ci = circs[i];
    ci.type = rd.gw()[0];
    ci.net = rd.gi();
    ci.netp = rd.gi();
    ci.trans = -1;
    ci.x0 = rd.gi();
    ci.y0 = rd.gi();
    ci.x1 = rd.gi();
    ci.y1 = rd.gi();
    ci.surface = rd.gi();
    while(!rd.eol())
      ci.neighbors.push_back(strtol(rd.gw()+1, 0, 10));
    rd.nl();
  }

  ne = rd.gi();
  rd.nl();
  nets.resize(ne);
  for(int i=0; i != ne; i++) {
    int id = rd.gi();
    assert(id == i);
    ninfo &ni = nets[i];
    while(!rd.eol())
      ni.circ.push_back(rd.gi());
    rd.nl();
  }

  ne = rd.gi();
  rd.nl();
  trans.resize(ne);
  for(int i=0; i != ne; i++) {
    int id = rd.gi();
    assert(id == i);
    tinfo &ti = trans[i];
    ti.circ = rd.gi();
    circs[ti.circ].trans = i;
    ti.x = rd.gi();
    ti.y = rd.gi();
    ti.t1 = rd.gi();
    ti.gate = rd.gi();
    ti.t2 = rd.gi();
    ti.f = rd.gd();
    rd.nl();
    gate_to_trans[ti.gate].push_back(i);
    term_to_trans[ti.t1].push_back(i);
    term_to_trans[ti.t2].push_back(i);
  }
}
