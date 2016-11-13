#define _FILE_OFFSET_BITS 64
#undef _FORTIFY_SOURCE

#include <reader.h>
#include <images.h>
#include <timing.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/mman.h>
#endif
#include <math.h>

#include <set>
#include <map>
#include <list>
#include <vector>

#include <boost/bind.hpp>
#include <boost/function.hpp>

using namespace std;

struct circuit_map {
  int *data;
  int nl, sx, sy;

  int p(int l, int x, int y) const {
    return data[nl*(x+y*sx)+l];
  }

  void s(int l, int x, int y, int v) {
    data[nl*(x+y*sx)+l] = v;
  }

  circuit_map(const char *fname, int nl, int sx, int sy, bool create);
  ~circuit_map();
};

circuit_map::circuit_map(const char *fname, int _nl, int _sx, int _sy, bool create)
{
  nl = _nl;
  sx = _sx;
  sy = _sy;
  unsigned char *map_adr;
  if(create) {
    create_file_rw(fname, map_adr, nl*4*(int_least64_t)sx*sy);
    memset(map_adr, 0xff, (int_least64_t)sx*sy*4*nl);

  } else {
    int_least64_t size;
    map_file_ro(fname, map_adr, size, false);
  }

  data = (int *)map_adr;
}

circuit_map::~circuit_map()
{
  #ifdef _WIN32
    UnmapViewOfFile(data);
  #else
    munmap(data, long(nl)*sx*sy*4);
  #endif
}

enum {
  ACTIVE,
  POLY,
  METAL,
  BURIED,
  TRANSISTOR,
  CAPACITOR,
  DISABLED
};

const char *type_names = "apmbtc*";

struct circuit_info {
  int type;
  int surface;
  int x0, y0, x1, y1;
  int net, netp, metal;
  set<int> neighbors;
};

struct via_info {
  int metal;
  int active_poly;

  via_info(int _metal, int _active_poly) {
    metal = _metal;
    active_poly = _active_poly;
  }
};

struct net_info {
  set<int> circuits;
};

struct trans_info {
  int circ, t1, t2, gate, x, y;
  double strength;
};

struct via_map {
  map<int, list<int> > metal_to_ap, ap_to_metal;
};

struct fill_coord {
  int x, y;
  fill_coord(int _x, int _y) { x = _x; y = _y; }
};

struct metal_link_info {
  int x1, y1, x2, y2;
  int circuit_id;
};

void fill_1(list<fill_coord> &fc, int x, int y, int sx, int sy, int color, boost::function<void(int, int)> set_pixel, boost::function<int(int, int)> read_color, boost::function<bool(int, int)> test_pixel)
{
  if(test_pixel(x, y))
    return;
  set_pixel(x, y);
  int x0 = x-1;
  while(x0 >= 0 && !test_pixel(x0, y) && read_color(x0, y) == color) {
    set_pixel(x0, y);
    x0--;
  }
  x0++;
  int x1 = x+1;
  while(x1 < sx && !test_pixel(x1, y) && read_color(x1, y) == color) {
    set_pixel(x1, y);
    x1++;
  }
  x1--;
  for(int xx=x0; xx<=x1; xx++) {
    if(y > 0   && !test_pixel(xx, y-1) && read_color(xx, y-1) == color)
      fc.push_back(fill_coord(xx, y-1));
    if(y < sy-1 && !test_pixel(xx, y+1) && read_color(xx, y+1) == color)
      fc.push_back(fill_coord(xx, y+1));
  }
}

void fill(int x, int y, int sx, int sy, int color, boost::function<void(int, int)> set_pixel, boost::function<int(int, int)> read_color, boost::function<bool(int, int)> test_pixel)
{
  list<fill_coord> fc;
  fc.push_back(fill_coord(x, y));
  while(!fc.empty()) {
    int xx = fc.front().x;
    int yy = fc.front().y;
    fc.pop_front();
    fill_1(fc, xx, yy, sx, sy, color, set_pixel, read_color, test_pixel);
  }
}

void color_active_poly_set(int x, int y, int *pmap, bool *is_buried, bool *is_caps)
{
  int v = pmap[x+y*11];
  if(v & 2)
    *is_buried = true;
  if(v & 4)
    *is_caps = true;
  pmap[x+y*11] = v | 8;
}

int color_active_poly_read(int x, int y, int *pmap)
{
  return pmap[x+y*11] & 1;
}

bool color_active_poly_test(int x, int y, int *pmap)
{
  return pmap[x+y*11] & 8;
}

int color_active_poly(int x, int y, const pbm *active, const pbm *poly, const pbm *buried, const pbm *caps)
{
  bool ca = active->p(x, y);
  bool cp = poly->p(x, y);
  if(ca && cp) {
    for(int yy = y-5; yy <= y+5; yy++)
      for(int xx = x-5; xx <= x+5; xx++)
	if(buried->p(xx, yy) || (caps && caps->p(xx, yy)))
	  goto check_buried_caps;
    return TRANSISTOR;
  check_buried_caps:
    {
      int pmap[11*11];
      for(int yy = 0; yy <= 10; yy++)
	for(int xx = 0; xx <= 10; xx++) {
	  int x1 = x-5+xx;
	  int y1 = y-5+yy;
	  pmap[yy*11+xx] = active->p(x1, y1) && poly->p(x1, y1) ? buried->p(x1, y1) ? 3 : caps && caps->p(x1, y1) ? 5 : 1 : 0;
	}
      bool is_buried = false;
      bool is_caps = false;
      fill(5, 5, 11, 11, 1, boost::bind(color_active_poly_set, _1, _2, pmap, &is_buried, & is_caps), boost::bind(color_active_poly_read, _1, _2, pmap), boost::bind(color_active_poly_test, _1, _2, pmap));
      return is_caps ? CAPACITOR : is_buried ? BURIED : TRANSISTOR;
    }
  }

  return ca ? ACTIVE : cp ? POLY : -1;
}

