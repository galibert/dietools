#undef _FORTIFY_SOURCE

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

#include <set>
#include <string>
#include <algorithm>
#include <vector>
#include <map>
#include <list>

using namespace std;

struct point {
  int x, y;

  point() {}
  point(int _x, int _y) { x = _x; y = _y; }
};

enum {
  W_S = 0,
  E_S = 1,
  N_S = 2,
  S_S = 3,
  W_D = 4,
  E_D = 5,
  N_D = 6,
  S_D = 7
};

enum { T1, T2, GATE };
enum { S_0, S_1, S_FLOAT };
enum { N_NORMAL, N_GND, N_VCC };

struct reader {
  char *data, *pos;
  bool has_nl;
  const char *gw();
  const char *gwnl();
  void nl();
  int gi();
  double gd();

  reader(const char *fname);
  ~reader();
};

class net;

class lt {
public:
  string name;

  virtual ~lt();
};

class node : public lt {
public:
  point pos;
  vector<net *> nets;
  vector<int> netids;

  node(reader &rd);
  virtual ~node();
  void resolve_nets(const vector<net *> &nets);
};

class mosfet : public node {
public:
  int orientation;
  bool depletion;
  double f;

  mosfet(reader &rd, bool depletion);
  virtual ~mosfet();
};

class capacitor : public node {
public:
  int orientation;
  double f;

  capacitor(reader &rd);
  virtual ~capacitor();
};

class power_node : public node {
public:
  bool is_vcc;

  power_node(reader &rd, bool is_vcc);
  virtual ~power_node();
};
  
class pad : public node {
public:
  int orientation;

  int name_width, name_height;
  unsigned char *name_image;

  pad(reader &rd);
  virtual ~pad();
};

class net : public lt {
public:
  struct line {
    int p1, p2;
  };

  vector<point> pt;
  vector<line> lines;
  vector<int> dots;

  int id;
  int powernet;
  bool has_gate;

  net(reader &rd, int id);
  virtual ~net();
  bool is_named() const;
};

vector<node *> nodes;
vector<net *> nets;
map<string, net *> netidx;
map<net *, list<mosfet *> > net_to_trans_term;

lt::~lt()
{
}

node::node(reader &rd)
{
  pos.x = rd.gi();
  pos.y = rd.gi();
}

node::~node()
{
}

void node::resolve_nets(const vector<net *> &_nets)
{
  nets.resize(netids.size());
  for(unsigned int i=0; i != nets.size(); i++)
    nets[i] = _nets[netids[i]];
}

power_node::power_node(reader &rd, bool _is_vcc) : node(rd)
{
  is_vcc = _is_vcc;

  netids.resize(1);
  netids[0] = rd.gi();
  name = rd.gwnl();
  rd.nl();
}

power_node::~power_node()
{
}

pad::pad(reader &rd) : node(rd)
{
  netids.resize(1);
  netids[0] = rd.gi();
  orientation = rd.gi();
  name = rd.gwnl();
  rd.nl();
}

pad::~pad()
{
}

mosfet::mosfet(reader &rd, bool _depletion) : node(rd)
{
  depletion = _depletion;
  netids.resize(3);
  netids[T1] = rd.gi();
  netids[GATE] = rd.gi();
  netids[T2] = rd.gi();
  f = rd.gd();
  orientation = rd.gi();
  name = rd.gwnl();
  rd.nl();
}

mosfet::~mosfet()
{
}

capacitor::capacitor(reader &rd) : node(rd)
{
  netids.resize(2);
  netids[T1] = rd.gi();
  netids[T2] = rd.gi();
  f = rd.gd();
  orientation = rd.gi();
  name = rd.gwnl();
  rd.nl();
}

capacitor::~capacitor()
{
}

net::net(reader &rd, int _id)
{
  powernet = N_NORMAL;
  has_gate = false;
  id = _id;
  int nx = rd.gi();
  pt.resize(nx);
  for(int j=0; j != nx; j++) {
    pt[j].x = rd.gi();
    pt[j].y = rd.gi();
  }
  nx = rd.gi();
  lines.resize(nx);
  for(int j=0; j != nx; j++) {
    lines[j].p1 = rd.gi();
    lines[j].p2 = rd.gi();
  }
  nx = rd.gi();
  dots.resize(nx);
  for(int j=0; j != nx; j++)
    dots[j] = rd.gi();
  name = rd.gwnl();
  rd.nl();
}

