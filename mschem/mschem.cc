#undef _FORTIFY_SOURCE

#include "globals.h"
#include "pad_info.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
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

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <ft2build.h>
#include FT_FREETYPE_H

const char *opt_text, *opt_svg, *opt_tiles;

double ratio;
int sy1;

enum {
  WX = 800,
  WY = 600,
  PATCH_SX = 512,
  PATCH_SY = 512
};

struct time_info {
  timeval start_time;
  int lsec;
};

struct cglyph {
  int sx, sy, dx, dy, left, top;
  unsigned char *image;
};

static FT_Library flib;
static FT_Face face;
static double cached_size, cached_rot;
static map<int, cglyph> cached_glyphs;


void freetype_init()
{
  FT_Init_FreeType(&flib);
  if(FT_New_Face(flib, "/usr/share/fonts/corefonts/times.ttf", 0, &face)) {
    fprintf(stderr, "Font opening error\n");
    exit(1);
  }
  FT_Select_Charmap(face, FT_ENCODING_UNICODE);
  cached_size = cached_rot = 0;
}

static void freetype_render(const char *str, double size, double rot, int &width, int &height, unsigned char *&image)
{
  if(size != cached_size || rot != cached_rot) {
    for(map<int, cglyph>::iterator i = cached_glyphs.begin(); i != cached_glyphs.end(); i++)
      delete[] i->second.image;
    cached_glyphs.clear();
  }

  FT_Set_Char_Size(face, 0, 64*size, 900, 900);
  int c = 0x10000*cos(rot*M_PI/180);
  int s = 0x10000*sin(rot*M_PI/180);

  FT_Matrix rotation;
  rotation.xx = c;
  rotation.xy = -s;
  rotation.yx = s;
  rotation.yy = c;

  FT_Vector pos;
  pos.x = pos.y = 0;
  FT_GlyphSlot slot = face->glyph;
  int xmin = 0, xmax = 0, ymin = 0, ymax = 0;
  int cx = 0, cy = 0;
  for(const char *p = str; *p; p++) {
    char cc = *p;
    cglyph &cg = cached_glyphs[cc];
    if(!cg.image) {
      FT_Set_Transform(face, &rotation, &pos);
      FT_Load_Char(face, cc, FT_LOAD_RENDER);
      assert(slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY);
      assert(slot->bitmap.num_grays == 256);
      cg.sx = slot->bitmap.width;
      cg.sy = slot->bitmap.rows;
      cg.left = slot->bitmap_left;
      cg.top = slot->bitmap_top;
      cg.dx = (slot->advance.x < 0 ? slot->advance.x - 32 : slot->advance.x + 32)/64;
      cg.dy = -(slot->advance.y < 0 ? slot->advance.y - 32 : slot->advance.y + 32)/64;
      cg.image = new unsigned char[cg.sx*cg.sy];
      const unsigned char *src = slot->bitmap.buffer;
      unsigned char *dst = cg.image;
      for(int y=0; y<cg.sy; y++) {
	for(int x=0; x<cg.sx; x++)
	  dst[x] = src[x] ^ 0xff;
	src += slot->bitmap.pitch;
	dst += cg.sx;
      }
    }
    int x0 = cx + cg.left;
    int x1 = x0 + cg.sx;
    int y0 = cy - cg.top;
    int y1 = y0 + cg.sy;
    if(x0 < xmin)
      xmin = x0;
    if(x1 > xmax)
      xmax = x1;
    if(y0 < ymin)
      ymin = y0;
    if(y1 > ymax)
      ymax = y1;
    cx += cg.dx;
    cy += cg.dy;
  }
    
  width = xmax - xmin;
  height = ymax - ymin;
  image = new unsigned char[width*height];
  memset(image, 0xff, width*height);
  cx = -xmin;
  cy = -ymin;

  for(const char *p = str; *p; p++) {
    char cc = *p;
    cglyph &cg = cached_glyphs[cc];
    unsigned char *dest = image + cx+cg.left + (cy-cg.top) * width;
    const unsigned char *src = cg.image;
    for(int yy=0; yy<cg.sy; yy++) {
      unsigned char *d1 = dest;
      const unsigned char *s1 = src;
      for(int xx=0; xx<cg.sx; xx++) {
	if(*d1 > *s1)
	  *d1 = *s1;
	s1++;
	d1++;
      }
      dest += width;
      src += cg.sx;
    }

    cx += cg.dx;
    cy += cg.dy;
  }
}

void start(time_info &tinfo, const char *msg)
{
  fprintf(stderr, "%s\n", msg);
  gettimeofday(&tinfo.start_time, NULL);
  tinfo.lsec = 0;
}

void tick(time_info &tinfo, int pos, int max)
{
  double ratio = double(pos+1)/double(max);
  timeval ctime;
  gettimeofday(&ctime, NULL);
  int ms = (ctime.tv_sec - tinfo.start_time.tv_sec) * 1000 + (ctime.tv_sec - tinfo.start_time.tv_sec) / 1000;
  int rt = pos+1 == max ? ms : ms/ratio*(1-ratio);
  int rts = rt/1000;
  if(rts != tinfo.lsec || pos+1 == max) {
    tinfo.lsec = rts;
    fprintf(stderr, " %3d%% %6d:%02d%c", int(100*ratio+0.5), rts / 60, rts % 60, pos+1 == max ? '\n' : '\r');
  }
}

class patch {
public:
  unsigned char data[(PATCH_SX+1)*PATCH_SY];
  patch();
  ~patch();

  void clear();
  void hline(int ox, int oy, int x1, int x2, int y);
  void vline(int ox, int oy, int x, int y1, int y2);
  void rect(int ox, int oy, int x1, int y1, int x2, int y2);
  void line(int ox, int oy, int x1, int y1, int x2, int y2);
  void invert(int ox, int oy, int x, int y);
  void mipmap(const patch &p1, int dx, int dy);
  void bitmap_blend(int ox, int oy, const unsigned char *src, int src_w, int src_h, int x, int y);

  void save_png(const char *fname);

private:
  static void w32(unsigned char *p, unsigned int l);
  static void wchunk(int fd, unsigned int type, unsigned char *p, unsigned int l);
  inline unsigned char *base(int ox, int oy, int x, int y);
};

patch::patch()
{
  clear();
}

patch::~patch()
{
}

void patch::clear()
{
  memset(data, 0xff, (PATCH_SX+1)*PATCH_SY);
  for(int i=0; i<PATCH_SY; i++)
    data[i*(PATCH_SX+1)] = 0;
}

unsigned char *patch::base(int ox, int oy, int x, int y)
{
  return data + 1 + (y-oy)*(PATCH_SX+1) + (x-ox);
}

void patch::w32(unsigned char *p, unsigned int l)
{
  p[0] = l>>24;
  p[1] = l>>16;
  p[2] = l>>8;
  p[3] = l;
}

void patch::wchunk(int fd, unsigned int type, unsigned char *p, unsigned int l)
{
  unsigned char v[8];
  unsigned int crc;
  w32(v, l);
  w32(v+4, type);
  write(fd, v, 8);
  crc = crc32(0, v+4, 4);
  if(l) {
    write(fd, p, l);
    crc = crc32(crc, p, l);
  }
  w32(v, crc);
  write(fd, v, 4);
}

void patch::save_png(const char *fname)
{
  char msg[4096];
  sprintf(msg, "Error opening %s for writing", fname);
  int fd = open(fname, O_WRONLY|O_CREAT|O_TRUNC, 0666);
  if(fd < 0) {
    perror(msg);
    exit(1);
  }


  write(fd, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8);

  unsigned char h[13];
  w32(h, PATCH_SX);
  w32(h+4, PATCH_SY);
  h[8] = 8; // bpp
  h[9] = 0; // grayscale
  h[10] = 0; // compression
  h[11] = 0; // filter
  h[12] = 0; // interlace
  wchunk(fd, 0x49484452L, h, 13); // IHDR

  int len = int((PATCH_SX+1)*PATCH_SY*1.1 + 12);
  unsigned char *res = new unsigned char[len];
  unsigned long sz = len;
  compress(res, &sz, data, (PATCH_SX+1)*PATCH_SY);
  wchunk(fd, 0x49444154L, res, sz); // IDAT
  wchunk(fd, 0x49454E44L, 0, 0); // IEND
  close(fd);

  delete[] res;
}

void patch::hline(int ox, int oy, int x1, int x2, int y)
{
  if(y < oy || y >= oy + PATCH_SY)
    return;
  if(x2 < ox || x1 >= ox + PATCH_SX)
    return;
  if(x1 < ox)
    x1 = ox;
  if(x2 >= ox + PATCH_SX)
    x2 = ox + PATCH_SX - 1;
  memset(base(ox, oy, x1, y), 0, x2-x1+1);
}

void patch::vline(int ox, int oy, int x, int y1, int y2)
{
  if(x < ox || x >= ox + PATCH_SX)
    return;
  if(y2 < oy || y1 >= oy + PATCH_SY)
    return;
  if(y1 < oy)
    y1 = oy;
  if(y2 >= oy + PATCH_SX)
    y2 = oy + PATCH_SX - 1;
  unsigned char *p = base(ox, oy, x, y1);
  for(int y=y1; y <= y2; y++) {
    *p = 0;
    p += PATCH_SX + 1;
  }
}

void patch::rect(int ox, int oy, int x1, int y1, int x2, int y2)
{
  if(x2 < ox || x1 >= ox + PATCH_SX)
    return;
  if(y2 < oy || y1 >= oy + PATCH_SY)
    return;
  if(x1 < ox)
    x1 = ox;
  if(y1 < oy)
    y1 = oy;
  if(x2 >= ox + PATCH_SX)
    x2 = ox + PATCH_SX - 1;
  if(y2 >= oy + PATCH_SX)
    y2 = oy + PATCH_SX - 1;
  int sx = x2-x1+1;
  unsigned char *p = base(ox, oy, x1, y1);
  for(int y=y1; y <= y2; y++) {
    memset(p, 0, sx);
    p += PATCH_SX + 1;
  }
}

void patch::invert(int ox, int oy, int x, int y)
{
  static const int pt_1[] = { 0, 4, 1, 4, 2, 3, 3, 2, 4, 1, 4, 0, 4, -1, 3, -2, 2, -3, 1, -4, 0, -4, -1, -4, -2, -3, -3, -2, -4, -1, -4, 0, -4, 1, -3, 2, -2, 3, -1, 4 };
  const int *ptlist = pt_1;
  int ptcount = sizeof(pt_1)/2/sizeof(int);
  if(x < ox+4 || x >= ox + PATCH_SX - 4)
    return;
  if(y < oy+4 || y >= oy + PATCH_SY - 4)
    return;

  for(int i=0; i != ptcount; i++) {
    int xx = x + ptlist[2*i];
    int yy = y + ptlist[2*i+1];
    if(xx < ox || xx >= ox + PATCH_SX)
      continue;
    if(yy < oy || yy >= oy + PATCH_SY)
      continue;
    *base(ox, oy, xx, yy) = 0;
  }
}

void patch::line(int ox, int oy, int x1, int y1, int x2, int y2)
{
  if(x1 == x2) {
    if(y1 < y2)
      vline(ox, oy, x1, y1, y2);
    else
      vline(ox, oy, x1, y2, y1);
    return;
  }
  if(y1 == y2) {
    if(x1 < x2)
      hline(ox, oy, x1, x2, y1);
    else
      hline(ox, oy, x2, x1, y1);
    return;
  }
  if(x1 < ox && x2 < ox)
    return;
  if(y1 < oy && y2 < oy)
    return;
  if(x1 >= ox + PATCH_SX && x2 >= ox + PATCH_SX)
    return;
  if(y1 >= oy + PATCH_SY && y2 >= oy + PATCH_SY)
    return;

  if(y2 < y1) {
    int t = y2;
    y2 = y1;
    y1 = t;
    t = x2;
    x2 = x1;
    x1 = t;
  }

  if(x2 > x1) {
    unsigned long dx = (((unsigned long)(x2-x1)) << 24) / (y2 - y1);
    unsigned char *p;
    unsigned long cpx;
    unsigned int px;
    if(y1 < oy) {
      cpx = (((unsigned long)x1) << 24) + 0x800000;
      cpx += dx*((oy-y1)*2-1)/2;
      px = (cpx - 0x7fffff) >> 24;
      p = data+1;
      y1 = 0;
    } else {
      cpx = (((unsigned long)x1) << 24) + 0x800000;
      cpx -= dx/2;
      px = x1;
      p = data+1 + (PATCH_SX+1)*(y1-oy);
      y1 -= oy;
    }
    y2 -= oy;
    if(y2 > PATCH_SY)
      y2 = PATCH_SY;

    for(int y=y1; y != y2; y++) {
      cpx += dx;
      unsigned int nx = (cpx - 0x7fffff) >> 24;
      if(px > nx)
	px = nx;
      int xx1 = px - ox;
      int xx2 = nx - ox;
      if(xx1 >= PATCH_SX)
	return;
      if(xx2 >= 0) {
	if(xx1 < 0)
	  xx1 = 0;
	if(xx2 >= PATCH_SX)
	  xx2 = PATCH_SX-1;
	memset(p + xx1, 0, xx2-xx1+1);
      }
      px = nx+1;
      p += PATCH_SX+1;
    }
    if(y2 == PATCH_SY)
      return;
    cpx += dx/2;
    unsigned int nx = (cpx - 0x7fffff) >> 24;
    if(px > nx)
      px = nx;
    int xx1 = px - ox;
    int xx2 = nx - ox;
    if(xx1 >= PATCH_SX)
      return;
    if(xx2 < 0)
      return;
    if(xx1 < 0)
      xx1 = 0;
    if(xx2 >= PATCH_SX)
      xx2 = PATCH_SX-1;
    memset(p + xx1, 0, xx2-xx1+1);

  } else {
    unsigned long dx = (((unsigned long)(x1-x2)) << 24) / (y2 - y1);
    unsigned char *p;
    unsigned long cpx;
    unsigned int px;
    if(y1 < oy) {
      cpx = (((unsigned long)x1) << 24) + 0x800000;
      cpx -= dx*((oy-y1)*2-1)/2;
      px = (cpx + 0x800000) >> 24;
      p = data+1;
      y1 = 0;
    } else {
      cpx = (((unsigned long)x1) << 24) + 0x800000;
      cpx += dx/2;
      px = x1;
      p = data+1 + (PATCH_SX+1)*(y1-oy);
      y1 -= oy;
    }
    y2 -= oy;
    if(y2 > PATCH_SY)
      y2 = PATCH_SY;

    for(int y=y1; y != y2; y++) {
      cpx -= dx;
      unsigned int nx = (cpx + 0x800000) >> 24;
      if(px < nx)
	px = nx;
      int xx1 = nx - ox;
      int xx2 = px - ox;
      if(xx2 < 0)
	return;
      if(xx1 < PATCH_SX) {
	if(xx1 < 0)
	  xx1 = 0;
	if(xx2 >= PATCH_SX)
	  xx2 = PATCH_SX-1;
	memset(p + xx1, 0, xx2-xx1+1);
      }
      px = nx-1;
      p += PATCH_SX+1;
    }
    if(y2 == PATCH_SY)
      return;
    cpx -= dx/2;
    unsigned int nx = (cpx + 0x800000) >> 24;
    if(px < nx)
      px = nx;
    int xx1 = nx - ox;
    int xx2 = px - ox;
    if(xx2 < 0)
      return;
    if(xx1 >= PATCH_SX)
      return;
    if(xx1 < 0)
      xx1 = 0;
    if(xx2 >= PATCH_SX)
      xx2 = PATCH_SX-1;
    memset(p + xx1, 0, xx2-xx1+1);

  }
}