int color_active_gates(int x, int y, const pbm *active, const pbm *gates, const pbm *caps)
{
  bool ca = active->p(x, y);
  bool cg = gates->p(x, y);
  bool cc = caps && caps->p(x, y);
  assert(!(cg && cc));
  if(cc)
    return CAPACITOR;
  if(ca)
    return ACTIVE;
  if(cg)
    return TRANSISTOR;
  return -1;
}

int color_metal(int x, int y, const pbm *metal)
{
  return metal->p(x, y) ? METAL : -1;
}

void circuit_set(int x, int y, int cid, circuit_info *ci, circuit_map *dest, int l)
{
  if(ci->x0 > x)
    ci->x0 = x;
  if(ci->y0 > y)
    ci->y0 = y;
  if(ci->x1 < x)
    ci->x1 = x;
  if(ci->y1 < y)
    ci->y1 = y;
  ci->surface++;
  dest->s(l, x, y, cid);
}

bool circuit_test(int x, int y, int cid, const circuit_map *dest, int l)
{
  return dest->p(l, x, y) == cid;
}

void build_circuits(time_info &tinfo, boost::function<int(int, int)> color, vector<circuit_info> &circuits, circuit_map *dest, int l)
{
  for(int y=0; y<dest->sy; y++) {
    tinfo.tick(y, dest->sy);
    for(int x=0; x<dest->sx; x++) {
      if(dest->p(l, x, y) != -1)
	continue;
      int c = color(x, y);
      if(c == -1)
	continue;
      int cid = circuits.size();
      circuits.resize(cid+1);
      circuit_info *ci = &circuits[cid];
      ci->type = c;
      ci->surface = 0;
      ci->x0 = ci->x1 = x;
      ci->y0 = ci->y1 = y;
      ci->net = -1;
      ci->netp = -1;
      ci->metal = -1;
      fill(x, y, dest->sx, dest->sy, c, boost::bind(circuit_set, _1, _2, cid, ci, dest, l), color, boost::bind(circuit_test, _1, _2, cid, dest, l));
    }
  }
}

static void build_neighbors_add(vector<circuit_info> &circuits, int cm, int x, int y, int cn)
{
  if(cn == -1 || cn == cm)
    return;
  int cmt = circuits[cm].type;
  int cnt = circuits[cn].type;
  if((cmt == ACTIVE && cnt != POLY) || (cmt == POLY && cnt != ACTIVE) || (cmt != POLY && cmt != ACTIVE))
    circuits[cm].neighbors.insert(cn);
}

void build_neighbors(time_info &tinfo, vector<circuit_info> &circuits, const circuit_map &cmap)
{
  for(int y=1; y<cmap.sy-1; y++) {
    tinfo.tick(y-1, cmap.sy-2);
    for(int x=1; x<cmap.sx-1; x++) {
      int cm = cmap.p(0, x, y);
      if(cm == -1)
	continue;
      build_neighbors_add(circuits, cm, x-1, y, cmap.p(0, x-1, y));
      build_neighbors_add(circuits, cm, x+1, y, cmap.p(0, x+1, y));
      build_neighbors_add(circuits, cm, x, y-1, cmap.p(0, x, y-1));
      build_neighbors_add(circuits, cm, x, y+1, cmap.p(0, x, y+1));
    }
  }
}

void build_transistor_groups(list<set<int> > &groups, int &terminaux, int &gates, vector<bool> &is_terminal, vector<bool> &is_gate, const vector<circuit_info> &circuits, int id)
{
  const circuit_info &ci = circuits[id];
  terminaux = 0;
  gates = 0;
  for(int step=0; step<2; step++) {
    for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++) {
      const circuit_info &ci1 = circuits[*j];
      if(step ^ (ci1.type != BURIED && ci1.type != CAPACITOR))
	continue;
      list<set<int> >::iterator l;
      for(l = groups.begin(); l != groups.end(); l++) {
	for(set<int>::const_iterator k = l->begin(); k != l->end(); k++)
	  if(ci1.neighbors.find(*k) != ci1.neighbors.end()) {
	    const circuit_info &ci2 = circuits[*k];
	    if(!((ci1.type == ACTIVE && ci2.type == POLY) || (ci1.type == POLY && ci2.type == ACTIVE)))
	      goto found;
	  }
      }

    found:
      if(l == groups.end()) {
	groups.push_back(set<int>());
	groups.back().insert(*j);
	if(ci1.type == ACTIVE || ci1.type == BURIED || ci1.type == CAPACITOR) {
	  terminaux++;
	  is_terminal.push_back(true);
	} else
	  is_terminal.push_back(false);

	if(ci1.type == POLY || ci1.type == BURIED || ci1.type == CAPACITOR) {
	  gates++;
	  is_gate.push_back(true);
	} else
	  is_gate.push_back(false);

      } else
	l->insert(*j);
    }
  }
}