net::~net()
{
}

bool net::is_named() const
{
  return name[0] < '0' || name[0] > '9';
}

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

void state_load(const char *fname)
{
  reader rd(fname);

  rd.nl();

  int nn = rd.gi();
  rd.nl();
  nodes.resize(nn);
  for(int i=0; i != nn; i++) {
    node *n;
    const char *ts = rd.gw();
    switch(ts[0]) {
    case 't':
    case 'd':
      n = new mosfet(rd, ts[0] == 'd');
      break;
    case 'v':
    case 'g':
      n = new power_node(rd, ts[0] == 'v');
      break;
    case 'p':
      n = new pad(rd);
      break;
    case 'c':
      n = new capacitor(rd);
      break;
    default:
      abort();
    }
    nodes[i] = n;
  }

  nn = rd.gi();
  rd.nl();
  nets.resize(nn);
  for(int i=0; i != nn; i++) {
    net *n = new net(rd, i);
    nets[i] = n;
    netidx[n->name] = n;
  }

  for(unsigned int i=0; i != nodes.size(); i++)
    nodes[i]->resolve_nets(nets);

  for(unsigned int i=0; i != nodes.size(); i++) {
    power_node *pn = dynamic_cast<power_node *>(nodes[i]);
    if(pn)
      pn->nets[0]->powernet = pn->is_vcc ? N_VCC : N_GND;
  }

  for(unsigned int i=0; i != nodes.size(); i++) {
    mosfet *t = dynamic_cast<mosfet *>(nodes[i]);
    if(t) {
      net_to_trans_term[t->nets[T1]].push_back(t);
      if(t->nets[T1] != t->nets[T2])
	net_to_trans_term[t->nets[T2]].push_back(t);
      t->nets[GATE]->has_gate = true;
    }
  }
}

string escape(string n)
{
  for(unsigned int i=0; i != n.size(); i++) {
    char c = n[i];
    if(c == '.' || c == '-' || c == ' ' || c == '\'' || c == '+' || c == '<' || c == '>')
      n[i] = '_';
  }
  if(n[0] >= '0' && n[0] <= '9')
    n = 'n' + n;
  return n;
}

struct mapper_term {
  enum {
    P_1 = -1,
    P_0 = -2
  };

  bool depletion;
  int netvar[3];
};

struct mapper {
  vector<mapper_term> terms;
  vector<net *> nets;
  set<int> outputs;

  int count_t, count_d, count_v;
};

string netvar_name(int id)
{
  char buf[16];
  if(id == mapper_term::P_0)
    return "0";
  else if(id == mapper_term::P_1)
    return "1";
  else if(id < 26)
    sprintf(buf, "%c", 'a'+id);
  else
    sprintf(buf, "<%d>", id);
  return buf;
}

void make_counts(mapper &m)
{
  m.count_t = m.count_d = m.count_v = 0;

  for(auto i : m.terms) {
    if(i.depletion)
      m.count_d++;
    else
      m.count_t++;
    for(int j=0; j<3; j++)
      if(m.count_v <= i.netvar[j])
	m.count_v = i.netvar[j]+1;
  }
}

void build_net_list(set<net *> &nets, net *root)
{
  list<net *> stack;
  stack.push_back(root);
  while(!stack.empty()) {
    net *n = stack.front();
    stack.pop_front();
    list<mosfet *> &trans = net_to_trans_term[n];
    for(list<mosfet *>::iterator i = trans.begin(); i != trans.end(); i++) {
      mosfet *t = *i;
      for(int term=0; term<3; term++) {
	net *n1 = t->nets[term];
	if(nets.find(n1) == nets.end() && (n1 == root || !n1->is_named())) {
	  nets.insert(n1);
	  stack.push_back(n1);
	}
      }
    }
  }
}