void patch::mipmap(const patch &p1, int dx, int dy)
{
  unsigned char *q = data + 1 + (dx ? PATCH_SX/2 : 0) + (dy ? (PATCH_SX+1)*PATCH_SY/2 : 0);
  const unsigned char *p = p1.data + 1;
  for(int y=0; y != PATCH_SY/2; y++) {
    unsigned char *q1 = q;
    const unsigned char *p1 = p;
    for(int x=0; x != PATCH_SX/2; x++) {
      int v = p1[0] + p1[1] + p1[PATCH_SX+1] + p1[PATCH_SX+2] + 2;
      v = v*v/4096;
      *q1 = v;
      q1 ++;
      p1 += 2;
    }
    q += PATCH_SX+1;
    p += 2*(PATCH_SX+1);
  }
}

void patch::bitmap_blend(int ox, int oy, const unsigned char *src, int src_w, int src_h, int x, int y)
{
  x -= ox;
  y -= oy;
  int w = (x+src_w > PATCH_SX ? PATCH_SX : x+src_w) - (x < 0 ? 0 : x);
  int h = (y+src_h > PATCH_SY ? PATCH_SY : y+src_h) - (y < 0 ? 0 : y);
  unsigned char *dest = data+1;
  if(x > 0)
    dest += x;
  else
    src -= x;
  if(y > 0)
    dest += (PATCH_SX+1)*y;
  else
    src -= src_w*y;
  for(int yy=0; yy<h; yy++) {
    unsigned char *d1 = dest;
    const unsigned char *s1 = src;
    for(int xx=0; xx<w; xx++) {
      if(*d1 > *s1)
	*d1 = *s1;
      s1++;
      d1++;
    }
    dest += PATCH_SX+1;
    src += src_w;
  }
}

struct point {
  int x, y;

  point() {}
  point(int _x, int _y) { x = _x; y = _y; }
};

bool in_limit(point p)
{
  //  return p.x < 6000 / RATIO && p.y < 4000 / RATIO;
  //  return p.x > 4500;
  //  return p.x < 2000 && ((state->info.sy - 1)/RATIO-p.y) > 3000;
  return true;
}

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


class net;

class lt {
public:
  virtual ~lt();
  void wrap(lua_State *L);
  virtual const char *type_name() const = 0;
  static lt *checkparam(lua_State *L, int idx, const char *tname);
  static lt *getparam(lua_State *L, int idx, const char *tname);
  static lt *getparam_any(lua_State *L, int idx);
  static void make_metatable(lua_State *L, const luaL_Reg *table, const char *tname);
};

class node : public lt {
public:
  enum { NORMAL, GND, VCC };
  point pos;
  vector<net *> nets;
  vector<int> nettypes;
  string oname;

  virtual ~node();
  virtual point get_pos(int pin) const = 0;
  virtual void refine_position() = 0;
  virtual void to_svg(FILE *fd) const = 0;
  virtual void to_txt(FILE *fd) const = 0;
  virtual void draw(patch &p, int ox, int oy) const = 0;
  void build_power_nodes_and_nets(vector<node *> &nodes, vector<net *> &nets);
  void add_net(int pin, net *n);
  int get_nettype(int nid) const;
  point net_get_center(int pin) const;
  void move(int x, int y);
  virtual void set_orientation(char orient, net *source) = 0;
  virtual void set_subtype(string subtype);

  static int l_pos(lua_State *L);
  static int l_move(lua_State *L);
  static int l_t1(lua_State *L);
  static int l_t2(lua_State *L);
  static int l_gate(lua_State *L);
  static int l_set_name(lua_State *L);
};

class mosfet : public node {
public:
  static const char *type_name_s;

  int orientation;
  int trans;
  int ttype;
  double f;

  mosfet(int trans, double f, int ttype);
  virtual ~mosfet();
  point get_pos(int pin) const;
  void refine_position();
  void to_svg(FILE *fd) const;
  void to_txt(FILE *fd) const;
  virtual void draw(patch &p, int ox, int oy) const;
  void set_orientation(char orient, net *source);
  virtual void set_subtype(string subtype);
  static mosfet *checkparam(lua_State *L, int idx);
  static mosfet *getparam(lua_State *L, int idx);
  virtual const char *type_name() const;
  static int luaopen(lua_State *L);
  static int l_type(lua_State *L);
  static int l_tostring(lua_State *L);
  static int l_ttype(lua_State *L);
  static int l_set_ttype(lua_State *L);
};

class capacitor : public node {
public:
  static const char *type_name_s;

  int circ;
  int orientation;
  double f;

  capacitor(int circ, double f);
  virtual ~capacitor();
  point get_pos(int pin) const;
  void refine_position();
  void to_svg(FILE *fd) const;
  void to_txt(FILE *fd) const;
  virtual void draw(patch &p, int ox, int oy) const;
  void set_orientation(char orient, net *source);
  static capacitor *checkparam(lua_State *L, int idx);
  static capacitor *getparam(lua_State *L, int idx);
  virtual const char *type_name() const;
  static int luaopen(lua_State *L);
  static int l_type(lua_State *L);
  static int l_tostring(lua_State *L);
};

class power_node : public node {
public:
  static const char *type_name_s;

  bool is_vcc;

  power_node(bool is_vcc, point start_pos);
  virtual ~power_node();
  point get_pos(int pin) const;
  void refine_position();
  void to_svg(FILE *fd) const;
  void to_txt(FILE *fd) const;
  virtual void draw(patch &p, int ox, int oy) const;
  void set_orientation(char orient, net *source);
  static power_node *checkparam(lua_State *L, int idx);
  static power_node *getparam(lua_State *L, int idx);
  virtual const char *type_name() const;
  static int luaopen(lua_State *L);
  static int l_type(lua_State *L);
  static int l_tostring(lua_State *L);
};
  
class pad : public node {
public:
  static const char *type_name_s;

  string name;
  int orientation;

  int name_width, name_height;
  unsigned char *name_image;

  pad(string name, point pos, int orientation);
  virtual ~pad();
  point get_pos(int pin) const;
  void refine_position();
  void to_svg(FILE *fd) const;
  void to_txt(FILE *fd) const;
  virtual void draw(patch &p, int ox, int oy) const;
  void set_orientation(char orient, net *source);
  static pad *checkparam(lua_State *L, int idx);
  static pad *getparam(lua_State *L, int idx);
  virtual const char *type_name() const;
  static int luaopen(lua_State *L);
  static int l_type(lua_State *L);
  static int l_tostring(lua_State *L);
};

struct ref {
  node *n;
  int pin;
  ref(node *_n, int _pin) { n = _n; pin = _pin; }
};

class net : public lt {
public:
  static const char *type_name_s;

  int id, nid;
  vector<ref> nodes;
  vector<point> routes;
  list<pair<int, int> > draw_order;
  string oname;

  net(int id, int nid, bool is_vcc);
  virtual ~net();
  void add_node(ref n);
  void add_route(point p);

  point get_center() const;
  point get_closest(const node *nref, point p) const;
  void add_link_keys(int nid, vector<unsigned long> &link_keys) const;
  void handle_key(uint_least64_t k);
  void to_svg(FILE *fd) const;
  void to_txt(FILE *fd) const;
  void draw(patch &p, int ox, int oy) const;
  static net *checkparam(lua_State *L, int idx);
  static net *getparam(lua_State *L, int idx);
  virtual const char *type_name() const;
  static int luaopen(lua_State *L);
  static int l_type(lua_State *L);
  static int l_tostring(lua_State *L);
  static int l_route(lua_State *L);
  static int l_set_name(lua_State *L);
};

lt::~lt()
{
}

void lt::wrap(lua_State *L)
{
  char name[64];
  sprintf(name, "lt.%p", this);
  lua_getfield(L, LUA_REGISTRYINDEX, name);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lt **p = static_cast<lt **>(lua_newuserdata(L, sizeof(lt *)));
    *p = this;
    luaL_getmetatable(L, type_name());
    lua_setmetatable(L, -2);
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, name);
  }
}

lt *lt::checkparam(lua_State *L, int idx, const char *tname)
{
  const char *name;

  if(!lua_getmetatable(L, idx))
    return 0;

  lua_rawget(L, LUA_REGISTRYINDEX);
  name = lua_tostring(L, -1);

  if(!name || (tname && strcmp(name, tname))) {
    lua_pop(L, 1);
    return 0;
  }
  lua_pop(L, 1);

  return *static_cast<lt **>(lua_touserdata(L, idx));
}

lt *lt::getparam(lua_State *L, int idx, const char *tname)
{
  lt *p = checkparam(L, idx, tname);
  if(!p) {
    char msg[256];
    sprintf(msg, "%s expected", tname ? tname : "mschem type");
    luaL_argcheck(L, p, idx, msg);
  }
  return p;
}

lt *lt::getparam_any(lua_State *L, int idx)
{
  lt **l = static_cast<lt **>(lua_touserdata(L, idx));
  if(!l)
    luaL_error(L, "not a lt at index %d\n", idx);

  return *l;
}

void lt::make_metatable(lua_State *L, const luaL_Reg *table, const char *tname)
{
  luaL_newmetatable(L, tname);
  lua_pushvalue(L, -1);
  lua_pushstring(L, tname);
  lua_rawset(L, LUA_REGISTRYINDEX);
  lua_pushstring(L, "__index");
  lua_pushvalue(L, -2);
  lua_settable(L, -3);
  luaL_setfuncs(L, table, 0);
  lua_pop(L, 1);
}

double cdistance(point p1, point p2)
{
  double dx = p2.x - p1.x;
  double dy = p2.y - p1.y;
  return sqrt(dx*dx + dy*dy);
}

FILE *svg_open(const char *fname, int width, int height)
{
  char msg[4096];
  sprintf(msg, "Error opening %s for writing", fname);
  FILE *fd = fopen(fname, "w");
  if(!fd) {
    perror(msg);
    exit(1);
  }

  fprintf(fd, "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"no\"?>\n");
  fprintf(fd, "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"%d\" height=\"%d\" id=\"svg1\"\n"
	  "style=\"fill:none;stroke:#000000;stroke-width:1px;stroke-linecap:butt;stroke-linejoin:miter;stroke-opacity:1;fill-opacity:1;font-style:normal;font-variant:normal;font-weight:normal;font-stretch:normal;text-align:center;line-height:125%%;letter-spacing:0px;word-spacing:0px;writing-mode:lr-tb;font-family:Times New Roman\">\n",
          width, height);
  return fd;
}

void svg_text(FILE *fd, point p, string text, int px, int size)
{
  static const char *anchor[3] = { "start", "middle", "end" };
  fprintf(fd, "  <text xml:space=\"preserve\" style=\"font-size:%dpx;text-anchor:%s;fill:#000000;stroke:none\" x=\"%d\" y=\"%d\">\n", size*10, anchor[px+1], p.x*10, p.y*10);
  fprintf(fd, "    <tspan x=\"%d\" y=\"%d\">%s</tspan>\n", p.x*10, p.y*10, text.c_str());
  fprintf(fd, "  </text>\n");	  
}

void svg_close(FILE *fd)
{
  fprintf(fd, "</svg>\n");
  fclose(fd);
}

node::~node()
{
}

void node::set_subtype(string subtype)
{
  abort();
}

point node::net_get_center(int pin) const
{
  return nets[pin]->get_closest(this, get_pos(pin));
}

void node::add_net(int pin, net *n)
{
  nets[pin] = n;
}

int node::get_nettype(int nid) const
{
  return nid == state->vcc ? VCC : nid == state->gnd ? GND : NORMAL;
}