void clean_and_remap(time_info &tinfo, vector<circuit_info> &circuits, circuit_map &cmap)
{
  bool has_error = false;

  int nc = circuits.size();
  int *remap_active = new int[nc];
  int *remap_poly   = new int[nc];

  for(unsigned int i=0; i != circuits.size(); i++) {
    tinfo.tick(i, circuits.size());
    circuit_info &ci = circuits[i];
    switch(ci.type) {
    case ACTIVE:
      remap_active[i] = i;
      remap_poly[i] = -1;
      break;
    case POLY:
      remap_active[i] = -1;
      remap_poly[i] = i;
      break;
    case BURIED:
      remap_active[i] = i;
      remap_poly[i] = i;
      break;
    case CAPACITOR:
      remap_active[i] = i;
      remap_poly[i] = i;
      break;
    case METAL:
      remap_active[i] = -1;
      remap_poly[i] = -1;
      break;
    case TRANSISTOR: {
      list<set<int> > groups;
      vector<bool> is_gate, is_terminal;
      int terminaux, gates;
      build_transistor_groups(groups, terminaux, gates, is_terminal, is_gate, circuits, i);
      if(terminaux < 2) { //  || (ci.x0 - ci.x1 <= 2 && ci.x0 - ci.x1 >= -2) || (ci.y0 - ci.y1 <= 2 && ci.y0 - ci.y1 >= -2)) {
	if(terminaux == 0 || gates == 0) {
	  fprintf(stderr, "P/A superposition zone (%d, %d)-(%d, %d) has no active %s\n", ci.x0, cmap.sy-1-ci.y1, ci.x1, cmap.sy-1-ci.y0, terminaux ? "gate" : "terminal");
	  for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++) {
	    const circuit_info &ci1 = circuits[*j];
	    fprintf(stderr, "  - %c%d (%d, %d)-(%d, %d)\n", type_names[ci1.type], *j, ci1.x0, cmap.sy-1-ci1.y1, ci1.x1, cmap.sy-1-ci1.y0);
	  }
	  has_error = true;
	} else {
	  int pmap = -1, amap = -1;
	  for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++) {
	    int t = circuits[*j].type;
	    switch(t) {
	    case ACTIVE: amap = *j; break;
	    case POLY:   pmap = *j; break;
	    case BURIED: if(amap == -1) amap = *j; if(pmap == -1) pmap = *j; break;
	    }
	  }
	  if(amap == -1 || pmap == -1) {
	    fprintf(stderr, "Bad a/pmap on clean-and-remap (%d, %d)-(%d, %d)\n", ci.x0, cmap.sy-1-ci.y1, ci.x1, cmap.sy-1-ci.y0);
	    has_error = true;
	    break;
	  }
	  for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++) {
	    circuits[amap].neighbors.insert(*j);
	    circuits[pmap].neighbors.insert(*j);
	  }

	  ci.type = DISABLED;
	  ci.neighbors.clear();
	  remap_active[i] = amap;
	  remap_poly[i] = pmap;
	}
      } else {
	remap_active[i] = i;
	remap_poly[i] = i;
      }
      break;
    }
    }
  }

  if(has_error)
    exit(1);

  for(int y=0; y<cmap.sy; y++) {
    tinfo.tick(y, cmap.sy);
    for(int x=0; x<cmap.sx; x++) {
      int v = cmap.p(0, x, y);
      if(v != -1) {
	cmap.s(0, x, y, remap_active[v]);
	cmap.s(1, x, y, remap_poly[v]);
      }
    }
  }

  for(unsigned int i=0; i != circuits.size(); i++) {
    circuit_info &ci = circuits[i];
  retry:
    for(set<int>::iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++)
      if(remap_active[*j] != -1 && remap_active[*j] != *j) {
	int rm = ci.type == ACTIVE ? remap_active[*j] : remap_poly[*j];
	ci.neighbors.erase(j);
	ci.neighbors.insert(rm);
	goto retry;
      }
  }
}

void compress_ids(time_info &tinfo, vector<circuit_info> &circuit_infos, list<metal_link_info> &virtual_poly_id, circuit_map &cmap)
{
  vector<int> remap;
  remap.resize(circuit_infos.size());
  unsigned int cid = 0;
  for(unsigned int i=0; i != circuit_infos.size(); i++) {
    remap[i] = cid;
    if(circuit_infos[i].type != DISABLED) {
      if(cid != i)
	circuit_infos[cid] = circuit_infos[i];
      cid++;
    }
  }
  circuit_infos.resize(cid);

  for(int y=0; y<cmap.sy; y++) {
    tinfo.tick(y, cmap.sy);
    for(int x=0; x<cmap.sx; x++)
      for(int l=0; l<3; l++) {
	int v = cmap.p(l, x, y);
	if(v != -1)
	  cmap.s(l, x, y, remap[v]);
      }
  }

  for(list<metal_link_info>::iterator i = virtual_poly_id.begin(); i != virtual_poly_id.end(); i++)
    i->circuit_id = remap[i->circuit_id];

  for(unsigned int i=0; i != circuit_infos.size(); i++) {
    circuit_info &ci = circuit_infos[i];
    set<int> re;
    for(set<int>::const_iterator i = ci.neighbors.begin(); i != ci.neighbors.end(); i++)
      re.insert(remap[*i]);
    ci.neighbors = re;
  }
}

void map_vias_set(int x, int y, via_info *via, const vector<circuit_info> *circuit_infos, pbm *used, circuit_map *cmap)
{
  used->s(x, y, true);
  int na = cmap->p(0, x, y);
  int np = cmap->nl >= 3 ? cmap->p(1, x, y) : -1;
  int nm = cmap->p(cmap->nl >= 3 ? 2 : 1, x, y);
  if(nm != -1) {
    if(via->metal == -1)
      via->metal = nm;
    else if(via->metal != nm)
      fprintf(stderr, "via at (%d, %d) touches multiple metal tracks\n", x, cmap->sy-1-y);
  }
  if(na != -1 && np != -1 && na != np)
    fprintf(stderr, "via at (%d, %d) touches split active/poly\n", x, cmap->sy-1-y);
  else if(na != -1 || np != -1) {
    int nn = na == -1 ? np : na;
    if((*circuit_infos)[nn].type == TRANSISTOR)
      fprintf(stderr, "via at (%d, %d) touches a transistor\n", x, cmap->sy-1-y);
    else if((*circuit_infos)[nn].type == CAPACITOR)
      fprintf(stderr, "via at (%d, %d) touches a capacitor\n", x, cmap->sy-1-y);
    else if(via->active_poly == -1)
      via->active_poly = nn;
    else if(via->active_poly != nn)
      fprintf(stderr, "via at (%d, %d) touches multiple poly/layer zones\n", x, cmap->sy-1-y);
  }
}