void build_net_groups(list<set<net *>> &netgroups, const set<net *> &nets)
{
  set<net *> done;
  for(auto i : nets) {
    if(done.find(i) == done.end() && !i->powernet) {
      netgroups.push_back(set<net *>());
      auto &g = netgroups.back();
      list<net *> stack;
      stack.push_back(i);
      g.insert(i);
      while(!stack.empty()) {
	net *n = stack.front();
	stack.pop_front();
	done.insert(n);
	for(const auto t : net_to_trans_term[n]) {
	  for(int term=0; term<3; term++)
	    if(term != GATE) {
	      net *n1 = t->nets[term];
	      if(nets.find(n1) != nets.end() && !n1->powernet && g.find(n1) == g.end()) {
		g.insert(n1);
		stack.push_back(n1);
	      }
	    }
	}
      }
    }
  }
}

void build_mapper(mapper &m, const set<net *> &g)
{
  set<const mosfet *> done;
  map<net *, int> ids;

  for(auto i : g) {
    for(const auto t : net_to_trans_term[i])
      if(done.find(t) == done.end()) {
	m.terms.resize(m.terms.size()+1);
	mapper_term &mt = m.terms.back();
	mt.depletion = t->depletion;
	for(int term=0; term<3; term++) {
	  net *n = t->nets[term];
	  if(n->powernet)
	    mt.netvar[term] = n->powernet == N_VCC ? mapper_term::P_1 : mapper_term::P_0;
	  else {
	    auto idi = ids.find(n);
	    int id;
	    if(idi == ids.end()) {
	      id = m.nets.size();
	      ids[n] = id;
	      m.nets.push_back(n);
	      if(g.find(n) != g.end() && (n->has_gate || n->is_named()))
		m.outputs.insert(id);
	    } else
	      id = idi->second;
	    mt.netvar[term] = id;
	  }
	}
	done.insert(t);
      }
  }
  make_counts(m);
}

string mapper_to_eq(const mapper &m)
{
  string r;
  for(auto i : m.terms) {
    r += i.depletion ? 'd' : 't';
    r += netvar_name(i.netvar[T1]);
    r += netvar_name(i.netvar[GATE]);
    r += netvar_name(i.netvar[T2]);
    r +=' ';
  }
  r += '+';
  for(auto i : m.outputs)
    r += netvar_name(i);
  return r;
}

string vname(const mapper &m, const vector<int> &vars, int id)
{
  net *n = m.nets[vars[id]];
  if(n->is_named())
    return n->name;
  else
    return 'n' + n->name;
}

void handler_d1aa_tab0__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !%s;\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str());
}

void handler_t0ba_ta11__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !%s;\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str());
}

void handler_tab0_tac0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str());
}

void handler_tab0_tac0_tad0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str());
}

void handler_tab0_tac0_tad0_tae0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str(),
	 vname(m, vars, 9).c_str());
}


void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str(),
	 vname(m, vars, 9).c_str(),
	 vname(m, vars, 10).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str(),
	 vname(m, vars, 9).c_str(),
	 vname(m, vars, 10).c_str(),
	 vname(m, vars, 11).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str(),
	 vname(m, vars, 9).c_str(),
	 vname(m, vars, 10).c_str(),
	 vname(m, vars, 11).c_str(),
	 vname(m, vars, 12).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_tan0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str(),
	 vname(m, vars, 9).c_str(),
	 vname(m, vars, 10).c_str(),
	 vname(m, vars, 11).c_str(),
	 vname(m, vars, 12).c_str(),
	 vname(m, vars, 13).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_tan0_tao0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str(),
	 vname(m, vars, 9).c_str(),
	 vname(m, vars, 10).c_str(),
	 vname(m, vars, 11).c_str(),
	 vname(m, vars, 12).c_str(),
	 vname(m, vars, 13).c_str(),
	 vname(m, vars, 14).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_tan0_tao0_tap0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str(),
	 vname(m, vars, 9).c_str(),
	 vname(m, vars, 10).c_str(),
	 vname(m, vars, 11).c_str(),
	 vname(m, vars, 12).c_str(),
	 vname(m, vars, 13).c_str(),
	 vname(m, vars, 14).c_str(),
	 vname(m, vars, 15).c_str());
}

void handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_tan0_tao0_tap0_taq0_daa1__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str(),
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 5).c_str(),
	 vname(m, vars, 6).c_str(),
	 vname(m, vars, 7).c_str(),
	 vname(m, vars, 8).c_str(),
	 vname(m, vars, 9).c_str(),
	 vname(m, vars, 10).c_str(),
	 vname(m, vars, 11).c_str(),
	 vname(m, vars, 12).c_str(),
	 vname(m, vars, 13).c_str(),
	 vname(m, vars, 14).c_str(),
	 vname(m, vars, 15).c_str(),
	 vname(m, vars, 16).c_str());
}

void handler_tab0_daa1_tadc_tcfe_tehg__acf(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !%s;\n", vname(m, vars, 0).c_str(), vname(m, vars, 1).c_str());
  printf("  set_t(%s, %s || %s, (%s && %s) || (%s && %s))\n", vname(m, vars, 2).c_str(), vname(m, vars, 3).c_str(), vname(m, vars, 5).c_str(), vname(m, vars, 3).c_str(), vname(m, vars, 0).c_str(), vname(m, vars, 5).c_str(), vname(m, vars, 2).c_str());
  printf("  set_t(%s, %s || %s, (%s && %s) || (%s && %s))\n", vname(m, vars, 4).c_str(), vname(m, vars, 5).c_str(), vname(m, vars, 7).c_str(), vname(m, vars, 5).c_str(), vname(m, vars, 2).c_str(), vname(m, vars, 7).c_str(), vname(m, vars, 4).c_str());
  printf("  set_t(%s, %s, %s)\n", vname(m, vars, 6).c_str(), vname(m, vars, 7).c_str(), vname(m, vars, 4).c_str());
}

void handler_ta1b__a(const mapper &m, const vector<int> &vars)
{
  printf("  %s = %s;\n", vname(m, vars, 0).c_str(), vname(m, vars, 1).c_str());
}

void handler_t0ba_tadc__a(const mapper &m, const vector<int> &vars)
{
  printf("  set_t(%s, %s || %s, %s && !%s);\n", vname(m, vars, 0).c_str(), vname(m, vars, 1).c_str(), vname(m, vars, 3).c_str(), vname(m, vars, 2).c_str(), vname(m, vars, 1).c_str());
}

void handler_t0ba_daa1_ta1c__ac(const mapper &m, const vector<int> &vars)
{
  printf("  %s = %s = !%s;\n", vname(m, vars, 2).c_str(), vname(m, vars, 0).c_str(), vname(m, vars, 1).c_str());
}

void handler_tab0_tac0_daa1_ta1d__ad(const mapper &m, const vector<int> &vars)
{
  printf("  %s = %s = !(%s || %s);\n",
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str());
}

void handler_tab0_tac0_daa1_taed__ad(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !(%s || %s);\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str(),
	 vname(m, vars, 2).c_str());
  printf("  set_t(%s, %s, %s);\n",
	 vname(m, vars, 3).c_str(),
	 vname(m, vars, 4).c_str(),
	 vname(m, vars, 0).c_str());

}

template<int n> void handler_tab1_tac0__a(const mapper &m, const vector<int> &vars)
{
  printf("  set_01(%s, %s",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 2).c_str());
  for(int i=1; i<n; i++)
    printf(" || %s",
	   vname(m, vars, 2+i).c_str());
  printf(", %s);\n",
	 vname(m, vars, 1).c_str());	 
}