void node::build_power_nodes_and_nets(vector<node *> &nodes, vector<net *> &_nets)
{
  net *vcc_net = NULL, *gnd_net = NULL;
  for(unsigned int i=0; i != nets.size(); i++)
    if(nettypes[i] != NORMAL) {
      net *n = NULL;
      switch(nettypes[i]) {
      case VCC:
	if(!vcc_net) {
	  point p = pos;
	  p.y = 0;
	  power_node *vcc = new power_node(true, p);
	  vcc_net = new net(-1, _nets.size(), true);
	  nodes.push_back(vcc);
	  _nets.push_back(vcc_net);
	  vcc->add_net(T1, vcc_net);
	  vcc_net->add_node(ref(vcc, T1));
	}
	n = vcc_net;
	break;

      case GND:
	if(!gnd_net) {
	  point p = pos;
	  p.y = (state->info.sy-1) / ratio;
	  power_node *gnd = new power_node(false, p);
	  gnd_net = new net(-1, _nets.size(), false);
	  nodes.push_back(gnd);
	  _nets.push_back(gnd_net);
	  gnd->add_net(T1, gnd_net);
	  gnd_net->add_node(ref(gnd, T1));
	}
	n = gnd_net;
	break;

      default:
	abort();
      }
      n->add_node(ref(this, i));
      nets[i] = n;
    }
}

void node::move(int x, int y)
{
  pos.x = x;
  pos.y = y;
}

int node::l_pos(lua_State *L)
{
  node *n = static_cast<node *>(getparam_any(L, 1));
  lua_pushinteger(L, n->pos.x);
  lua_pushinteger(L, sy1 - n->pos.y);
  return 2;
}

int node::l_move(lua_State *L)
{
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "x expected");
  luaL_argcheck(L, lua_isnumber(L, 3), 3, "y expected");
  luaL_argcheck(L, lua_gettop(L) < 4 || lua_isnil(L, 4) || lua_isstring(L, 4), 4, "orientation expected");
  luaL_argcheck(L, lua_gettop(L) < 5 || lua_isnil(L, 5) || lua_isuserdata(L, 5), 5, "orientation node expected");
  node *n = static_cast<node *>(getparam_any(L, 1));
  n->move(lua_tonumber(L, 2), ((state->info.sy - 1)/ratio) - lua_tonumber(L, 3));
  net *nref = lua_gettop(L) >= 5 && !lua_isnil(L, 5) ? net::getparam(L, 5) : NULL;
  if(lua_gettop(L) >= 4 && lua_isstring(L, 4))
    n->set_orientation(lua_tostring(L, 4)[0], nref);
  return 0;
}

int node::l_t1(lua_State *L)
{
  node *n = static_cast<node *>(getparam_any(L, 1));
  n->nets[T1]->wrap(L);
  return 1;
}

int node::l_t2(lua_State *L)
{
  node *n = static_cast<node *>(getparam_any(L, 1));
  n->nets[T2]->wrap(L);
  return 1;
}

int node::l_gate(lua_State *L)
{
  node *n = static_cast<node *>(getparam_any(L, 1));
  n->nets[GATE]->wrap(L);
  return 1;
}

int node::l_set_name(lua_State *L)
{
  node *n = static_cast<node *>(getparam_any(L, 1));
  luaL_argcheck(L, lua_isstring(L, 2), 2, "name expected");
  n->oname = lua_tostring(L, 2);
  return 0;
}

const char *power_node::type_name_s = "m.power_node";

const char *power_node::type_name() const
{
  return type_name_s;
}

power_node *power_node::checkparam(lua_State *L, int idx)
{
  return static_cast<power_node *>(lt::checkparam(L, idx, type_name_s));
}

power_node *power_node::getparam(lua_State *L, int idx)
{
  return static_cast<power_node *>(lt::getparam(L, idx, type_name_s));
}

int power_node::l_type(lua_State *L)
{
  lua_pushstring(L, "w");
  return 1;
}

void power_node::set_orientation(char orient, net *source)
{
  abort();
}

int power_node::luaopen(lua_State *L)
{
  static const luaL_Reg m[] = {
    { "__tostring", l_tostring },
    { "pos",        l_pos      },
    { "move",       l_move     },
    { "type",       l_type     },
    { "t1",         l_t1       },
    { "set_name",   l_set_name },
    { }
  };

  make_metatable(L, m, type_name_s);
  return 1;
}

int power_node::l_tostring(lua_State *L)
{
  power_node *pn = getparam(L, 1);
  lua_pushstring(L, pn->is_vcc ? "vcc" : "gnd");
  return 1;
}

power_node::power_node(bool _is_vcc, point start_pos)
{
  is_vcc = _is_vcc;
  pos = start_pos;
  nets.resize(1);
  nettypes.resize(1);
  nettypes[0] = NORMAL;
  oname = is_vcc ? "vcc" : "gnd";
}

power_node::~power_node()
{
}

point power_node::get_pos(int pin) const
{
  return pos;
}

void power_node::refine_position()
{
  net *n = nets[0];
  bool has_pos = false;
  for(unsigned int i=0; i != n->nodes.size(); i++)
    if(n->nodes[i].n != this) {
      point p = n->nodes[i].n->get_pos(n->nodes[i].pin);
      if(!has_pos || (is_vcc ? p.y < pos.y : pos.y < p.y)) {
	has_pos = true;
	pos = p;
      }
    }
}

void power_node::to_svg(FILE *fd) const
{
  if(!in_limit(pos))
    return;

  fprintf(fd, "  <g>\n");
  if(is_vcc) {
    fprintf(fd, "    <path d=\"m %d %d 0,-10\" />\n", pos.x*10, pos.y*10);
    fprintf(fd, "    <rect style=\"fill:#000000\" width=\"20\" height=\"5\" x=\"%d\" y=\"%d\" />\n", pos.x*10-10, pos.y*10-15);
  } else {
    fprintf(fd, "    <path d=\"m %d %d 0,10\" />\n", pos.x*10, pos.y*10);
    fprintf(fd, "    <path d=\"m %d %d 20,0\" />\n", pos.x*10-10, pos.y*10+10);
    fprintf(fd, "    <path d=\"m %d %d 14,0\" />\n", pos.x*10-7, pos.y*10+12);
    fprintf(fd, "    <path d=\"m %d %d 8,0\" />\n", pos.x*10-4, pos.y*10+14);
    fprintf(fd, "    <path d=\"m %d %d 2,0\" />\n", pos.x*10-1, pos.y*10+16);
  }
  fprintf(fd, "  </g>\n");
}

void power_node::to_txt(FILE *fd) const
{
  fprintf(fd, "%c %d %d %d %s\n", is_vcc ? 'v' : 'g', pos.x, sy1-pos.y, nets[0]->nid, oname.c_str());
}

void power_node::draw(patch &p, int ox, int oy) const
{
  int bx = pos.x*10;
  int by = pos.y*10;
  if(bx+10 < ox || bx-10 >= ox+PATCH_SX || by+16 < oy || by-15 >= oy+PATCH_SY)
    return;
  if(is_vcc) {
    p.vline(ox, oy, bx, by-10, by);
    p.rect(ox, oy, bx-10, by-15, bx+10, by-10);
  } else {
    p.vline(ox, oy, bx, by, by+10);
    p.hline(ox, oy, bx-10, bx+10, by+10);
    p.hline(ox, oy, bx-7, bx+7, by+12);
    p.hline(ox, oy, bx-4, bx+4, by+14);
    p.hline(ox, oy, bx-1, bx+1, by+16);
  }
}

const char *pad::type_name_s = "m.pad";

const char *pad::type_name() const
{
  return type_name_s;
}

pad *pad::checkparam(lua_State *L, int idx)
{
  return static_cast<pad *>(lt::checkparam(L, idx, type_name_s));
}

pad *pad::getparam(lua_State *L, int idx)
{
  return static_cast<pad *>(lt::getparam(L, idx, type_name_s));
}

int pad::l_type(lua_State *L)
{
  lua_pushstring(L, "p");
  return 1;
}

int pad::l_tostring(lua_State *L)
{
  pad *pn = getparam(L, 1);
  lua_pushstring(L, ("pad:" + pn->name).c_str());
  return 1;
}

void pad::set_orientation(char orient, net *source)
{
  if(orient == 'n')
    orientation = N_S;
  else if(orient == 's')
    orientation = S_S;
  else if(orient == 'e')
    orientation = E_S;
  else if(orient == 'w')
    orientation = W_S;
  else
    abort();  
}

int pad::luaopen(lua_State *L)
{
  static const luaL_Reg m[] = {
    { "__tostring", l_tostring },
    { "pos",        l_pos      },
    { "move",       l_move     },
    { "type",       l_type     },
    { "t1",         l_t1       },
    { "set_name",   l_set_name },
    { }
  };

  make_metatable(L, m, type_name_s);
  return 1;
}

pad::pad(string _name, point _pos, int _orientation)
{
  pos = _pos;
  name = _name;
  orientation = _orientation;
  nets.resize(1);
  nettypes.resize(1);
  nettypes[0] = NORMAL;
  oname = name;

  freetype_render(name.c_str(), 6, 0, name_width, name_height, name_image);
}

pad::~pad()
{
}

point pad::get_pos(int pin) const
{
  return pos;
}

void pad::refine_position()
{
}

void pad::to_svg(FILE *fd) const
{
  if(!in_limit(pos))
    return;

  fprintf(fd, "  <g id=\"pad=%s\"><desc>%s</desc>\n", name.c_str(), name.c_str());
  point pt = pos;
  switch(orientation & 3) {
  case W_S:
    fprintf(fd, "    <path d=\"m %d %d -40,0 0,100 -200,0 0,-200 200,0 0,100\" />\n", pos.x*10, pos.y*10);
    pt.x -= 14;
    break;
  case E_S:
    fprintf(fd, "    <path d=\"m %d %d 40,0 0,100 200,0 0,-200 -200,0 0,100\" />\n", pos.x*10, pos.y*10);
    pt.x += 14;
    break;
  case N_S:
    fprintf(fd, "    <path d=\"m %d %d 0,-40 100,0 0,-200 -200,0 0,200 100,0\" />\n", pos.x*10, pos.y*10);
    pt.y -= 14;
    break;
  case S_S:
    fprintf(fd, "    <path d=\"m %d %d 0,40 100,0 0,200 -200,0 0,-200 100,0\" />\n", pos.x*10, pos.y*10);
    pt.y += 14;
    break;
  }
  pt.y += 3;
  svg_text(fd, pt, name, 0, 9);
  fprintf(fd, "  </g>\n");
}

void pad::to_txt(FILE *fd) const
{
  fprintf(fd, "p %d %d %d %d %s\n", pos.x, sy1-pos.y, nets[0]->nid, orientation, oname.c_str());
}

void pad::draw(patch &p, int ox, int oy) const
{
  int bx = pos.x*10;
  int by = pos.y*10;
  if(bx+240 < ox || bx-240 >= ox+PATCH_SX || by+240 < oy || by-240 >= oy+PATCH_SY)
    return;

  int tx = bx, ty = by;
  switch(orientation & 3) {
  case W_S:
    p.hline(ox, oy, bx-40, bx, by);
    p.hline(ox, oy, bx-240, bx-40, by-100);
    p.hline(ox, oy, bx-240, bx-40, by+100);
    p.vline(ox, oy, bx-40, by-100, by+100);
    p.vline(ox, oy, bx-240, by-100, by+100);
    tx = bx-140;
    ty = by;
    break;
  case E_S:
    p.hline(ox, oy, bx, bx+40, by);
    p.hline(ox, oy, bx+40, bx+240, by-100);
    p.hline(ox, oy, bx+40, bx+240, by+100);
    p.vline(ox, oy, bx+40, by-100, by+100);
    p.vline(ox, oy, bx+240, by-100, by+100);
    tx = bx+140;
    ty = by;
    break;
  case N_S:
    p.vline(ox, oy, bx, by-40, by);
    p.vline(ox, oy, bx-100, by-240, by-40);
    p.vline(ox, oy, bx+100, by-240, by-40);
    p.hline(ox, oy, bx-100, bx+100, by-40);
    p.hline(ox, oy, bx-100, bx+100, by-240);
    tx = bx;
    ty = by-140;
    break;
  case S_S:
    p.vline(ox, oy, bx, by, by+40);
    p.vline(ox, oy, bx-100, by+40, by+240);
    p.vline(ox, oy, bx+100, by+40, by+240);
    p.hline(ox, oy, bx-100, bx+100, by+40);
    p.hline(ox, oy, bx-100, bx+100, by+240);
    tx = bx;
    ty = by+140;
    break;
  }

  p.bitmap_blend(ox, oy, name_image, name_width, name_height, tx-name_width/2, ty-name_height/2);
}

const char *mosfet::type_name_s = "m.mosfet";

const char *mosfet::type_name() const
{
  return type_name_s;
}

mosfet *mosfet::checkparam(lua_State *L, int idx)
{
  return static_cast<mosfet *>(lt::checkparam(L, idx, type_name_s));
}

mosfet *mosfet::getparam(lua_State *L, int idx)
{
  return static_cast<mosfet *>(lt::getparam(L, idx, type_name_s));
}

int mosfet::l_type(lua_State *L)
{
  lua_pushstring(L, "t");
  return 1;
}

int mosfet::l_tostring(lua_State *L)
{
  static const char ttype_string[] = "tid";
  mosfet *mn = getparam(L, 1);
  const tinfo &ti = state->info.trans[mn->trans];
  char buf[4096];
  sprintf(buf, "%c%d(%s, %s, %s, %f)", ttype_string[mn->ttype], mn->trans, state->ninfo.net_name(ti.t1).c_str(), state->ninfo.net_name(ti.gate).c_str(), state->ninfo.net_name(ti.t2).c_str(), mn->f);
  lua_pushstring(L, buf);
  return 1;
}

void mosfet::set_orientation(char orient, net *source)
{
  if(orient == 'n')
    orientation = N_S;
  else if(orient == 's')
    orientation = S_S;
  else if(orient == 'e')
    orientation = E_S;
  else if(orient == 'w')
    orientation = W_S;
  else
    abort();
  if(source == nets[T2])
    orientation |= 4;
  else {
    assert(source == nets[T1] || !source);
  }
}