void map_vias(time_info &tinfo, vector<via_info> &via_infos, via_map &via_maps, const vector<circuit_info> &circuit_infos, const pbm *vias, circuit_map &cmap)
{
  pbm used(cmap.sx, cmap.sy);
  for(int y=0; y<cmap.sy; y++) {
    tinfo.tick(y, cmap.sy);
    for(int x=0; x<cmap.sx; x++) {
      if(vias->p(x, y) && !used.p(x, y)) {
	via_info via(-1, -1);
	fill(x, y, cmap.sx, cmap.sy, 1, boost::bind(map_vias_set, _1, _2, &via, &circuit_infos, &used, &cmap), boost::bind(&pbm::p, vias, _1, _2), boost::bind(&pbm::p, &used, _1, _2));
	if(via.metal == -1)
	  fprintf(stderr, "via at (%d, %d) does not touch the metal\n", x, cmap.sy-1-y);
	if(via.active_poly == -1)
	  fprintf(stderr, "via at (%d, %d) does not touch poly or active\n", x, cmap.sy-1-y);

	int yy = cmap.sy-1-y;
	//	if(!(x >= 6000 && x < 6750 && yy >= 5000 && yy < 6500))
	//	  continue;
	if(0 && x > 6500 && x < 7000 && yy >= 6500 && yy < 7000)
	  continue;
	if(via.metal != -1 && via.active_poly != -1)
	  via_infos.push_back(via);
      }
    }
  }

  for(unsigned int i=0; i != via_infos.size(); i++) {
    via_maps.metal_to_ap[via_infos[i].metal].push_back(via_infos[i].active_poly);
    via_maps.ap_to_metal[via_infos[i].active_poly].push_back(via_infos[i].metal);
  }
}

void via_list_add(list<int> &stack, const map<int, list<int> > vmap, int id, const vector<circuit_info> &circuit_infos)
{
  map<int, list<int> >::const_iterator vi = vmap.find(id);
  if(vi == vmap.end())
    return;
  for(list<int>::const_iterator i = vi->second.begin(); i != vi->second.end(); i++)
    if(circuit_infos[*i].net == -1)
      stack.push_back(*i);
}

void build_nets(time_info &tinfo, vector<net_info> &net_infos, vector<circuit_info> &circuit_infos, const via_map &via_maps, bool has_poly, int sx, int sy)
{
  for(unsigned int i=0; i != circuit_infos.size(); i++) {
    tinfo.tick(i, circuit_infos.size());
    if(circuit_infos[i].type != DISABLED && circuit_infos[i].net == -1) {
      if(circuit_infos[i].type == CAPACITOR || (circuit_infos[i].type == TRANSISTOR && !has_poly))
	continue;
      int nid = net_infos.size();
      net_infos.resize(nid+1);
      set<int> &circuits = net_infos[nid].circuits;
      list<int> stack;
      assert(circuit_infos[i].type != CAPACITOR);
      stack.push_back(i + (circuit_infos[i].type == POLY ? 1000000 : 0));
      do {
	int cid = stack.front();
	int pcid = cid;
	stack.pop_front();
	int is_poly = cid / 1000000;
	cid = cid % 1000000;
	circuit_info &ci = circuit_infos[cid];
	int curnet = is_poly && ci.type == CAPACITOR ? ci.netp : ci.net;
	if(curnet != -1) {
	  if(curnet != nid) {
	    fprintf(stderr, "Network creation failure on %c%d (%d, %d)-(%d, %d), nets %d and %d want to link\n", type_names[ci.type], cid, ci.x0, sy-1-ci.y1, ci.x1, sy-1-ci.y0, nid, curnet);
	    fprintf(stderr, "  pcid=%d\n", pcid);
	    fprintf(stderr, "  net %d (%d/%d):\n", curnet, ci.net, ci.netp);
	    for(set<int>::const_iterator j = net_infos[curnet].circuits.begin(); j != net_infos[curnet].circuits.end(); j++) {
	      const circuit_info &ci1 = circuit_infos[*j];
	      fprintf(stderr, "  - %c%d (%d/%d) (%d, %d)-(%d, %d)", type_names[ci1.type], *j, ci.net, ci.netp, ci1.x0, sy-1-ci1.y1, ci1.x1, sy-1-ci1.y0);
	      for(set<int>::const_iterator j = ci1.neighbors.begin(); j != ci1.neighbors.end(); j++) {
		const circuit_info &ci2 = circuit_infos[*j];
		fprintf(stderr, " %c%d", type_names[ci2.type], *j);
	      }
	      fprintf(stderr, "\n");
	    }
	    fprintf(stderr, "  net %d:\n", nid);
	    for(set<int>::const_iterator j = net_infos[nid].circuits.begin(); j != net_infos[nid].circuits.end(); j++) {
	      const circuit_info &ci1 = circuit_infos[*j];
	      fprintf(stderr, "  - %c%d (%d, %d)-(%d, %d)", type_names[ci1.type], *j, ci1.x0, sy-1-ci1.y1, ci1.x1, sy-1-ci1.y0);
	      for(set<int>::const_iterator j = ci1.neighbors.begin(); j != ci1.neighbors.end(); j++) {
		const circuit_info &ci2 = circuit_infos[*j];
		fprintf(stderr, " %c%d", type_names[ci2.type], *j);
	      }
	      fprintf(stderr, "\n");
	    }
	    exit(1);
	  }
	  continue;
	}

	circuits.insert(cid + (ci.type == CAPACITOR && is_poly ? 1000000 : 0));
	if(ci.type == CAPACITOR && is_poly)
	  ci.netp = nid;
	else
	  ci.net = nid;
	switch(ci.type) {
	case ACTIVE:
	  via_list_add(stack, via_maps.ap_to_metal, cid, circuit_infos);
	  for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++)
	    if(circuit_infos[*j].type == BURIED || circuit_infos[*j].type == CAPACITOR)
	      stack.push_back(*j);
	  break;

	case POLY:
	  via_list_add(stack, via_maps.ap_to_metal, cid, circuit_infos);
	  for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++) {
	    int id = *j;
	    if(circuit_infos[id].net == -1 && (circuit_infos[id].type == BURIED || circuit_infos[id].type == TRANSISTOR))
	      stack.push_back(id + 1000000);
	    if(circuit_infos[id].netp == -1 && circuit_infos[id].type == CAPACITOR)
	      stack.push_back(id + 1000000);
	  }
	  break;

	case METAL:
	  via_list_add(stack, via_maps.metal_to_ap, cid, circuit_infos);
	  break;

	case BURIED:
	  via_list_add(stack, via_maps.ap_to_metal, cid, circuit_infos);
	  for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++) {
	    if(circuit_infos[*j].type == ACTIVE || circuit_infos[*j].type == CAPACITOR)
	      stack.push_back(*j);
	    if(circuit_infos[*j].type == POLY || circuit_infos[*j].type == TRANSISTOR )
	      stack.push_back(*j + 1000000);
	    if(circuit_infos[*j].type == CAPACITOR)
	      stack.push_back(*j + 1000000);
	  }
	  break;

	case TRANSISTOR:
	  for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++) {
	    if(circuit_infos[*j].type == BURIED || circuit_infos[*j].type == POLY || circuit_infos[*j].type == CAPACITOR)
	      stack.push_back(*j + 1000000);
	  }
	  break;

	case CAPACITOR:
	  if(is_poly) {
	    for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++)
	      if(circuit_infos[*j].type == BURIED || circuit_infos[*j].type == POLY || circuit_infos[*j].type == TRANSISTOR)
		stack.push_back(*j + 1000000);
	  } else {
	    for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++)
	      if(circuit_infos[*j].type == BURIED || circuit_infos[*j].type == ACTIVE)
		stack.push_back(*j);
	  }
	  break;

	default:
	  abort();
	}
