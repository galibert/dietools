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
    }
  }
}

string escape(string n)
{
  for(unsigned int i=0; i != n.size(); i++) {
    char c = n[i];
    if(c == '.' || c == '-' || c == ' ' || c == '\'')
      n[i] = '_';
  }
  if(n[0] >= '0' && n[0] <= '9')
    n = 'n' + n;
  return n;
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
	if(nets.find(n1) == nets.end()) {
	  nets.insert(n1);
	  if(!n1->is_named())
	    stack.push_back(n1);
	}
      }
    }
  }
}

void logx(const char *name)
{
  net *root = netidx[name];
  set<net *> nets;
  build_net_list(nets, root);

  for(set<net *>::iterator i = nets.begin(); i != nets.end(); i++)
    if(!(*i)->powernet)
      printf("%p %s\n", *i, (*i)->name.c_str());
}

int main(int argc, char **argv)
{
  state_load(argv[1]);

  logx(argv[2]);

  return 0;
}