void mosfet::set_subtype(string subtype)
{
  if(subtype == "t")
    ttype = State::T_NMOS;
  else if(subtype == "d")
    ttype = State::T_NDEPL;
  else if(subtype == "i")
    ttype = State::T_PMOS;
  else
    abort();
}

int mosfet::l_ttype(lua_State *L)
{
  static const char *const ttype_string[] = { "t", "i", "d" };
  mosfet *m = getparam(L, 1);
  lua_pushstring(L, ttype_string[m->ttype]);
  return 1;
}

int mosfet::l_set_ttype(lua_State *L)
{
  mosfet *m = getparam(L, 1);
  const char *ttype = lua_tostring(L, 2);
  m->ttype = ttype[0] == 'i' ? State::T_PMOS : ttype[0] == 'd' ? State::T_NDEPL : State::T_NMOS;
  return 0;
}

int mosfet::luaopen(lua_State *L)
{
  static const luaL_Reg m[] = {
    { "__tostring",     l_tostring      },
    { "pos",            l_pos           },
    { "move",           l_move          },
    { "type",           l_type          },
    { "ttype",          l_ttype         },
    { "t1",             l_t1            },
    { "t2",             l_t2            },
    { "gate",           l_gate          },
    { "set_name",       l_set_name      },
    { "set_ttype",      l_set_ttype     },
    { }
  };

  make_metatable(L, m, type_name_s);
  return 1;
}

mosfet::mosfet(int _trans, double _f, int _ttype)
{
  trans = _trans;
  f = _f;
  ttype = _ttype;
  orientation = W_S;
  const tinfo &ti = state->info.trans[trans];
  pos.x = ti.x / ratio;
  pos.y = ((state->info.sy - 1) - ti.y) / ratio;
  char nn[32];
  sprintf(nn, "t%d", trans);
  oname = nn;
  nets.resize(3);
  nettypes.resize(3);
  nettypes[T1] = get_nettype(ti.t1);
  nettypes[T2] = get_nettype(ti.t2);
  nettypes[GATE] = get_nettype(ti.gate);
}

mosfet::~mosfet()
{
}

double mind(double a, double b)
{
  return a<b ? a : b;
}

void mosfet::refine_position()
{
  bool v = false;
  const tinfo &ti = state->info.trans[trans];
  point pt1 = net_get_center(T1);
  point pt2 = net_get_center(T2);
  point pg = net_get_center(GATE);

  if(v)
    printf("(%d, %d) (%d, %d) (%d, %d)\n", pt1.x, pt1.y, pg.x, pg.y, pt2.x, pt2.y);
  int best_orientation = -1;
  double best_dist = 0;
  for(orientation = 0; orientation < 8; orientation++) {
    if(v)
      printf("%d : (%d, %d) (%d, %d) (%d, %d)\n", orientation, get_pos(T1).x, get_pos(T1).y, get_pos(GATE).x, get_pos(GATE).y, get_pos(T2).x, get_pos(T2).y);
    double dist_t1 = cdistance(pt1, get_pos(T1));
    double dist_t2 = cdistance(pt2, get_pos(T2));
    double dist_g = cdistance(pg, get_pos(GATE));
    double dist;
    if(ti.t1 == ti.gate)
      dist = mind(dist_t1, dist_g) + dist_t2;
    else if(ti.t2 == ti.gate)
      dist = mind(dist_t2, dist_g) + dist_t1;
    else
      dist = dist_t1 + dist_g + dist_t2;
    if(v)
      printf("    %f %f %f -> %f\n", dist_t1, dist_g, dist_t2, dist);
    if(best_orientation == -1 || dist < best_dist) {
      best_orientation = orientation;
      best_dist = dist;
    }
  }
  orientation = best_orientation;
  if(v)
    printf("   -> %d (%f)\n", orientation, best_dist);
}

point mosfet::get_pos(int pin) const
{
  point p;
  switch(orientation & 3) {
  case W_S:
  default:
    switch(pin ^ (orientation & 4 ? 1 : 0)) {
    case T1:
      p.x = pos.x+1;
      p.y = pos.y+4;
      break;
    case T2:
      p.x = pos.x+1;
      p.y = pos.y-4;
      break;
    case GATE:
    case GATE|1:
      p.x = pos.x-4;
      p.y = pos.y;
      break;
    default:
      abort();
    }
    break;
  case E_S:
    switch(pin ^ (orientation & 4 ? 1 : 0)) {
    case T1:
      p.x = pos.x-1;
      p.y = pos.y+4;
      break;
    case T2:
      p.x = pos.x-1;
      p.y = pos.y-4;
      break;
    case GATE:
    case GATE|1:
      p.x = pos.x+4;
      p.y = pos.y;
      break;
    default:
      abort();
    }
    break;
  case N_S:
    switch(pin ^ (orientation & 4 ? 1 : 0)) {
    case T1:
      p.x = pos.x-4;
      p.y = pos.y+1;
      break;
    case T2:
      p.x = pos.x+4;
      p.y = pos.y+1;
      break;
    case GATE:
    case GATE|1:
      p.x = pos.x;
      p.y = pos.y-4;
      break;
    default:
      abort();
    }
    break;
  case S_S:
    switch(pin ^ (orientation & 4 ? 1 : 0)) {
    case T1:
      p.x = pos.x-4;
      p.y = pos.y-1;
      break;
    case T2:
      p.x = pos.x+4;
      p.y = pos.y-1;
      break;
    case GATE:
    case GATE|1:
      p.x = pos.x;
      p.y = pos.y+4;
      break;
    default:
      abort();
    }
    break;
  }
  return p;
}

void mosfet::to_svg(FILE *fd) const
{
  if(!in_limit(pos))
    return;

  fprintf(fd, "  <g id=\"trans-%d\"><desc>%g</desc>\n", trans, f);
  switch(orientation & 3) {
  case W_S:
    fprintf(fd, "    <path d=\"m %d %d 0,20 -10,0 0,40 10,0 0,20\" />\n", pos.x*10+10, pos.y*10-40);
    if(ttype == State::T_PMOS)
      fprintf(fd, "    <path d=\"m %d %d c 0,1.656855 -1.343146,3 -3,3 -1.656854,0 -3,-1.343145 -3,-3 0,-1.656854 1.343146,-3 3,-3 1.656854,0 3,1.343146 3,3 z\" />\n", pos.x*10-10, pos.y*10);
    fprintf(fd, "    <path d=\"m %d %d %d,0\" />\n", pos.x*10-40, pos.y*10, ttype == State::T_PMOS ? 24 : 30);
    fprintf(fd, "    <path d=\"m %d %d 0,40\" />\n", pos.x*10-10, pos.y*10-20);
    if(ttype == State::T_NDEPL)
      fprintf(fd, "    <rect style=\"fill:#000000\" width=\"4\" height=\"40\" x=\"%d\" y=\"%d\" />\n", pos.x*10, pos.y*10-20);
    break;
  case E_S:
    fprintf(fd, "    <path d=\"m %d %d 0,20 10,0 0,40 -10,0 0,20\" />\n", pos.x*10-10, pos.y*10-40);
    if(ttype == State::T_PMOS)
      fprintf(fd, "    <path d=\"m %d %d c 0,1.656855 -1.343146,3 -3,3 -1.656854,0 -3,-1.343145 -3,-3 0,-1.656854 1.343146,-3 3,-3 1.656854,0 3,1.343146 3,3 z\" />\n", pos.x*10+16, pos.y*10);
    fprintf(fd, "    <path d=\"m %d %d %d,0\" />\n", pos.x*10+40, pos.y*10, ttype == State::T_PMOS ? -24 : -30);
    fprintf(fd, "    <path d=\"m %d %d 0,40\" />\n", pos.x*10+10, pos.y*10-20);
    if(ttype == State::T_NDEPL)
      fprintf(fd, "    <rect style=\"fill:#000000\" width=\"4\" height=\"40\" x=\"%d\" y=\"%d\" />\n", pos.x*10-4, pos.y*10-20);
    break;
  case N_S:
    fprintf(fd, "    <path d=\"m %d %d 20,0 0,-10 40,0 0,10 20,0\" />\n", pos.x*10-40, pos.y*10+10);
    if(ttype == State::T_PMOS)
      fprintf(fd, "    <path d=\"m %d %d c 0,1.656855 -1.343146,3 -3,3 -1.656854,0 -3,-1.343145 -3,-3 0,-1.656854 1.343146,-3 3,-3 1.656854,0 3,1.343146 3,3 z\" />\n", pos.x*10+3, pos.y*10-13);
    fprintf(fd, "    <path d=\"m %d %d 0,%d\" />\n", pos.x*10, pos.y*10-40, ttype == State::T_PMOS ? 24 : 30);
    fprintf(fd, "    <path d=\"m %d %d 40,0\" />\n", pos.x*10-20, pos.y*10-10);
    if(ttype == State::T_NDEPL)
      fprintf(fd, "    <rect style=\"fill:#000000\" width=\"40\" height=\"4\" x=\"%d\" y=\"%d\" />\n", pos.x*10-20, pos.y*10);
    break;
  case S_S:
    fprintf(fd, "    <path d=\"m %d %d 20,0 0,10 40,0 0,-10 20,0\" />\n", pos.x*10-40, pos.y*10-10);
    if(ttype == State::T_PMOS)
      fprintf(fd, "    <path d=\"m %d %d c 0,1.656855 -1.343146,3 -3,3 -1.656854,0 -3,-1.343145 -3,-3 0,-1.656854 1.343146,-3 3,-3 1.656854,0 3,1.343146 3,3 z\" />\n", pos.x*10+3, pos.y*10+13);
    fprintf(fd, "    <path d=\"m %d %d 0,%d\" />\n", pos.x*10, pos.y*10+40, ttype == State::T_PMOS ? -24 : -30);
    fprintf(fd, "    <path d=\"m %d %d 40,0\" />\n", pos.x*10-20, pos.y*10+10);
    if(ttype == State::T_NDEPL)
      fprintf(fd, "    <rect style=\"fill:#000000\" width=\"40\" height=\"4\" x=\"%d\" y=\"%d\" />\n", pos.x*10-20, pos.y*10-4);
    break;
  default:
    abort();
  }
  fprintf(fd, "  </g>\n");
}

void mosfet::to_txt(FILE *fd) const
{
  static const char ttype_string[] = "tid";
  fprintf(fd, "%c %d %d %d %d %d %g %d %s\n", ttype_string[ttype], pos.x, sy1-pos.y, nets[T1]->nid, nets[GATE]->nid, nets[T2]->nid, f, orientation, oname.c_str());
}

void mosfet::draw(patch &p, int ox, int oy) const
{
  int bx = pos.x*10;
  int by = pos.y*10;
  if(bx+40 < ox || bx-40 >= ox+PATCH_SX || by+40 < oy || by-40 >= oy+PATCH_SY)
    return;
  switch(orientation & 3) {
  case W_S:
    p.vline(ox, oy, bx+10, by-40, by-20);
    p.hline(ox, oy, bx, bx+10, by-20);
    p.vline(ox, oy, bx, by-20, by+20);
    p.hline(ox, oy, bx, bx+10, by+20);
    p.vline(ox, oy, bx+10, by+20, by+40);
    p.hline(ox, oy, bx-40, ttype == State::T_PMOS ? bx-19 : bx-10, by);
    p.vline(ox, oy, bx-10, by-20, by+20);
    if(ttype == State::T_PMOS)
      p.invert(ox, oy, bx-15, by);
    if(ttype == State::T_NDEPL)
      p.rect(ox, oy, bx, by-20, bx+4, by+20);
    break;

  case E_S:
    p.vline(ox, oy, bx-10, by-40, by-20);
    p.hline(ox, oy, bx-10, bx, by-20);
    p.vline(ox, oy, bx, by-20, by+20);
    p.hline(ox, oy, bx-10, bx, by+20);
    p.vline(ox, oy, bx-10, by+20, by+40);
    p.hline(ox, oy, ttype == State::T_PMOS ? bx+19 : bx+10, bx+40, by);
    p.vline(ox, oy, bx+10, by-20, by+20);
    if(ttype == State::T_PMOS)
      p.invert(ox, oy, bx+15, by);
    if(ttype == State::T_NDEPL)
      p.rect(ox, oy, bx-4, by-20, bx, by+20);
    break;

  case N_S:
    p.hline(ox, oy, bx-40, bx-20, by+10);
    p.vline(ox, oy, bx-20, by, by+10);
    p.hline(ox, oy, bx-20, bx+20, by);
    p.vline(ox, oy, bx+20, by, by+10);
    p.hline(ox, oy, bx+20, bx+40, by+10);
    p.vline(ox, oy, bx, by-40, ttype == State::T_PMOS ? by-19 : by-10);
    p.hline(ox, oy, bx-20, bx+20, by-10);
    if(ttype == State::T_PMOS)
      p.invert(ox, oy, bx, by-15);
    if(ttype == State::T_NDEPL)
      p.rect(ox, oy, bx-20, by, bx+20, by+4);
    break;

  case S_S:
    p.hline(ox, oy, bx-40, bx-20, by-10);
    p.vline(ox, oy, bx-20, by-10, by);
    p.hline(ox, oy, bx-20, bx+20, by);
    p.vline(ox, oy, bx+20, by-10, by);
    p.hline(ox, oy, bx+20, bx+40, by-10);
    p.vline(ox, oy, bx, ttype == State::T_PMOS ? by+19 : by+10, by+40);
    p.hline(ox, oy, bx-20, bx+20, by+10);
    if(ttype == State::T_PMOS)
      p.invert(ox, oy, bx, by+15);
    if(ttype == State::T_NDEPL)
      p.rect(ox, oy, bx-20, by-4, bx+20, by);
    break;
  }
}