#if 0
	fprintf(stderr, "  + %c%d %d (%d, %d)-(%d, %d)", type_names[ci.type], cid, is_poly, ci.x0, cmap.sy-1-ci.y1, ci.x1, cmap.sy-1-ci.y0);
	fprintf(stderr, "    ");
	for(list<int>::const_iterator j = stack.begin(); j != stack.end(); j++)
	  fprintf(stderr, " %d", *j);
	fprintf(stderr, "\n");
#endif
      } while(!stack.empty());
    }
  }
}

static double dist_min(double v1, double v2)
{
  if(v2 >= 0 && (v1 < 0 || v2 < v1))
    return v2;
  else
    return v1;
}

double build_transistors_compute_force_dir(const set<int> &src, const set<int> &dst, int gate, const circuit_info &gci, const circuit_map &cmap, int &middle_x, int &middle_y)
{
  int sx = gci.x1-gci.x0+1 + 4;
  int sy = gci.y1-gci.y0+1 + 4;
  double *grid = new double[sx*sy];
  list<pair<int, int> > dests;
  int x0 = gci.x0 - 2;
  int y0 = gci.y0 - 2;
  double *g = grid;
  for(int y=0; y<sy; y++)
    for(int x=0; x<sx; x++) {
      int cet = cmap.p(0, x+x0, y+y0);
      double v = -100000;
      if(cet == gate)
	v = 100000;
      else if(src.find(cet) != src.end())
	v = 0;
      *g++ = v;
      if(x != 0 && x != sx-1 && y != 0 && y != sy-1 && dst.find(cet) != dst.end())
	dests.push_back(pair<int, int>(x, y));
    }

  int minx=1, miny=1, maxx=sx-2, maxy=sy-2;
  while(minx<=maxx) {
    int minx1 = minx;
    int miny1 = miny;
    int maxx1 = maxx;
    int maxy1 = maxy;
    minx = sx;
    miny = sy;
    maxx = -1;
    maxy = -1;
    for(int y=miny1; y<=maxy1; y++)
      for(int x=minx1; x<=maxx1; x++) {
	g = grid + y*sx + x;
	if(*g <= 0)
	  continue;
	double v = -100000;
	v = dist_min(v, g[-1-sx] + M_SQRT2);
	v = dist_min(v, g[  -sx] + 1);
	v = dist_min(v, g[ 1-sx] + M_SQRT2);
	v = dist_min(v, g[-1   ] + 1);
	v = dist_min(v, g[ 1   ] + 1);
	v = dist_min(v, g[-1+sx] + M_SQRT2);
	v = dist_min(v, g[   sx] + 1);
	v = dist_min(v, g[ 1+sx] + M_SQRT2);
	if(v < *g) {
	  *g = v;
	  if(x-1 < minx)
	    minx = x-1;
	  if(x+1 > maxx)
	    maxx = x+1;
	  if(y-1 < miny)
	    miny = y-1;
	  if(y+1 > maxy)
	    maxy = y+1;
	}
      }
  }
  double f = 0;
  double midx = 0, midy = 0;
  for(list<pair<int, int> >::const_iterator i = dests.begin(); i != dests.end(); i++) {
    g = grid + i->second*sx + i->first;
    double v = -100000;
    v = dist_min(v, g[-1-sx] + M_SQRT2);
    v = dist_min(v, g[  -sx] + 1);
    v = dist_min(v, g[ 1-sx] + M_SQRT2);
    v = dist_min(v, g[-1   ] + 1);
    v = dist_min(v, g[ 1   ] + 1);
    v = dist_min(v, g[-1+sx] + M_SQRT2);
    v = dist_min(v, g[   sx] + 1);
    v = dist_min(v, g[ 1+sx] + M_SQRT2);
    if(v > 0) {
      double ff = 1/v;
      f += ff;
      midx += i->first * ff;
      midy += i->second * ff;
    }
  }
  middle_x = x0 + int(midx / f + 0.5);
  middle_y = y0 + int(midy / f + 0.5);
  delete[] grid;
  return f;
}