void handler_t0ba_daa1_tadc_tcfe__ac(const mapper &m, const vector<int> &vars)
{
  printf("  %s = !%s\n",
	 vname(m, vars, 0).c_str(),
	 vname(m, vars, 1).c_str());
  printf("  set_01(%s, %s, %s)\n",
	 
struct handler {
  mapper m;
  void (*f)(const mapper &m, const vector<int> &vars);

  handler(mapper _m, void (*_f)(const mapper &m, const vector<int> &vars)) { m = _m; f = _f; }
};

list<handler> handlers;

int get_netvar(const char *&eq)
{
  if(*eq == '0') {
    eq++;
    return mapper_term::P_0;
  }
  if(*eq == '1') {
    eq++;
    return mapper_term::P_1;
  }
  if(*eq != '<')
    return *eq++ - 'a';
  int id = strtol(eq+1, 0, 10);
  while(*eq++ != '>');
  return id;
}

void eq_parse(mapper &m, const char *eq)
{
  while(*eq) {
    switch(*eq++) {
    case 'd':
    case 't': {
      m.terms.resize(m.terms.size()+1);
      mapper_term &mt = m.terms.back();
      mt.depletion = eq[-1] == 'd';
      mt.netvar[T1] = get_netvar(eq);
      mt.netvar[GATE] = get_netvar(eq);
      mt.netvar[T2] = get_netvar(eq);
      eq++;
      break;
    }
      
    case '+':
      while(*eq)
	m.outputs.insert(get_netvar(eq));
      break;

    default:
      abort();
    }
  }
  make_counts(m);
}

void reg(const char *eq, void (*f)(const mapper &m, const vector<int> &vars))
{
  mapper m;
  eq_parse(m, eq);
  handlers.push_back(handler(m, f));
}

void register_handlers()
{
  reg("d1aa tab0 +a", handler_d1aa_tab0__a);
  reg("tab0 tac0 daa1 +a", handler_tab0_tac0_daa1__a);
  reg("tab0 tac0 ta11 +a", handler_tab0_tac0_daa1__a);
  reg("tab0 tac0 tad0 daa1 +a", handler_tab0_tac0_tad0_daa1__a);
  reg("tab0 tac0 tad0 tae0 daa1 +a", handler_tab0_tac0_tad0_tae0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_tan0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_tan0_tao0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 tap0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_tan0_tao0_tap0_daa1__a);
  reg("tab0 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 tap0 taq0 daa1 +a", handler_tab0_tac0_tad0_tae0_taf0_tag0_tah0_tai0_taj0_tak0_tal0_tam0_tan0_tao0_tap0_taq0_daa1__a);

  reg("tab1 tac0 +a", handler_tab1_tac0__a<1>);
  reg("tab1 tac0 tad0 +a", handler_tab1_tac0__a<2>);
  reg("tab1 tac0 tad0 tae0 +a", handler_tab1_tac0__a<3>);
  reg("tab1 tac0 tad0 tae0 taf0 +a", handler_tab1_tac0__a<4>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 +a", handler_tab1_tac0__a<5>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 +a", handler_tab1_tac0__a<6>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 +a", handler_tab1_tac0__a<7>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 +a", handler_tab1_tac0__a<8>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 +a", handler_tab1_tac0__a<9>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 +a", handler_tab1_tac0__a<10>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 +a", handler_tab1_tac0__a<11>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 +a", handler_tab1_tac0__a<12>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 +a", handler_tab1_tac0__a<13>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 tap0 +a", handler_tab1_tac0__a<14>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 tap0 taq0 +a", handler_tab1_tac0__a<15>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 tap0 taq0 tar0 +a", handler_tab1_tac0__a<16>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 tap0 taq0 tar0 tas0 +a", handler_tab1_tac0__a<17>);
  reg("tab1 tac0 tad0 tae0 taf0 tag0 tah0 tai0 taj0 tak0 tal0 tam0 tan0 tao0 tap0 taq0 tar0 tas0 tat0 +a", handler_tab1_tac0__a<18>);

  reg("tab0 daa1 tadc tcfe tehg +acf", handler_tab0_daa1_tadc_tcfe_tehg__acf);
  reg("ta1b +a", handler_ta1b__a);
  reg("t0ba tadc +a", handler_t0ba_tadc__a);
  reg("t0ba daa1 ta1c +ac", handler_t0ba_daa1_ta1c__ac);
  reg("tab0 tac0 daa1 ta1d +ad", handler_tab0_tac0_daa1_ta1d__ad);
  reg("tab0 tac0 daa1 taed +ad", handler_tab0_tac0_daa1_taed__ad);
  reg("t0ba ta11 +a", handler_t0ba_ta11__a);
  reg("t0ba daa1 tadc tcfe +ac", handler_t0ba_daa1_tadc_tcfe__ac);
}

bool unify(const mapper &m1, const mapper &m2, vector<int> &vars)
{
  if(m1.count_t != m2.count_t || m1.count_d != m2.count_d || m1.count_v != m2.count_v || m1.outputs.size() != m2.outputs.size())
    return false;

  if(0)
    fprintf(stderr, "unify:\n-- %s\n-- %s\n", mapper_to_eq(m1).c_str(), mapper_to_eq(m2).c_str());  

  for(auto &i : vars)
    i = -1;

  int nterm = m1.count_t + m1.count_d;

  list<int> match_order;
  set<int> match_tagged;

  list<int> match_unordered;
  for(int i=0; i != nterm; i++)
      match_unordered.push_back(i);

  while(!match_unordered.empty()) {
    int best_free_count = 0;
    list<int>::iterator best_free;
    for(list<int>::iterator i = match_unordered.begin(); i != match_unordered.end();) {
      const mapper_term &m = m1.terms[*i];
      int count = 0;
      for(unsigned int j=0; j != 3; j++)
	if(m.netvar[j] >= 0 && match_tagged.find(m.netvar[j]) == match_tagged.end())
	  count++;
      if(!count) {
	match_order.push_back(*i);
	list<int>::iterator ii = i;
	i++;
	match_unordered.erase(ii);
      } else {
	if(best_free_count == 0 || count < best_free_count) {
	  best_free_count = count;
	  best_free = i;
	}
	i++;
      }	
    }
    if(best_free_count == 0) {
      assert(match_unordered.empty());
      break;
    }
    match_order.push_back(*best_free);
    const mapper_term &m = m1.terms[*best_free];
    for(unsigned int j=0; j != 3; j++)
      if(m.netvar[j] >= 0)
	match_tagged.insert(m.netvar[j]);
    match_unordered.erase(best_free);
  }

  int slot = 0;
  vector<int> cursors;
  vector<int> alts;
  vector<bool> fixed;
  cursors.resize(nterm);
  alts.resize(nterm);
  list<int>::iterator cur_match = match_order.begin();
  map<int, int> cur_nodes;
  map<int, int> cur_nets;
  map<int, int> net_slots;
  set<int> used_nodes;
  set<int> used_nets;
  goto changed_slot;

 changed_slot:
  {
    if(0)
      fprintf(stderr, "changed slot %d\n", slot);
    cursors[slot] = 0;
  }

 try_slot:
  {
    if(0)
      fprintf(stderr, "try slot %d\n", slot);
    const mapper_term &me = m1.terms[*cur_match];
    assert(cursors[slot] != nterm);
    int n = cursors[slot];
    if(used_nodes.find(n) != used_nodes.end())
      goto next_non_alt_in_slot;

    bool compatible = me.depletion == m2.terms[n].depletion;

    if(!compatible)
      goto next_non_alt_in_slot;

    used_nodes.insert(n);
    if(0)
      fprintf(stderr, "slot %d adding %d\n", slot, *cur_match);
    cur_nodes[*cur_match] = n;
    alts[slot] = 0;
    goto try_alt;
  }

 try_alt:
  {
    if(0) {
      fprintf(stderr, "try alt %d %d %d\n", slot, alts[slot], *cur_match);
    }
    const mapper_term &me = m1.terms[*cur_match];
    int n = cursors[slot];
    vector<int> params;
    params.resize(3);
    switch(alts[slot]) {
    case 0:
      params[T1] = m2.terms[n].netvar[T1];
      params[GATE] = m2.terms[n].netvar[GATE];
      params[T2] = m2.terms[n].netvar[T2];
      break;
    case 1:
      params[T1] = m2.terms[n].netvar[T2];
      params[GATE] = m2.terms[n].netvar[GATE];
      params[T2] = m2.terms[n].netvar[T1];
      break;
    }

    map<int, int> temp_nets;
    set<int> temp_used_nets;
    for(unsigned int i=0; i != 3; i++) {
      if(params[i] >= 0 && me.netvar[i] >= 0) {
	if(0) {
	  fprintf(stderr, "  check param %d, m1[%d] vs. m2[%d]\n", i, me.netvar[i], params[i]);
	}
	map<int, int>::const_iterator ni = cur_nets.find(me.netvar[i]);	
	if(ni == cur_nets.end()) {
	  ni = temp_nets.find(me.netvar[i]);
	  if(ni == temp_nets.end()) {
	    if(used_nets.find(params[i]) != used_nets.end())
	      goto next_alt_in_slot;
	    if(temp_used_nets.find(params[i]) != temp_used_nets.end())
	      goto next_alt_in_slot;
	    if(0)
	      fprintf(stderr, "    acceptable, m1[%d] = m2[%d]\n", me.netvar[i], params[i]);
	    temp_nets[me.netvar[i]] = params[i];
	    temp_used_nets.insert(params[i]);
	    continue;
	  }
	}
	if(ni->second != params[i])
	  goto next_alt_in_slot;
	else if(0)
	  fprintf(stderr, "    all good\n");
      } else if(params[i] != me.netvar[i]) {
	if(0)
	  fprintf(stderr, "  param %d wiring mismatched (%d / %d)\n", i, me.netvar[i], params[i]);
	goto next_alt_in_slot;
      } else if(0)
	fprintf(stderr, "  param %d wired ok (%d)\n", i, params[i]);
    }

    for(map<int, int>::const_iterator i = temp_nets.begin(); i != temp_nets.end(); i++) {
      cur_nets[i->first] = i->second;
      used_nets.insert(i->second);
      net_slots[i->first] = slot;
    }
    goto advance_slot;
  }

 next_alt_in_slot:
  {
    if(0)
      fprintf(stderr, "next alt in slot %d %d\n", slot, alts[slot]);
    alts[slot]++;
    if(alts[slot] == 2) {
      int n = cursors[slot];   
      assert(used_nodes.find(n) != used_nodes.end());
      used_nodes.erase(used_nodes.find(n));
      if(0)
	fprintf(stderr, "slot %d removing %d\n", slot, *cur_match);
      cur_nodes.erase(cur_nodes.find(*cur_match));
      goto next_non_alt_in_slot;
    }
    goto try_alt;
  }

 next_non_alt_in_slot:
  {
    if(0)
      fprintf(stderr, "next non alt in slot %d\n", slot);
    
    cursors[slot]++;
    if(cursors[slot] == nterm)
      goto backoff_slot;
    goto try_slot;
  }

 backoff_slot:
  {
    if(0)
      fprintf(stderr, "backoff %d\n", slot);
    slot--;
    if(slot < 0)
      goto no_match;
    cur_match--;
    const mapper_term &me = m1.terms[*cur_match];
    for(unsigned int i=0; i != 3; i++)
      if(me.netvar[i] >= 0) {
	map<int, int>::iterator j = net_slots.find(me.netvar[i]);
	if(j != net_slots.end() && j->second == slot) {
	  assert(used_nets.find(cur_nets[me.netvar[i]]) != used_nets.end());
	  used_nets.erase(used_nets.find(cur_nets[me.netvar[i]]));;
	  net_slots.erase(j);
	  cur_nets.erase(cur_nets.find(me.netvar[i]));
	}
      }
    goto next_alt_in_slot;
  }

 advance_slot:
  {
    if(0)
      fprintf(stderr, "advance slot %d\n", slot);
    slot++;
    cur_match++;
    if(cur_match == match_order.end())
      goto match;
    goto changed_slot;
  }

 no_match:
  return false;

 match:
  vars.resize(m1.count_v);
  for(map<int, int>::const_iterator i = cur_nets.begin(); i != cur_nets.end(); i++)
    vars[i->first] = i->second;

  return true;
}

bool handle(const mapper &m)
{
  vector<int> vars;
  vars.resize(m.count_v);

  for(auto i : handlers)
    if(unify(i.m, m, vars)) {
      i.f(m, vars);
      return true;
    }

  return false;
}


void logx(const char *name)
{
  net *root = netidx[name];
  set<net *> nets;
  list<set<net *>> netgroups;
  build_net_list(nets, root);
  build_net_groups(netgroups, nets);

  for(auto j : netgroups) {
    mapper m;
    build_mapper(m, j);
    if(!handle(m)) {
      string eq = mapper_to_eq(m);
      printf("group: %s  %s\n", eq.c_str(), escape(eq).c_str());
      for(unsigned int i=0; i != m.nets.size(); i++)
	printf("  %s %s\n", netvar_name(i).c_str(), m.nets[i]->name.c_str());
    }
  }
}

int main(int argc, char **argv)
{
  state_load(argv[1]);

  register_handlers();

  logx(argv[2]);

  return 0;
}