const char *capacitor::type_name_s = "m.capacitor";

const char *capacitor::type_name() const
{
  return type_name_s;
}

capacitor *capacitor::checkparam(lua_State *L, int idx)
{
  return static_cast<capacitor *>(lt::checkparam(L, idx, type_name_s));
}

capacitor *capacitor::getparam(lua_State *L, int idx)
{
  return static_cast<capacitor *>(lt::getparam(L, idx, type_name_s));
}

int capacitor::l_type(lua_State *L)
{
  lua_pushstring(L, "c");
  return 1;
}

int capacitor::l_tostring(lua_State *L)
{
  capacitor *cn = getparam(L, 1);
  const cinfo &ci = state->info.circs[cn->circ];
  char buf[4096];
  sprintf(buf, "c%d(%s, %s, %d)", cn->circ, state->ninfo.net_name(ci.net).c_str(), state->ninfo.net_name(ci.netp).c_str(), ci.surface);
  lua_pushstring(L, buf);
  return 1;
}

void capacitor::set_orientation(char orient, net *source)
{
  if(orient == 'n')
    orientation = N_S;
  else if(orient == 'w')
    orientation = W_S;
  else
    abort();  
  if(source == nets[T2])
    orientation |= 4;
  else {
    assert(source == nets[T1] || !source);
  }
}

int capacitor::luaopen(lua_State *L)
{
  static const luaL_Reg m[] = {
    { "__tostring", l_tostring },
    { "pos",        l_pos      },
    { "move",       l_move     },
    { "type",       l_type     },
    { "t1",         l_t1       },
    { "t2",         l_t2       },
    { "set_name",   l_set_name },
    { }
  };

  make_metatable(L, m, type_name_s);
  return 1;
}

capacitor::capacitor(int _circ, double _f)
{
  circ = _circ;
  orientation = W_S;
  const cinfo &ci = state->info.circs[circ];
  pos.x = (ci.x0 + ci.x1 + 1)/2/ratio;
  pos.y = ((state->info.sy - 1) - (ci.y0 + ci.y1 + 1)/2) / ratio;

  f = _f;
  char nn[32];
  sprintf(nn, "c%d", circ);
  oname = nn;
  nets.resize(2);
  nettypes.resize(2);
  nettypes[T1] = get_nettype(ci.net);
  nettypes[T2] = get_nettype(ci.netp);
}

capacitor::~capacitor()
{
}

void capacitor::refine_position()
{
  point pt1 = net_get_center(T1);
  point pt2 = net_get_center(T2);

  int best_orientation = -1;
  double best_dist = 0;
  static const int ori[4] = { W_S, W_D, N_S, N_D };
  for(int io=0; io<4; io++) {
    orientation = ori[io];
    double dist_t1 = cdistance(pt1, get_pos(T1));
    double dist_t2 = cdistance(pt2, get_pos(T2));
    double dist;
    dist = dist_t1 + dist_t2;
    if(best_orientation == -1 || dist < best_dist) {
      best_orientation = orientation;
      best_dist = dist;
    }
  }
  orientation = best_orientation;
}

point capacitor::get_pos(int pin) const
{
  point p;
  switch(orientation & 3) {
  case W_S:
    switch(pin ^ (orientation & 4 ? 1 : 0)) {
    case T1:
      p.x = pos.x-1;
      p.y = pos.y;
      break;
    case T2:
      p.x = pos.x+1;
      p.y = pos.y;
      break;
    default:
      abort();
    }
    break;

  case N_S:
    switch(pin ^ (orientation & 4 ? 1 : 0)) {
    case T1:
      p.x = pos.x;
      p.y = pos.y+1;
      break;
    case T2:
      p.x = pos.x;
      p.y = pos.y-1;
      break;
    default:
      abort();
    }
    break;

  default:
    abort();
  }
  return p;
}

void capacitor::to_svg(FILE *fd) const
{
  if(!in_limit(pos))
    return;

  fprintf(fd, "  <g id=\"caps-%d\"><desc>%g</desc>\n", circ, f);
  switch(orientation & 3) {
  case W_S:
    fprintf(fd, "    <path d=\"m %d %d 0,20\" />\n", pos.x*10-2, pos.y*10-10);
    fprintf(fd, "    <path d=\"m %d %d 0,20\" />\n", pos.x*10+2, pos.y*10-10);
    fprintf(fd, "    <path d=\"m %d %d 8,0\" />\n", pos.x*10-10, pos.y*10);
    fprintf(fd, "    <path d=\"m %d %d 8,0\" />\n", pos.x*1+2, pos.y*10);
    break;
  case N_S:
    fprintf(fd, "    <path d=\"m %d %d 20,0\" />\n", pos.x*10-10, pos.y*10-2);
    fprintf(fd, "    <path d=\"m %d %d 20,0\" />\n", pos.x*10-10, pos.y*10+2);
    fprintf(fd, "    <path d=\"m %d %d 0,8\" />\n", pos.x*10, pos.y*10-10);
    fprintf(fd, "    <path d=\"m %d %d 0,8\" />\n", pos.x*10, pos.y*10+2);
    break;
  }
  fprintf(fd, "  </g>\n");
}

void capacitor::to_txt(FILE *fd) const
{
  fprintf(fd, "c %d %d %d %d %g %d %s\n", pos.x, sy1-pos.y, nets[T1]->nid, nets[T2]->nid, f, orientation, oname.c_str());
}

void capacitor::draw(patch &p, int ox, int oy) const
{
  int bx = pos.x*10;
  int by = pos.y*10;
  if(bx+10 < ox || bx-10 >= ox+PATCH_SX || by+10 < oy || by-10 >= oy+PATCH_SY)
    return;
  switch(orientation & 3) {
  case W_S:
    p.vline(ox, oy, bx-2, by-10, by+10);
    p.vline(ox, oy, bx+2, by-10, by+10);
    p.hline(ox, oy, bx-10, bx-2, by);
    p.hline(ox, oy, bx+2, bx+10, by);
    break;

  case N_S:
    p.hline(ox, oy, bx-10, bx+10, by-2);
    p.hline(ox, oy, bx-10, bx+10, by+2);
    p.vline(ox, oy, bx, by-10, by-2);
    p.vline(ox, oy, bx, by+2, by+10);
    break;
  }
}

const char *net::type_name_s = "m.net";

const char *net::type_name() const
{
  return type_name_s;
}

net *net::checkparam(lua_State *L, int idx)
{
  return static_cast<net *>(lt::checkparam(L, idx, type_name_s));
}

net *net::getparam(lua_State *L, int idx)
{
  return static_cast<net *>(lt::getparam(L, idx, type_name_s));
}

int net::l_type(lua_State *L)
{
  lua_pushstring(L, "n");
  return 1;
}

int net::l_tostring(lua_State *L)
{
  net *nn = getparam(L, 1);
  if(nn->id == -1)
    lua_pushstring(L, "powernet");
  else
    lua_pushstring(L, state->ninfo.net_name(nn->id).c_str());
  return 1;
}

int net::l_route(lua_State *L)
{
  net *n = getparam(L, 1);
  for(int i=2; i < lua_gettop(L); i+=2) {
    luaL_argcheck(L, lua_isnumber(L, i), i, "x expected");
    luaL_argcheck(L, lua_isnumber(L, i+1), i+1, "y expected");
    n->add_route(point(lua_tonumber(L, i), ((state->info.sy - 1)/ratio) - lua_tonumber(L, i+1)));
  }
  return 0;
}

int net::l_set_name(lua_State *L)
{
  net *n = getparam(L, 1);
  luaL_argcheck(L, lua_isstring(L, 2), 2, "name expected");
  n->oname = lua_tostring(L, 2);
  return 0;
}

int net::luaopen(lua_State *L)
{
  static const luaL_Reg m[] = {
    { "__tostring", l_tostring },
    { "type",       l_type     },
    { "route",      l_route    },
    { "set_name",   l_set_name },
    { }
  };

  make_metatable(L, m, type_name_s);
  return 1;
}

net::net(int _id, int _nid, bool is_vcc)
{
  id = _id;
  nid = _nid;
  if(id != -1)
    oname = state->ninfo.net_name(id);
  else
    oname = is_vcc ? "vcc" : "gnd";
}

net::~net()
{
}

void net::add_node(ref rid)
{
  nodes.push_back(rid);
}

void net::add_route(point p)
{
  routes.push_back(p);
}

point net::get_center() const
{
  point p;
  p.x = 0;
  p.y = 0;
  for(unsigned int i = 0; i != nodes.size(); i++) {
    point p1 = nodes[i].n->get_pos(nodes[i].pin);
    p.x += p1.x;
    p.y += p1.y;
  }
  int np = nodes.size();
  p.x /= np;
  p.y /= np;
  return p;
}

point net::get_closest(const node *nref, point ref) const
{
  point best_point;
  best_point.x = 0;
  best_point.y = 0;
  int best_dist = -1;
  for(unsigned int i = 0; i != nodes.size(); i++) {
    if(nodes[i].n == nref)
      continue;
    point p1 = nodes[i].n->get_pos(nodes[i].pin);
    int dist = abs(p1.x-ref.x) + abs(p1.y - ref.y);
    if(best_dist == -1 || dist < best_dist) {
      best_point = p1;
      best_dist = dist;
    }
  }
  for(unsigned int i = 0; i != routes.size(); i++) {
    int dist = abs(routes[i].x-ref.x) + abs(routes[i].y - ref.y);
    if(best_dist == -1 || dist < best_dist) {
      best_point = routes[i];
      best_dist = dist;
    }
  }
  return best_point;
}

void net::add_link_keys(int nid, vector<unsigned long> &link_keys) const
{
  int np = nodes.size() + routes.size();
  vector<point> pt;
  pt.resize(np);
  for(unsigned int i = 0; i != nodes.size(); i++)
    pt[i] = nodes[i].n->get_pos(nodes[i].pin);
  for(unsigned int i = 0; i != routes.size(); i++)
    pt[i+nodes.size()] = routes[i];

  vector<bool> in_tree;
  in_tree.resize(np);
  for(int i=0; i != np; i++)
    in_tree[i] = i == 0;
  for(int i=0; i<np-1; i++) {
    int best_dist = 0;
    int best_a = -1;
    int best_b = -1;
    for(int a=0; a<np; a++)
      if(!in_tree[a])
	for(int b=0; b<np; b++)
	  if(in_tree[b]) {
	    int dist = abs(pt[a].x-pt[b].x) + abs(pt[a].y-pt[b].y);
	    if(best_a == -1 || dist <= best_dist) {
	      best_a = a;
	      best_b = b;
	      best_dist = dist;
	    }
	  }
    in_tree[best_a] = true;
    link_keys.push_back((((uint_least64_t)best_dist) << 48) | (((uint_least64_t)best_a) << 32) | (best_b << 16) | (nid));
  }
}

void net::handle_key(uint_least64_t k)
{
  draw_order.push_back(pair<int, int>((k >> 32) & 0xffff, (k >> 16) & 0xffff));
}

void net::to_svg(FILE *fd) const
{
  if(nodes.empty())
    return;

  point p = get_center();
  if(!in_limit(p))
    return;

  int np = nodes.size() + routes.size();
  vector<point> pt;
  map<int, int> use_count;
  pt.resize(np);

  for(unsigned int i = 0; i != nodes.size(); i++) {
    pt[i] = nodes[i].n->get_pos(nodes[i].pin);
    use_count[pt[i].y*65536 + pt[i].x]++;
  }
  for(unsigned int i = 0; i != routes.size(); i++)
    pt[i+nodes.size()] = routes[i];

  fprintf(fd, "  <g id=\"%s\"><desc>%s</desc>\n", state->ninfo.net_name(id).c_str(), oname.c_str());
  for(list<pair<int, int> >::const_iterator i = draw_order.begin(); i != draw_order.end(); i++) {
    if(pt[i->first].x == pt[i->second].x && pt[i->first].y == pt[i->second].y)
      continue;
    fprintf(fd, "    <path d=\"M %d %d %d %d\" />\n", pt[i->first].x*10, pt[i->first].y*10, pt[i->second].x*10, pt[i->second].y*10);
    use_count[pt[i->first].y*65536 + pt[i->first].x]++;
    use_count[pt[i->second].y*65536 + pt[i->second].x]++;
  }
  for(map<int, int>::const_iterator i = use_count.begin(); i != use_count.end(); i++)
    if(i->second > 2)
      for(int j=0; j != np; j++)
	if(pt[j].y*65536+pt[j].x == i->first)
	  fprintf(fd, "    <path style=\"fill:#000000;stroke:none\" d=\"m %d,%d a 3,3 0 1 1 -6,0 3,3 0 1 1 6,0 z\" />\n", 10*pt[j].x+3, 10*pt[j].y);
  fprintf(fd, "  </g>\n");
}

void net::to_txt(FILE *fd) const
{
  int np = nodes.size() + routes.size();
  vector<point> pt;
  map<int, int> use_count;
  pt.resize(np);

  for(unsigned int i = 0; i != nodes.size(); i++) {
    pt[i] = nodes[i].n->get_pos(nodes[i].pin);
    use_count[pt[i].y*65536 + pt[i].x]++;
  }
  for(unsigned int i = 0; i != routes.size(); i++)
    pt[i+nodes.size()] = routes[i];

  fprintf(fd, "%d", np);
  for(int i=0; i != np; i++)
    fprintf(fd, " %d %d", pt[i].x, sy1-pt[i].y);

  int nd = 0;
  for(list<pair<int, int> >::const_iterator i = draw_order.begin(); i != draw_order.end(); i++) {
    if(pt[i->first].x == pt[i->second].x && pt[i->first].y == pt[i->second].y)
      continue;
    use_count[pt[i->first].y*65536 + pt[i->first].x]++;
    use_count[pt[i->second].y*65536 + pt[i->second].x]++;
    nd++;
  }
  int uc = 0;
  for(map<int, int>::const_iterator i = use_count.begin(); i != use_count.end(); i++)
    if(i->second > 2)
      uc++;

  fprintf(fd, " %d", nd);
  for(list<pair<int, int> >::const_iterator i = draw_order.begin(); i != draw_order.end(); i++) {
    if(pt[i->first].x == pt[i->second].x && pt[i->first].y == pt[i->second].y)
      continue;
    fprintf(fd, " %d %d", i->first, i->second);
  }

  fprintf(fd, " %d", uc);
  for(map<int, int>::const_iterator i = use_count.begin(); i != use_count.end(); i++)
    if(i->second > 2) {
      for(int j=0; j != np; j++)
	if(pt[j].y*65536+pt[j].x == i->first) {
	  fprintf(fd, " %d", j);
	  break;
	}
    }
  fprintf(fd, " %s\n", oname.c_str());
}