double build_transistors_compute_force(const set<int> &t1, const set<int> &t2, int gate, const circuit_info &gci, const circuit_map &cmap, int &middle_x, int &middle_y)
{
  double f1, f2;
  int x1, x2, y1, y2;
  f1 = build_transistors_compute_force_dir(t1, t2, gate, gci, cmap, x1, y1);
  f2 = build_transistors_compute_force_dir(t2, t1, gate, gci, cmap, x2, y2);
  middle_x = (x1+x2)/2;
  middle_y = (y1+y2)/2;

  return 0.5*(f1 + f2);
}

void build_transistors(time_info &tinfo, vector<trans_info> &trans_infos, const vector<net_info> &net_infos, const vector<circuit_info> &circuit_infos, const circuit_map &cmap)
{
  for(unsigned int i=0; i != circuit_infos.size(); i++) {
    tinfo.tick(i, circuit_infos.size());
    if(circuit_infos[i].type == TRANSISTOR) {
      list<set<int> > groups;
      vector<bool> is_gate, is_terminal;
      int terminaux, gates;
      build_transistor_groups(groups, terminaux, gates, is_terminal, is_gate, circuit_infos, i);

      int ng = groups.size();
      double *forces = new double[ng*ng];
      int *midx = new int[ng*ng];
      int *midy = new int[ng*ng];
      int j=0;
      for(list<set<int> >::const_iterator ji = groups.begin(); ji != groups.end(); ji++, j++) {
	if(!is_terminal[j])
	  continue;
	int k = j+1;
	list<set<int> >::const_iterator ki = ji;
	ki++;
	for(;ki != groups.end();ki++, k++) {
	  if(!is_terminal[k])
	    continue;
	  int idx = j*ng+k;
	  forces[idx] = build_transistors_compute_force(*ji, *ki, i, circuit_infos[i], cmap, midx[idx], midy[idx]);
	}
      }

      vector<int> nets;
      vector<bool> linked;
      linked.resize(ng);
      for(j=0; j<ng; j++)
	linked[j] = false;

      for(list<set<int> >::const_iterator ji = groups.begin(); ji != groups.end(); ji++)
	nets.push_back(circuit_infos[*ji->begin()].net);

      for(j=0; j<ng; j++) {
	if(linked[j] || !is_terminal[j])
	  continue;
	int best_k = -1;
	double best_f = 0;
	int best_idx = 0;
	for(int k=0; k<ng; k++) {
	  if(j == k || !is_terminal[k])
	    continue;
	  int idx = j < k ? j*ng+k : k*ng+j;
	  double f = forces[idx];
	  if(best_k == -1 || f > best_f) {
	    best_k = k;
	    best_f = f;
	    best_idx = idx;
	  }
	}
	linked[j] = true;
	linked[best_k] = true;
	trans_info ti;
	ti.circ = i;
	ti.t1 = nets[j];
	ti.t2 = nets[best_k];
	ti.gate = circuit_infos[i].net;
	ti.strength = best_f;
	ti.x = midx[best_idx];
	ti.y = midy[best_idx];
	trans_infos.push_back(ti);
      }
      delete[] forces;
      delete[] midx;
      delete[] midy;
    }
  }
}


void build_transistors_metal_gate(time_info &tinfo, vector<trans_info> &trans_infos, const vector<net_info> &net_infos, const vector<circuit_info> &circuit_infos, const circuit_map &cmap)
{
  for(unsigned int i=0; i != circuit_infos.size(); i++) {
    tinfo.tick(i, circuit_infos.size());
    if(circuit_infos[i].type == TRANSISTOR) {
      const circuit_info &ci = circuit_infos[i];
      if(ci.neighbors.size() != 2) {
	fprintf(stderr, "Gate at (%d, %d)-(%d, %d) has %d neighbors.\n",
		ci.x0, cmap.sy-1-ci.y0, ci.x1, cmap.sy-1-ci.y1,
		int(ci.neighbors.size()));
	exit(1);
      }


      trans_info ti;
      ti.circ = i;

      set<int>::const_iterator ni = ci.neighbors.begin();
      set<int> t1, t2;
      t1.insert(*ni);
      ti.t1 = circuit_infos[*ni++].net;
      t2.insert(*ni);
      ti.t2 = circuit_infos[*ni++].net;
      ti.gate = ci.net;
      ti.strength = build_transistors_compute_force(t1, t2, i, ci, cmap, ti.x, ti.y);
      trans_infos.push_back(ti);
    }
  }
}

void circuit_stats(const vector<circuit_info> &circuit_infos)
{
  int t[7];
  t[0] = t[1] = t[2] = t[3] = t[4] = t[5] = t[6] = 0;
  for(unsigned int i=0; i != circuit_infos.size(); i++)
    t[circuit_infos[i].type]++;

  fprintf(stderr, "-> %d active, %d poly, %d metal, %d buried, %d gates, %d capacitors, %d disabled, %d total\n",
	  t[0], t[1], t[2], t[3], t[4], t[5], t[6], t[0]+t[1]+t[2]+t[3]+t[4]+t[5]+t[6]);
}

void dump(FILE *out, int sx, int sy, int nl, const vector<circuit_info> &circuit_infos)
{
  fprintf(out, "%d %d %d\n", sx, sy, nl);
  fprintf(out, "%d circuits\n", int(circuit_infos.size()));
  for(unsigned int i=0; i != circuit_infos.size(); i++) {
    const circuit_info &ci = circuit_infos[i];
    fprintf(out, "%6d %c %5d %5d %5d %5d %5d %5d %d", i, type_names[ci.type], ci.net, ci.netp, ci.x0, sy-1-ci.y1, ci.x1, sy-1-ci.y0, ci.surface);
    for(set<int>::const_iterator j = ci.neighbors.begin(); j != ci.neighbors.end(); j++)
      fprintf(out, " %c%d", type_names[circuit_infos[*j].type], *j);
    fprintf(out, "\n");
  }
}

void dump(FILE *out, int sx, int sy, int nl, const vector<net_info> &net_infos)
{
  fprintf(out, "%d nets\n", int(net_infos.size()));
  for(unsigned int i=0; i != net_infos.size(); i++) {
    const net_info &ni = net_infos[i];
    fprintf(out, "%5d", i);
    for(set<int>::const_iterator j = ni.circuits.begin(); j != ni.circuits.end(); j++)
      fprintf(out, " %d", *j);
    fprintf(out, "\n");
  }
}

void dump(FILE *out, int sx, int sy, int nl, const vector<trans_info> &trans_infos)
{
  fprintf(out, "%d transistors\n", int(trans_infos.size()));
  for(unsigned int i=0; i != trans_infos.size(); i++) {
    const trans_info &ti = trans_infos[i];
    fprintf(out, "%5d %6d %5d %5d %5d %5d %5d %g\n", i, ti.circ, ti.x, sy-1 - ti.y, ti.t1, ti.gate, ti.t2, ti.strength);
  }
}

void dump(const char *fname, int sx, int sy, int nl, const vector<trans_info> &trans_infos, const vector<net_info> &net_infos, const vector<circuit_info> &circuit_infos)
{
  FILE *out = fopen(fname, "w");
  dump(out, sx, sy, nl, circuit_infos);
  dump(out, sx, sy, nl, net_infos);
  dump(out, sx, sy, nl, trans_infos);
  fclose(out);
}

// Add a virtual circuit between the two vcc domains
void add_virtual_circuit(vector<circuit_info> &circuit_infos, metal_link_info &ml, int sy, bool has_poly)
{
  int id = circuit_infos.size();
  circuit_infos.resize(id+1);
  circuit_info &ci = circuit_infos[id];
  ci.type = has_poly ? POLY :  ACTIVE;
  ci.surface = abs(ml.x2-ml.x1)+abs(ml.y2-ml.y1)+1;
  ci.x0 = ml.x1;
  ci.x1 = ml.x2;
  ci.y1 = sy-1-ml.y2;
  ci.y0 = sy-1-ml.y1;
  ci.net = -1;
  ml.circuit_id = id;
}

// Add a pair of virtual vias with the virtual circuit
void add_virtual_vias(vector<via_info> &via_infos, via_map &via_maps, metal_link_info &ml, const circuit_map &cmap)
{
  int vcc1 = cmap.p(cmap.nl >= 3 ? 2 : 1, ml.x1, cmap.sy-1-ml.y1);
  int vcc2 = cmap.p(cmap.nl >= 3 ? 2 : 1, ml.x2, cmap.sy-1-ml.y2);
  via_infos.push_back(via_info(vcc1, ml.circuit_id));
  via_infos.push_back(via_info(vcc2, ml.circuit_id));
  via_maps.metal_to_ap[vcc1].push_back(ml.circuit_id);
  via_maps.metal_to_ap[vcc2].push_back(ml.circuit_id);
  via_maps.ap_to_metal[ml.circuit_id].push_back(vcc1);
  via_maps.ap_to_metal[ml.circuit_id].push_back(vcc2);
}

// In a metal gates circuit, lookup the metal net corresponding to gates and caps
void lookup_gates_and_caps(time_info &tinfo, vector<circuit_info> &circuit_infos, const circuit_map &cmap)
{
  for(int y=0; y<cmap.sy; y++) {
    tinfo.tick(y, cmap.sy);
    for(int x=0; x<cmap.sx; x++) {
      int ca = cmap.p(0, x, y);
      if(ca == -1)
	continue;
      circuit_info &ci = circuit_infos[ca];
      if(ci.type == TRANSISTOR || ci.type == CAPACITOR) {
	int cm = cmap.p(1, x, y);
	if(cm == -1) {
	  fprintf(stderr, "%s does not touch metal at (%d, %d)\n", ci.type == TRANSISTOR ? "Gate" : "Capacitor", x, cmap.sy-1-y);
	  exit(1);
	}
	int nm = circuit_infos[cm].net;
	if(ci.type == TRANSISTOR) {
	  if(ci.net == -1)
	    ci.net = nm;
	  else if(ci.net != nm) {
	    fprintf(stderr, "Transistor touches multiple metal at (%d, %d)\n", x, cmap.sy-1-y);
	    exit(1);
	  }
	} else {
	  if(ci.netp == -1)
	    ci.netp = nm;
	  else if(ci.netp != nm) {
	    fprintf(stderr, "Capacitor touches multiple metal at (%d, %d)\n", x, cmap.sy-1-y);
	    exit(1);
	  }
	}
      }
    }
  }
}


pbm *active = NULL;
pbm *gates = NULL;
pbm *buried = NULL;
pbm *metal = NULL;
pbm *poly = NULL;
pbm *vias = NULL;
pbm *caps = NULL;
list<metal_link_info> metal_links;

const char *map_name = NULL;
const char *list_name = NULL;
int sx;
int sy;