void net::draw(patch &p, int ox, int oy) const
{
  if(nodes.empty())
    return;

  int np = nodes.size() + routes.size();
  vector<point> pt;
  map<int, int> use_count;
  pt.resize(np);

  for(unsigned int i = 0; i != nodes.size(); i++) {
    pt[i] = nodes[i].n->get_pos(nodes[i].pin);
    use_count[pt[i].y*65536 + pt[i].x]++;
  }
  for(unsigned int i = 0; i != routes.size(); i++)
    pt[i+nodes.size()] = routes[i];

  for(list<pair<int, int> >::const_iterator i = draw_order.begin(); i != draw_order.end(); i++) {
    if(pt[i->first].x == pt[i->second].x && pt[i->first].y == pt[i->second].y)      
      continue;
    p.line(ox, oy, pt[i->first].x*10, pt[i->first].y*10, pt[i->second].x*10, pt[i->second].y*10);
    use_count[pt[i->first].y*65536 + pt[i->first].x]++;
    use_count[pt[i->second].y*65536 + pt[i->second].x]++;
  }
  for(map<int, int>::const_iterator i = use_count.begin(); i != use_count.end(); i++)
    if(i->second > 2) {
      int bx = (i->first & 65535)*10;
      int by = (i->first >> 16)*10;
      p.hline(ox, oy, bx-2, bx+2, by-3);
      p.hline(ox, oy, bx-3, bx+3, by-2);
      p.hline(ox, oy, bx-4, bx+4, by-1);
      p.hline(ox, oy, bx-4, bx+4, by);
      p.hline(ox, oy, bx-4, bx+4, by+1);
      p.hline(ox, oy, bx-3, bx+3, by+2);
      p.hline(ox, oy, bx-2, bx+2, by+3);
    }
}

void draw(const char *format, const vector<node *> &nodes, const vector<net *> &nets)
{
  unsigned int limx = int(state->info.sx / ratio)*10/PATCH_SX;
  unsigned int limy = int(state->info.sy / ratio)*10/PATCH_SY;

  unsigned int maxlim = limx > limy ? limx : limy;
  unsigned int limp = 1;
  while(maxlim > 0) {
    limp++;
    maxlim = maxlim >> 1;
  }

  char msg[4096];
  sprintf(msg, "generating images, %d levels", limp);
  time_info tinfo;
  start(tinfo, msg);

  unsigned int plim = 1 << (2*limp-2);
  patch *levels = new patch[limp];
  unsigned int nno = nodes.size();
  unsigned int nne = nets.size();
  int id = 0;
  for(unsigned int i=0; i<plim; i++) {
    unsigned int x0 = 0, y0 = 0;
    for(unsigned int j=0; j<limp; j++) {
      if(i & (1 << (2*j)))
	x0 |= 1 << j;
      if(i & (2 << (2*j)))
	y0 |= 1 << j;
    }
    if(x0 > limx || y0 > limy)
      continue;

    int ox = x0 * PATCH_SX;
    int oy = y0 * PATCH_SY;

    for(unsigned int j=0; j != nno; j++)
      nodes[j]->draw(levels[0], ox, oy);
    for(unsigned int j=0; j != nne; j++)
      nets[j]->draw(levels[0], ox, oy);

    bool last_x = x0 == limx;
    bool last_y = y0 == limy;
    unsigned int x1 = x0;
    unsigned int y1 = y0;
    for(unsigned int level = 1; level < limp; level++) {
      levels[level].mipmap(levels[level-1], x1 & 1, y1 & 1);
      if(!((last_x || (x1 & 1)) && (last_y || (y1 & 1))))
	break;
      x1 = x1 >> 1;
      y1 = y1 >> 1;
    }
    x1 = x0;
    y1 = y0;
    for(unsigned int level = 0; level < limp; level++) {
      char name[4096];
      sprintf(name, format, limp-1-level, y1, x1);
      levels[level].save_png(name);
      levels[level].clear();
      if(!((last_x || (x1 & 1)) && (last_y || (y1 & 1))))
	break;
      x1 = x1 >> 1;
      y1 = y1 >> 1;
    }      
    tick(tinfo, id++, (limx+1)*(limy+1));
  }
  delete[] levels;
}

void save_txt(const char *fname, int sx, int sy, const vector<node *> &nodes, const vector<net *> &nets)
{
  char msg[4096];
  sprintf(msg, "Error opening %s for writing", fname);
  FILE *fd = fopen(fname, "w");
  if(!fd) {
    perror(msg);
    exit(1);
  }

  fprintf(fd, "%d %d %f\n", sx, sy, ratio);

  fprintf(fd, "%d nodes\n", int(nodes.size()));
  for(unsigned int i=0; i != nodes.size(); i++)
    nodes[i]->to_txt(fd);

  fprintf(fd, "%d nets\n", int(nets.size()));
  for(unsigned int i=0; i != nets.size(); i++)
    nets[i]->to_txt(fd);
  fclose(fd);
}

void build_mosfets(vector<node *> &nodes, map<int, list<ref> > &nodemap)
{
  vector<unsigned long> transinf;
  for(unsigned int i=0; i != state->info.trans.size(); i++) {
    const tinfo &ti = state->info.trans[i];
    unsigned long id;
    if(ti.t1 < ti.t2)
      id = (((uint_least64_t)(ti.t1)) << 48) | (((uint_least64_t)(ti.t2)) << 32);
    else
      id = (((uint_least64_t)(ti.t2)) << 48) | (((uint_least64_t)(ti.t1)) << 32);
    id |= (ti.gate << 20) | i;
    transinf.push_back(id);
  }

  sort(transinf.begin(), transinf.end());

  double f = 0;
  for(unsigned int i=0; i != state->info.trans.size(); i++) {
    int tid = transinf[i] & 0xfffff;
    f += state->info.trans[tid].f;
    if(i != state->info.trans.size()-1 && !((transinf[i] ^ transinf[i+1]) & 0xfffffffffff00000UL))
      continue;

    mosfet *m = new mosfet(tid, f, state->ttype[tid]);
    nodes.push_back(m);
    nodemap[state->info.trans[tid].t1].push_back(ref(m, T1));
    nodemap[state->info.trans[tid].t2].push_back(ref(m, T2));
    nodemap[state->info.trans[tid].gate].push_back(ref(m, GATE));
    f = 0;
  }
}

void build_capacitors(vector<node *> &nodes, map<int, list<ref> > &nodemap)
{
  vector<unsigned long> capsinf;
  for(unsigned int i=0; i != state->info.circs.size(); i++) {
    const cinfo &ci = state->info.circs[i];
    if(ci.type == 'c') {
      unsigned long id;
      if(ci.net < ci.netp)
	id = (((uint_least64_t)(ci.net )) << 48) | (((uint_least64_t)(ci.netp)) << 32);
      else
	id = (((uint_least64_t)(ci.netp)) << 48) | (((uint_least64_t)(ci.net )) << 32);
      id |= i;
      capsinf.push_back(id);
    }
  }

  sort(capsinf.begin(), capsinf.end());

  double f = 0;
  for(unsigned int i=0; i != capsinf.size(); i++) {
    int cid = capsinf[i] & 0xffffffff;
    f += state->info.circs[cid].surface;
    if(i != capsinf.size()-1 && !((capsinf[i] ^ capsinf[i+1]) & 0xffffffffffff0000UL))
      continue;
    
    capacitor *caps = new capacitor(cid, f);
    nodes.push_back(caps);
    nodemap[state->info.circs[cid].net].push_back(ref(caps, T1));
    nodemap[state->info.circs[cid].netp].push_back(ref(caps, T2));
    f = 0;
  }
}

void build_pads(const char *fname, vector<node *> &nodes, map<int, list<ref> > &nodemap)
{
  pad_info pp(fname, state->ninfo);
  for(unsigned int i=0; i != pp.pads.size(); i++) {
    const pinfo &pi = pp.pads[i];
    point p;
    p.x = int(pi.x / ratio + 0.5);
    p.y = int(((state->info.sy - 1) - pi.y) / ratio + 0.5);
    int orientation;
    switch(pi.orientation) {
    case 'n': orientation = N_S; break;
    case 's': orientation = S_S; break;
    case 'e': orientation = E_S; break;
    case 'w': orientation = W_S; break;
    default: abort();
    }
    pad *pa = new pad(pi.name, p, orientation);
    nodes.push_back(pa);
    nodemap[pi.net].push_back(ref(pa, T1));
  }
}

void build_nets(vector<net *> &nets, const map<int, list<ref> > &nodemap)
{
  for(unsigned int i=0; i != state->info.nets.size(); i++) {
    net *n = new net(i, nets.size(), false);
    nets.push_back(n);
    if(int(i) == state->vcc || int(i) == state->gnd)      
      continue;
    map<int, list<ref> >::const_iterator k = nodemap.find(i);
    if(k != nodemap.end())
      for(list<ref>::const_iterator j = k->second.begin(); j != k->second.end(); j++) {
	n->add_node(*j);
	j->n->add_net(j->pin, n);
      }
  }
}

void build_power_nodes_and_nets(vector<node *> &nodes, vector<net *> &nets)
{
  int nn = nodes.size();
  for(int i=0; i != nn; i++)
    nodes[i]->build_power_nodes_and_nets(nodes, nets);
}

void build_net_links(vector<net *> &nets)
{
  vector<unsigned long> link_keys;
  for(unsigned int i=0; i != nets.size(); i++)
    nets[i]->add_link_keys(i, link_keys);
  sort(link_keys.begin(), link_keys.end());
  for(unsigned int i=0; i != link_keys.size(); i++) {
    unsigned long k = link_keys[i];
    nets[k & 0xffff]->handle_key(k);
  }
}

vector<node *> nodes;
vector<net *> nets;

int l_nodes_rect(lua_State *L)
{
  luaL_argcheck(L, lua_isnumber(L, 1), 1, "x0 expected");
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "y0 expected");
  luaL_argcheck(L, lua_isnumber(L, 3), 3, "x1 expected");
  luaL_argcheck(L, lua_isnumber(L, 4), 4, "y1 expected");

  int x0 = lua_tonumber(L, 1);
  int y0 = (state->info.sy - 1) / ratio - lua_tonumber(L, 2);
  int x1 = lua_tonumber(L, 3);
  int y1 = (state->info.sy - 1) / ratio - lua_tonumber(L, 4);

  lua_newtable(L);
  int id = 1;
  for(unsigned int i = 0; i != nodes.size(); i++) {
    node *n = nodes[i];
    if(n->pos.x >= x0 && n->pos.x <= x1 && n->pos.y >= y0 && n->pos.y <= y1) {
      n->wrap(L);
      lua_rawseti(L, -2, id++);
    }
  }
  return 1;
}

net *get_net_from_name(const char *name)
{
  int nid = state->ninfo.find(name);
  if(nid == -1) {
    fprintf(stderr, "net %s unknown\n", name);
    exit(1);
  }
  for(unsigned int i=0; i != nets.size(); i++)
    if(nets[i]->id == nid)
      return nets[i];
  abort();
}

int l_named_net(lua_State *L)
{
  luaL_argcheck(L, lua_isstring(L, 1), 1, "net name expected");
  get_net_from_name(lua_tostring(L, 1))->wrap(L);
  return 1;
}

int l_nodes_trace(lua_State *L)
{
  luaL_argcheck(L, lua_isstring(L, 1) || net::checkparam(L, 1), 1, "start net/net name expected");
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "depth expected");

  net *base;
  if(lua_isstring(L, 1))
    base = get_net_from_name(lua_tostring(L, 1));
  else
    base = net::getparam(L, 1);

  int depth = lua_tonumber(L, 2);
  set<net *> edges;
  for(int i=3; i<=lua_gettop(L); i++) {
    luaL_argcheck(L, lua_isstring(L, i) || net::checkparam(L, i), i, "edge net/net name expected");
    net *n;
    if(lua_isstring(L, i))
      n = get_net_from_name(lua_tostring(L, i));
    else
      n = net::getparam(L, i);
    edges.insert(n);
  }

  list<net *> stack;
  set<net *> found;
  set<node *> fnodes;
  stack.push_back(base);
  found.insert(base);
  for(int i=0; i<depth; i++) {
    list<net *> ostack = stack;
    stack.clear();
    for(list<net *>::const_iterator j = ostack.begin(); j != ostack.end(); j++) {
      net *n = *j;

      for(unsigned int k=0; k != n->nodes.size(); k++) {
	node *nd = n->nodes[k].n;
	fnodes.insert(nd);
	for(unsigned int l=0; l != nd->nets.size(); l++) {
	  net *nn = nd->nets[l];
	  if(found.find(nn) == found.end() && edges.find(nn) == edges.end()) {
	    found.insert(nn);
	    stack.push_back(nn);
	  }
	}
      }
    }
  }
 
  lua_newtable(L);
  int id = 1;
  for(set<node *>::const_iterator i = fnodes.begin(); i != fnodes.end(); i++) {
    (*i)->wrap(L);
    lua_rawseti(L, -2, id++);
  }
  return 1; 
}

int l_make_match(lua_State *L)
{
  luaL_argcheck(L, lua_istable(L, 1), 1, "node array expected");
  luaL_argcheck(L, lua_gettop(L) < 2 || lua_istable(L, 2) || lua_isnil(L, 2), 2, "presets expected");
  set<node *> all_nodes;
  set<net *> all_nets;
  set<node *> preset_nodes;
  set<net *> preset_nets;
  map<node *, string> node_names;
  map<net *, string> net_names;
  set<string> all_names;

  lua_pushvalue(L, 1);
  lua_pushnil(L);
  while(lua_next(L, -2) != 0) {
    lt *l = lt::getparam_any(L, -1);
    if(dynamic_cast<node *>(l))
      all_nodes.insert(static_cast<node *>(l));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  if(lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
    lua_pushvalue(L, 2);
    lua_pushnil(L);
    while(lua_next(L, -2) != 0) {
      lt *l = lt::getparam_any(L, -1);
      lua_pushvalue(L, -2);
      string name = lua_tostring(L, -1);
      all_names.insert(name);
      if(dynamic_cast<node *>(l)) {
	all_nodes.insert(static_cast<node *>(l));
	node_names[static_cast<node *>(l)] = name;
	preset_nodes.insert(static_cast<node *>(l));
      } else {
	net_names[static_cast<net *>(l)] = name;
	preset_nets.insert(static_cast<net *>(l));
      }
      lua_pop(L, 2);
    }
    lua_pop(L, 1);
  }

  for(set<node *>::const_iterator i = all_nodes.begin(); i != all_nodes.end(); i++) {
    node *n = *i;
    for(unsigned int j=0; j != n->nets.size(); j++)
      all_nets.insert(n->nets[j]);
  }

  int id = 0;
  for(set<node *>::const_iterator i = all_nodes.begin(); i != all_nodes.end(); i++) {
    if(node_names.find(*i) != node_names.end())
      continue;
    char buf[32];
    do {
      if(all_nodes.size() < 26) {
	buf[0] = 'A' + id;
	buf[1] = 0;
      } else
	sprintf(buf, "N%d", id);
      id++;
    } while(all_names.find(buf) != all_names.end());
    all_names.insert(buf);
    node_names[*i] = buf;
  }

  id = 0;
  for(set<net *>::const_iterator i = all_nets.begin(); i != all_nets.end(); i++) {
    if(net_names.find(*i) != net_names.end())
      continue;
    char buf[32];
    do {
      if(all_nets.size() < 26) {
	buf[0] = 'a' + id;
	buf[1] = 0;
      } else
	sprintf(buf, "n%d", id);
      id++;
    } while(all_names.find(buf) != all_names.end());
    all_names.insert(buf);
    net_names[*i] = buf;
  }

  string res;
  for(set<node *>::const_iterator i = all_nodes.begin(); i != all_nodes.end(); i++)
    if(preset_nodes.find(*i) == preset_nodes.end()) {
      static const int default_gate_order[2] = { T1, T2 };
      static const int mosfet_gate_order[3] = { T1, GATE, T2 };
      const int *gate_order = default_gate_order;
      char buf[4096];
      char *p = buf;
      node *n = *i;
      p += sprintf(p, "%s=", node_names[n].c_str());
      if(dynamic_cast<mosfet *>(n)) {
	static const char ttype_string[] = "tid";
	p += sprintf(p, "%c", ttype_string[static_cast<mosfet *>(n)->ttype]);
	gate_order = mosfet_gate_order;
      } else if(dynamic_cast<capacitor *>(n))
	p += sprintf(p, "c");
      else if(dynamic_cast<power_node *>(n))
	p += sprintf(p, "%s", static_cast<power_node *>(n)->is_vcc ? "vcc" : "gnd");
      else if(dynamic_cast<pad *>(n))
	p += sprintf(p, "pad");
      else
	abort();
      *p++ = '(';
      for(unsigned int i=0; i != n->nets.size(); i++) {
	if(i) {
	  *p++ = ',';
	  *p++ = ' ';
      }
	p += sprintf(p, "%s", net_names[n->nets[gate_order[i]]].c_str());
      }
      *p++ = ')';
      *p++ = '\n';
      *p++ = 0;
      res += buf;
    }

  for(set<net *>::const_iterator i = all_nets.begin(); i != all_nets.end(); i++) {
    net *n = *i;
    if(preset_nets.find(n) == preset_nets.end()) {
      if(n->id == -1 || state->ninfo.names[n->id].empty())
	continue;
      char buf[4096];
      sprintf(buf, "%s~%s\n", net_names[n].c_str(), state->ninfo.names[n->id].c_str());
      res += buf;
    }
  }

  lua_pushstring(L, res.c_str());
  return 1;
}

struct match_entry {
  string type;
  vector<string> params;
};

bool string_match(string::const_iterator ns, string::const_iterator ne, string::const_iterator ms, string::const_iterator me)
{
  while(ms != me) {
    switch(*ms) {
    case '?':
      if(ns == ne)
	return false;
      ms++;
      ns++;
      break;

    case '*':
      ms++;
      for(;;) {
	if(string_match(ns, ne, ms, me))
	  return true;
	if(ns == ne)
	  return false;
	ns++;
      }

    default:
      if(ns == ne)
	return false;
      if(*ms++ != *ns++)
	return false;
      break;
    }
  }
  return ns == ne;
}

bool string_match(string name, string mask)
{
  return string_match(name.begin(), name.end(), mask.begin(), mask.end());
}

int l_match(lua_State *L)
{
  luaL_argcheck(L, lua_istable(L, 1), 1, "node array expected");
  luaL_argcheck(L, lua_isstring(L, 2), 2, "match string exepected");
  luaL_argcheck(L, lua_gettop(L) < 3 || lua_istable(L, 3) || lua_isnil(L, 3), 3, "presets expected");

  set<node *> all_nodes;
  lua_pushvalue(L, 1);
  lua_pushnil(L);
  while(lua_next(L, -2) != 0) {
    lt *l = lt::getparam_any(L, -1);
    if(dynamic_cast<node *>(l))
      all_nodes.insert(static_cast<node *>(l));
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  string match = lua_tostring(L, 2);

  map<string, node *> preset_nodes;
  map<string, net *> preset_nets;
  set<node *> preset_nodes_set;
  set<net *> preset_nets_set;

  if(lua_gettop(L) >= 3 && !lua_isnil(L, 3)) {
    lua_pushvalue(L, 3);
    lua_pushnil(L);
    while(lua_next(L, -2) != 0) {
      lt *l = lt::getparam_any(L, -1);
      lua_pushvalue(L, -2);
      string name = lua_tostring(L, -1);
      if(dynamic_cast<node *>(l)) {
	preset_nodes[name] = static_cast<node *>(l);
	preset_nodes_set.insert(static_cast<node *>(l));
      } else {
	preset_nets[name] = static_cast<net *>(l);
	preset_nets_set.insert(static_cast<net *>(l));
      }
      lua_pop(L, 2);
    }
    lua_pop(L, 1);
  }

  map<string, match_entry> matches;
  map<string, string> name_constraints;

  string::iterator p = match.begin();
  while(p != match.end()) {
    while(p != match.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
      p++;
    if(p == match.end())
      break;
    string::iterator q = p;
    while(p != match.end() && *p != ' ' && *p != '\n' && *p != '\t' && *p != '=' && *p != '~')
      p++;
    string name = string(q, p);
    while(p != match.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
      p++;
    if(p == match.end())
      break;
    if(*p == '=') {
      match_entry me;
      p++;
      while(p != match.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	p++;
      if(p == match.end())
	break;
      q = p;
      while(p != match.end() && *p != ' ' && *p != '\n' && *p != '\t' && *p != '(')
	p++;
      me.type = string(q, p);
      int np;
      if(me.type == "t" || me.type == "d" || me.type == "i")
	np = 3;
      else if(me.type == "c")
	np = 2;
      else if(me.type == "vcc" || me.type == "gnd" || me.type == "pad")
	np = 1;
      else {
	fprintf(stderr, "match: unknown type %s\n", me.type.c_str());
	exit(1);
      }
      while(p != match.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	p++;
      if(p == match.end())
	break;
      if(*p != '(') {
	fprintf(stderr, "match: ( expected after %s=%s\n", name.c_str(), me.type.c_str());
	exit(1);
      }
      p++;
      for(;;) {
	while(p != match.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	  p++;
	if(p == match.end())
	  break;
	q = p;
	while(p != match.end() && *p != ' ' && *p != '\n' && *p != '\t' && *p != ')' && *p != ',')
	  p++;
	me.params.push_back(string(q, p));
	while(p != match.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	  p++;
	if(p == match.end())
	  break;
	if(*p != ')' && *p != ',') {
	  fprintf(stderr, "match: ) or , expected after %s=%s parameter\n", name.c_str(), me.type.c_str());
	  exit(1);
	}
	if(*p++ == ')')
	  break;
      }
      if(np != int(me.params.size())) {
	fprintf(stderr, "match: Wrong parameter count for %s=%s, got %d, expected %d\n", name.c_str(), me.type.c_str(), int(me.params.size()), np);
	exit(1);
      }
      matches[name] = me;
    } else if(*p == '~') {
      p++;
      while(p != match.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	p++;
      if(p == match.end())
	break;
      q = p;
      while(p != match.end() && *p != ' ' && *p != '\n' && *p != '\t')
	p++;
      name_constraints[name] = string(q, p);
    } else {
      fprintf(stderr, "match: expected = or ~ after %s\n", name.c_str());
      exit(1);
    }
  }

  list<string> match_order;
  set<string> match_tagged;

  list<string> match_unordered;
  for(map<string, match_entry>::iterator i = matches.begin(); i != matches.end(); i++) {
    if(preset_nodes.find(i->first) != preset_nodes.end()) {
      match_order.push_back(i->first);
      for(unsigned int j=0; j != i->second.params.size(); j++)
	match_tagged.insert(i->second.params[j]);
    } else
      match_unordered.push_back(i->first);
  }

  for(map<string, net *>::iterator i = preset_nets.begin(); i != preset_nets.end(); i++)
    match_tagged.insert(i->first);

  while(!match_unordered.empty()) {
    int best_free_count = 0;
    list<string>::iterator best_free;
    for(list<string>::iterator i = match_unordered.begin(); i != match_unordered.end();) {
      const match_entry &m = matches[*i];
      int count = 0;
      for(unsigned int j=0; j != m.params.size(); j++)
	if(match_tagged.find(m.params[j]) == match_tagged.end())
	  count++;
      if(!count) {
	match_order.push_back(*i);
	list<string>::iterator ii = i;
	i++;
	match_unordered.erase(ii);
      } else {
	if(m.params.size() == 1)
	  count += 10;
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
    const match_entry &m = matches[*best_free];
    for(unsigned int j=0; j != m.params.size(); j++)
      match_tagged.insert(m.params[j]);
    match_unordered.erase(best_free);
  }

  int slot = 0;
  vector<set<node *>::iterator> cursors;
  vector<int> alts;
  vector<bool> fixed;
  cursors.resize(matches.size());
  alts.resize(matches.size());
  list<string>::iterator cur_match = match_order.begin();
  map<string, node *> cur_nodes;
  map<string, net *> cur_nets;
  map<string, int> net_slots;
  set<node *> used_nodes;
  set<net *> used_nets;
  goto changed_slot;

 changed_slot:
  {
    if(0)
      fprintf(stderr, "changed slot %d\n", slot);
    map<string, node *>::const_iterator pi = preset_nodes.find(*cur_match);
    if(pi == preset_nodes.end())
      cursors[slot] = all_nodes.begin();
    else
      cursors[slot] = all_nodes.find(pi->second);
  }

 try_slot:
  {
    if(0)
      fprintf(stderr, "try slot %d\n", slot);
    const match_entry &me = matches[*cur_match];
    assert(cursors[slot] != all_nodes.end());
    node *n = *cursors[slot];
    if(used_nodes.find(n) != used_nodes.end())
      goto next_non_alt_in_slot;
    if(preset_nodes_set.find(n) != preset_nodes_set.end() && preset_nodes.find(*cur_match) == preset_nodes.end())
      goto next_non_alt_in_slot;

    bool compatible;
    if(me.type == "t" || me.type == "d" || me.type == "i") {
      int ttype = me.type == "t" ? State::T_NMOS : me.type == "d" ? State::T_NDEPL : State::T_PMOS;
      mosfet *m = dynamic_cast<mosfet *>(n);
      compatible = m && (m->ttype == ttype);

    } else if(me.type == "c")
      compatible = dynamic_cast<capacitor *>(n);

    else if(me.type == "vcc" || me.type == "gnd") {
      power_node *m = dynamic_cast<power_node *>(n);
      compatible = m && ((me.type == "gnd") ^ m->is_vcc);

    } else if(me.type == "pad")
      compatible = dynamic_cast<pad *>(n);

    else
      abort();

    if(!compatible)
      goto next_non_alt_in_slot;

    used_nodes.insert(n);
    if(0)
      fprintf(stderr, "slot %d adding %s\n", slot, cur_match->c_str());
    cur_nodes[*cur_match] = n;
    alts[slot] = 0;
    goto try_alt;
  }

 try_alt:
  {
    if(0) {
      mosfet *n = dynamic_cast<mosfet *>(*cursors[slot]);
      fprintf(stderr, "try alt %d %d (%s t%d)\n", slot, alts[slot], cur_match->c_str(), n ? n->trans : -1);
    }
    const match_entry &me = matches[*cur_match];
    node *n = *cursors[slot];
    vector<net *> params;
    params.resize(me.params.size());
    if(me.type == "t" || me.type == "d" || me.type == "i") {
      switch(alts[slot]) {
      case 0:
	params[0] = n->nets[T1];
	params[1] = n->nets[GATE];
	params[2] = n->nets[T2];
	break;
      case 1:
	params[0] = n->nets[T2];
	params[1] = n->nets[GATE];
	params[2] = n->nets[T1];
	break;
      }
    } else if(me.type == "c") {
      switch(alts[slot]) {
      case 0:
	params[0] = n->nets[T1];
	params[1] = n->nets[T2];
	break;
      case 1:
	params[0] = n->nets[T2];
	params[1] = n->nets[T1];
	break;
      }
    } else
      params[0] = n->nets[T1];

    map<string, net *> temp_nets;
    set<net *> temp_used_nets;
    for(unsigned int i=0; i != me.params.size(); i++) {
      if(0) {
	fprintf(stderr, "  check param %d (%s) vs. %p (%d)\n", i, me.params[i].c_str(), params[i], params[i]->id);
      }
      map<string, net *>::const_iterator ni = cur_nets.find(me.params[i]);	
      if(ni == cur_nets.end()) {
	ni = preset_nets.find(me.params[i]);
	if(ni == preset_nets.end()) {
	  ni = temp_nets.find(me.params[i]);
	  if(ni == temp_nets.end()) {
	    if(used_nets.find(params[i]) != used_nets.end())
	      goto next_alt_in_slot;
	    if(temp_used_nets.find(params[i]) != temp_used_nets.end())
	      goto next_alt_in_slot;
	    if(preset_nets_set.find(params[i]) != preset_nets_set.end())
	      goto next_alt_in_slot;
	    if(name_constraints.find(me.params[i]) != name_constraints.end())
	      if(params[i]->id == -1 || state->ninfo.names[params[i]->id].empty() || !string_match(state->ninfo.names[params[i]->id], name_constraints[me.params[i]]))
		 goto next_alt_in_slot;
	    temp_nets[me.params[i]] = params[i];
	    temp_used_nets.insert(params[i]);
	    continue;
	  }
	}
      }
      if(ni->second != params[i])
	  goto next_alt_in_slot;
      if(cur_nets.find(me.params[i]) == cur_nets.end() && preset_nets.find(me.params[i]) != preset_nets.end())
	temp_nets[me.params[i]] = params[i];
    }

    for(map<string, net *>::const_iterator i = temp_nets.begin(); i != temp_nets.end(); i++) {
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
    const match_entry &me = matches[*cur_match];
    int nalt = me.type == "t" || me.type == "d" || me.type == "i" || me.type == "c" ? 2 : 1;
    alts[slot]++;
    if(alts[slot] == nalt) {
      node *n = *cursors[slot];   
      assert(used_nodes.find(n) != used_nodes.end());
      used_nodes.erase(used_nodes.find(n));
      if(0)
	fprintf(stderr, "slot %d removing %s\n", slot, cur_match->c_str());
      cur_nodes.erase(cur_nodes.find(*cur_match));
      goto next_non_alt_in_slot;
    }
    goto try_alt;
  }

 next_non_alt_in_slot:
  {
    if(0)
      fprintf(stderr, "next non alt in slot %d\n", slot);
    
    map<string, node *>::const_iterator pi = preset_nodes.find(*cur_match);
    if(pi != preset_nodes.end())
      goto backoff_slot;
    cursors[slot]++;
    if(cursors[slot] == all_nodes.end())
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
    const match_entry &me = matches[*cur_match];
    for(unsigned int i=0; i != me.params.size(); i++) {
      map<string, int>::iterator j = net_slots.find(me.params[i]);
      if(j != net_slots.end() && j->second == slot) {
	assert(used_nets.find(cur_nets[me.params[i]]) != used_nets.end());
	used_nets.erase(used_nets.find(cur_nets[me.params[i]]));;
	net_slots.erase(j);
	cur_nets.erase(cur_nets.find(me.params[i]));
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
  return 0;

 match:
  lua_newtable(L);
  for(map<string, node *>::const_iterator i = cur_nodes.begin(); i != cur_nodes.end(); i++) {
    i->second->wrap(L);
    lua_setfield(L, -2, i->first.c_str());
  }
  for(map<string, net *>::const_iterator i = cur_nets.begin(); i != cur_nets.end(); i++) {
    i->second->wrap(L);
    lua_setfield(L, -2, i->first.c_str());
  }
  return 1;
}

int l_move(lua_State *L)
{
  luaL_argcheck(L, lua_istable(L, 1), 1, "node hash expected");
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "x coordinate expected");
  luaL_argcheck(L, lua_isnumber(L, 3), 3, "y coordinate expected");
  luaL_argcheck(L, lua_isstring(L, 4), 4, "movement string exepected");

  map<string, node *> nodes;
  map<string, net *> nets;

  lua_pushvalue(L, 1);
  lua_pushnil(L);
  while(lua_next(L, -2) != 0) {
    lt *l = lt::getparam_any(L, -1);
    lua_pushvalue(L, -2);
    string name = lua_tostring(L, -1);
    if(dynamic_cast<node *>(l))
      nodes[name] = static_cast<node *>(l);
    else
      nets[name] = static_cast<net *>(l);

    lua_pop(L, 2);
  }
  lua_pop(L, 1);

  int x = lua_tointeger(L, 2);
  int y = lua_tointeger(L, 3);

  string move = lua_tostring(L, 4);
  string::iterator p = move.begin();
  while(p != move.end()) {
    while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
      p++;
    if(p == move.end())
      break;
    string::iterator q = p;
    while(p != move.end() && *p != ' ' && *p != '\n' && *p != '\t')
      p++;
    string name = string(q, p);
    node *n = nodes[name];
    if(!n) {
      fprintf(stderr, "move: Error, node %s not given\n", name.c_str());
      exit(1);
    }
    while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
      p++;
    if(p == move.end())
      break;
    q = p;
    while(p != move.end() && *p != ' ' && *p != '\n' && *p != '\t')
      p++;
    int dx = strtol(string(q, p).c_str(), 0, 10);
    while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
      p++;
    if(p == move.end())
      break;
    q = p;
    while(p != move.end() && *p != ' ' && *p != '\n' && *p != '\t' && *p != ',')
      p++;
    int dy = strtol(string(q, p).c_str(), 0, 10);

    n->move(x+dx, ((state->info.sy - 1)/ratio) - (y+dy));
    while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
      p++;
    if(p == move.end())
      break;
    if(*p != ',') {
      q = p;
      while(p != move.end() && *p != ' ' && *p != '\n' && *p != '\t' && *p != ',')
	p++;
      string net_name(q+1, p);
      if(net_name != "") {
	if(nets.find(net_name) == nets.end()) {
	  fprintf(stderr, "move: net %s not found\n", net_name.c_str());
	  exit(1);
	}
      }
      net *onet = net_name != "" ? nets[net_name] : NULL;
      if(onet) {
	if(n->nets.size() == 1) {
	  fprintf(stderr, "move: node %s needs no net for orientation\n", name.c_str());
	  exit(1);
	}
	if(n->nets[T1] != onet && n->nets[T2] != onet) {
	  fprintf(stderr, "move: node %s is not connected through a terminal to net %s\n", name.c_str(), net_name.c_str());
	  exit(1);
	}
      }
      n->set_orientation(*q, onet);
      while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	p++;
      if(p == move.end())
	break;
      if(*p != ',') {
	while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	  p++;
	if(p == move.end())
	  break;
	q = p;
	while(p != move.end() && *p != ' ' && *p != '\n' && *p != '\t' && *p != ',')
	  p++;
	n->set_subtype(string(q, p));
      }
      while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	p++;
      if(p == move.end())
	break;
    }
    if(*p++ != ',') {
      fprintf(stderr, "move: Missing comma\n");
      exit(1);
    }
  }
  return 0;
}

int l_route(lua_State *L)
{
  luaL_argcheck(L, lua_istable(L, 1), 1, "net hash expected");
  luaL_argcheck(L, lua_isnumber(L, 2), 2, "x coordinate expected");
  luaL_argcheck(L, lua_isnumber(L, 3), 3, "y coordinate expected");
  luaL_argcheck(L, lua_isstring(L, 4), 4, "routing string exepected");

  map<string, net *> nets;

  lua_pushvalue(L, 1);
  lua_pushnil(L);
  while(lua_next(L, -2) != 0) {
    lt *l = lt::getparam_any(L, -1);
    lua_pushvalue(L, -2);
    string name = lua_tostring(L, -1);
    if(dynamic_cast<net *>(l))
      nets[name] = static_cast<net *>(l);

    lua_pop(L, 2);
  }
  lua_pop(L, 1);

  int x = lua_tointeger(L, 2);
  int y = lua_tointeger(L, 3);

  string move = lua_tostring(L, 4);
  string::iterator p = move.begin();
  while(p != move.end()) {
    while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
      p++;
    if(p == move.end())
      break;
    string::iterator q = p;
    while(p != move.end() && *p != ' ' && *p != '\n' && *p != '\t')
      p++;
    string name = string(q, p);
    net *n = nets[name];
    if(!n) {
      fprintf(stderr, "move: Error, net %s not given\n", name.c_str());
      exit(1);
    }
    while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
      p++;
    if(p == move.end())
      break;
    do {
      q = p;
      while(p != move.end() && *p != ' ' && *p != '\n' && *p != '\t')
	p++;
      int dx = strtol(string(q, p).c_str(), 0, 10);
      while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	p++;
      if(p == move.end())
	break;
      q = p;
      while(p != move.end() && *p != ' ' && *p != '\n' && *p != '\t' && *p != ',')
	p++;
      int dy = strtol(string(q, p).c_str(), 0, 10);

      n->add_route(point(x+dx, sy1 - (y+dy)));
      while(p != move.end() && (*p == ' ' || *p == '\n' || *p == '\t'))
	p++;
      if(p == move.end())
	break;
    } while(*p != ',');
    if(p == move.end())
      break;
    p++;
  }
  return 0;
}

int l_setup(lua_State *L)
{
  state = new State(lua_tostring(L, 2), lua_tostring(L, 1), lua_tostring(L, 3), lua_toboolean(L, 6));
  ratio = lua_tonumber(L, 5);

  sy1 = (state->info.sy-1)/ratio;

  map<int, list<ref> > nodemap;

  build_mosfets(nodes, nodemap);
  build_capacitors(nodes, nodemap);
  build_pads(lua_tostring(L, 4), nodes, nodemap);
  build_nets(nets, nodemap);
  build_power_nodes_and_nets(nodes, nets);

  for(unsigned int i=0; i != nodes.size(); i++)
    nodes[i]->refine_position();
  return 0;
}

int l_text(lua_State *L)
{
  opt_text = lua_tostring(L, 1);
  return 0;
}

int l_svg(lua_State *L)
{
  opt_svg = lua_tostring(L, 1);
  return 0;
}

int l_tiles(lua_State *L)
{
  opt_tiles = lua_tostring(L, 1);
  return 0;
}

int luaopen_mschem(lua_State *L)
{
  static const luaL_Reg mschem_l[] = {
    { "nodes_rect",  l_nodes_rect  },
    { "nodes_trace", l_nodes_trace },
    { "make_match",  l_make_match  },
    { "match",       l_match       },
    { "move",        l_move        },
    { "route",       l_route       },
    { "named_net",   l_named_net   },
    { "setup",       l_setup       },
    { "text",        l_text        },
    { "svg",         l_svg         },
    { "tiles",       l_tiles       },

    { }
  };
  lua_getglobal(L, "_G");
  luaL_setfuncs(L, mschem_l, 0);
  return 1;
}

static const luaL_Reg lualibs[] = {
  { "_G",            luaopen_base        },
  { LUA_LOADLIBNAME, luaopen_package     },
  { LUA_TABLIBNAME,  luaopen_table       },
  { LUA_IOLIBNAME,   luaopen_io          },
  { LUA_OSLIBNAME,   luaopen_os          },
  { LUA_STRLIBNAME,  luaopen_string      },
  { LUA_MATHLIBNAME, luaopen_math        },
  { LUA_DBLIBNAME,   luaopen_debug       },
  { "mschem",        luaopen_mschem      },
  { "mosfet",        mosfet::luaopen     },
  { "capacitor",     capacitor::luaopen  },
  { "power_node",    power_node::luaopen },
  { "pad",           pad::luaopen        },
  { "net",           net::luaopen        },
  { }
};

void lua_fun(const char *fname, vector<node *> &nodes, vector<net *> &nets)
{
  lua_State *L = luaL_newstate();

  for(const luaL_Reg *lib = lualibs; lib->func; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1);
  }

  if(luaL_loadfile(L, fname)) {
    fprintf(stderr, "Error loading %s: %s\n", fname, lua_tostring(L, -1));
    exit(1);
  }
  if(lua_pcall(L, 0, 0, 0)) {
    fprintf(stderr, "Error: %s\n", lua_tostring(L, -1));
    exit(1);
  }
}

int main(int argc, char **argv)
{
  ratio = 1;
  opt_text = opt_svg = opt_tiles = NULL;

  freetype_init();

  lua_fun(argv[1], nodes, nets);

  build_net_links(nets);

  if(opt_svg) {
    FILE *fd = svg_open(opt_svg, state->info.sx / ratio * 10, state->info.sy / ratio * 10);
    for(unsigned int i=0; i != nodes.size(); i++)
      nodes[i]->to_svg(fd);
    for(unsigned int i=0; i != nets.size(); i++)
      nets[i]->to_svg(fd);

    svg_close(fd);
  }

  if(opt_text)
    save_txt(opt_text, state->info.sx / ratio, state->info.sy / ratio, nodes, nets);

  if(opt_tiles) {
    char buf[4096];
    sprintf(buf, "%s/%%d/y%%03d_x%%03d.png", opt_tiles);
    draw(buf, nodes, nets);
  }

  return 0;
}