void nmos_poly_single_metal()
{
  vector<circuit_info> circuit_infos;
  vector<via_info> via_infos;
  via_map via_maps;
  vector<net_info> net_infos;
  vector<trans_info> trans_infos;

  circuit_map cmap(map_name, 3, sx, sy, true);

  time_info tinfo;
  tinfo.start("build circuits active/poly");
  build_circuits(tinfo, boost::bind(color_active_poly, _1, _2, active, poly, buried, caps), circuit_infos, &cmap, 0);
  for(list<metal_link_info>::iterator i = metal_links.begin(); i != metal_links.end(); i++)
    add_virtual_circuit(circuit_infos, *i, sy, true);
  circuit_stats(circuit_infos);
  tinfo.start("build circuits metal");
  build_circuits(tinfo, boost::bind(color_metal, _1, _2, metal), circuit_infos, &cmap, 2);
  circuit_stats(circuit_infos);
  tinfo.start("build neighbors");
  build_neighbors(tinfo, circuit_infos, cmap);
  tinfo.start("clean and remap");
  clean_and_remap(tinfo, circuit_infos, cmap);
  circuit_stats(circuit_infos);
  tinfo.start("compressing ids");
  compress_ids(tinfo, circuit_infos, metal_links, cmap);
  circuit_stats(circuit_infos);
  tinfo.start("mapping vias");
  map_vias(tinfo, via_infos, via_maps, circuit_infos, vias, cmap);
  for(list<metal_link_info>::iterator i = metal_links.begin(); i != metal_links.end(); i++)
    add_virtual_vias(via_infos, via_maps, *i, cmap);
  fprintf(stderr, "  -> %d vias mapped\n", int(via_infos.size()));
  tinfo.start("building nets");
  build_nets(tinfo, net_infos, circuit_infos, via_maps, true, sx, sy);
  fprintf(stderr, "  -> %d nets built\n", int(net_infos.size()));
  tinfo.start("building transistors");
  build_transistors(tinfo, trans_infos, net_infos, circuit_infos, cmap);
  fprintf(stderr, "  -> %d transistors built\n", int(trans_infos.size()));
  fprintf(stderr, "Dumping info...\n");
  dump(list_name, sx, sy, 3, trans_infos, net_infos, circuit_infos);
}

void nmos_metal_gate()
{
  vector<circuit_info> circuit_infos;
  vector<via_info> via_infos;
  via_map via_maps;
  vector<net_info> net_infos;
  vector<trans_info> trans_infos;

  circuit_map cmap(map_name, 2, sx, sy, true);
  time_info tinfo;
  tinfo.start("build circuits active/gates");
  build_circuits(tinfo, boost::bind(color_active_gates, _1, _2, active, gates, caps), circuit_infos, &cmap, 0);
  for(list<metal_link_info>::iterator i = metal_links.begin(); i != metal_links.end(); i++)
    add_virtual_circuit(circuit_infos, *i, sy, false);
  circuit_stats(circuit_infos);
  tinfo.start("build circuits metal");
  build_circuits(tinfo, boost::bind(color_metal, _1, _2, metal), circuit_infos, &cmap, 1);
  circuit_stats(circuit_infos);
  tinfo.start("build neighbors");
  build_neighbors(tinfo, circuit_infos, cmap);
  tinfo.start("mapping vias");
  map_vias(tinfo, via_infos, via_maps, circuit_infos, vias, cmap);
  for(list<metal_link_info>::iterator i = metal_links.begin(); i != metal_links.end(); i++)
    add_virtual_vias(via_infos, via_maps, *i, cmap);
  fprintf(stderr, "  -> %d vias mapped\n", int(via_infos.size()));
  tinfo.start("building nets");
  build_nets(tinfo, net_infos, circuit_infos, via_maps, false, sx, sy);
  fprintf(stderr, "  -> %d nets built\n", int(net_infos.size()));
  tinfo.start("lookup gates and caps");
  lookup_gates_and_caps(tinfo, circuit_infos, cmap);
  tinfo.start("building transistors");
  build_transistors_metal_gate(tinfo, trans_infos, net_infos, circuit_infos, cmap);
  fprintf(stderr, "  -> %d transistors built\n", int(trans_infos.size()));
  fprintf(stderr, "Dumping info...\n");
  dump(list_name, sx, sy, 2, trans_infos, net_infos, circuit_infos);
}

int main(int argc, char **argv)
{
  if(argc != 2) {
    fprintf(stderr, "Usage:\n%s config.txt\n", argv[0]);
    exit(1);
  }

  reader rd(argv[1]);
  map_name = rd.gw();
  list_name = rd.gw();
  sx = rd.gi();
  sy = rd.gi();
  rd.nl();

  void (*method)() = NULL;

  while(!rd.eof()) {
    char buf[4096];
    string keyw = rd.gw();

    if(keyw == "nmos-poly-single-metal") {
      method = nmos_poly_single_metal;
      rd.nl();

    } else if(keyw == "nmos-metal-gate") {
      method = nmos_metal_gate;
      rd.nl();

    } else if(keyw == "active") {
      sprintf(buf, "%s.pbm", rd.gwnl());
      rd.nl();
      active = new pbm(buf);

    } else if(keyw == "poly") {
      sprintf(buf, "%s.pbm", rd.gwnl());
      rd.nl();
      poly = new pbm(buf);

    } else if(keyw == "metal") {
      sprintf(buf, "%s.pbm", rd.gwnl());
      rd.nl();
      metal = new pbm(buf);

    } else if(keyw == "buried") {
      sprintf(buf, "%s.pbm", rd.gwnl());
      rd.nl();
      buried = new pbm(buf);

    } else if(keyw == "vias") {
      sprintf(buf, "%s.pbm", rd.gwnl());
      rd.nl();
      vias = new pbm(buf);

    } else if(keyw == "caps") {
      sprintf(buf, "%s.pbm", rd.gwnl());
      rd.nl();
      caps = new pbm(buf);

    } else if(keyw == "gates") {
      sprintf(buf, "%s.pbm", rd.gwnl());
      rd.nl();
      gates = new pbm(buf);

    } else if(keyw == "metal-link") {
      metal_link_info ml;
      ml.x1 = rd.gi();
      ml.y1 = rd.gi();
      ml.x2 = rd.gi();
      ml.y2 = rd.gi();
      ml.circuit_id = -1;
      rd.nl();
      metal_links.push_back(ml);

    } else {
      fprintf(stderr, "Unhandled keyword %s\n", keyw.c_str());
      exit(1);
    }
  }

  if(method)
    method();
  else
    fprintf(stderr, "Method missing, nothing to do.\n");

  return 0;
}
