#undef _FORTIFY_SOURCE

#include "state.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <map>
#include <math.h>
#include <reader.h>
#include <limits.h>

#include <ft2build.h>
#include FT_FREETYPE_H

const unsigned char *trace_data;
int trace_size;

struct cglyph {
  int sx, sy, dx, dy, left, top;
  unsigned char *image;
};

static FT_Library flib;
static FT_Face face;
static double cached_size, cached_rot;
static map<int, cglyph> cached_glyphs;

static void bitmap_blend_8_32(unsigned int *ids, int dest_w, int dest_h, const unsigned char *src, int src_w, int src_h, int x, int y, int z, unsigned int id)
{
  if(z != 1)
    return;

  int w = (x+src_w > dest_w ? dest_w : x+src_w) - (x < 0 ? 0 : x);
  int h = (y+src_h > dest_h ? dest_h : y+src_h) - (y < 0 ? 0 : y);
  if(x > 0)
    ids += x;
  else
    src -= x;

  if(y > 0)
    ids += dest_w*y;
  else
    src -= src_w*y;

  for(int yy=0; yy<h; yy++) {
    unsigned int *i1 = ids;
    const unsigned char *s1 = src;
    for(int xx=0; xx<w; xx++) {
      if(*s1 < 0x80)
	*i1 = id;
      s1++;
      i1++;
    }
    ids += dest_w;
    src += src_w;
  }
}

void freetype_init()
{
  FT_Init_FreeType(&flib);
  #ifdef _WIN32
    if(FT_New_Face(flib, "C:\\Windows\\Fonts\\times.ttf", 0, &face)) {
      fprintf(stderr, "Font opening error- Is times.ttf in C:\\Windows\\Fonts?\n");
      exit(1);
    }
  #else
    if(FT_New_Face(flib, "/usr/share/fonts/corefonts/times.ttf", 0, &face)) {
      fprintf(stderr, "Font opening error\n");
      exit(1);
    }
  #endif
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

static unsigned int *base(unsigned int *ids, int ox, int oy, int w, int h, int x, int y)
{
  return ids + (y-oy)*w + (x-ox);
}

static void hline(unsigned int *ids, int ox, int oy, int w, int h, int z, int x1, int x2, int y, unsigned int id)
{
  ox /= z;
  oy /= z;
  x1 /= z;
  x2 /= z;
  y /= z;
  if(y < oy || y >= oy + h)
    return;
  if(x2 < ox || x1 >= ox + w)
    return;
  if(x1 < ox)
    x1 = ox;
  if(x2 >= ox + w)
    x2 = ox + w - 1;
  ids = base(ids, ox, oy, w, h, x1, y);
  for(int x=x1; x<=x2; x++)
    *ids++ = id;
}

static void vline(unsigned int *ids, int ox, int oy, int w, int h, int z, int x, int y1, int y2, unsigned int id)
{
  ox /= z;
  oy /= z;
  x /= z;
  y1 /= z;
  y2 /= z;
  if(x < ox || x >= ox + w)
    return;
  if(y2 < oy || y1 >= oy + h)
    return;
  if(y1 < oy)
    y1 = oy;
  if(y2 >= oy + h)
    y2 = oy + h - 1;
  unsigned int *p = base(ids, ox, oy, w, h, x, y1);
  for(int y=y1; y <= y2; y++) {
    *p = id;
    p += w;
  }
}

// ...###...
// ..#...#..
// .#.....#.
// #.......#
// #.......#
// #.......#
// .#.....#.
// ..#...#..
// ...###...

// .###.
// #...#
// #...#
// #...#
// .###.

// ###
// #.#
// ###

static void invert(unsigned int *ids, int ox, int oy, int w, int h, int z, int x, int y, unsigned int id)
{
  static const int pt_1[] = { 0, 4, 1, 4, 2, 3, 3, 2, 4, 1, 4, 0, 4, -1, 3, -2, 2, -3, 1, -4, 0, -4, -1, -4, -2, -3, -3, -2, -4, -1, -4, 0, -4, 1, -3, 2, -2, 3, -1, 4 };
  static const int pt_2[] = { 0, 2, 1, 2, 2, 1, 2, 0, 2, -1, 1, -2, 0, -2, -1, -2, -2, -1, -2, 0, -2, 1, -1, 2 };
  static const int pt_4[] = { 0, 1, 1, 1, 1, 0, 1, -1, 0, -1, -1, -1, -1, 0, -1, 1 };
  static const int pt_8[] = { 0, 0 };
  ox /= z;
  oy /= z;
  x  /= z;
  y  /= z;
  const int *ptlist;
  int ptcount;
  switch(z) {
  case 1:
    if(x < ox-4 || x >= ox + w + 4)
      return;
    if(y < oy-4 || y >= oy + h + 4)
      return;
    ptlist = pt_1;
    ptcount = sizeof(pt_1)/2/sizeof(int);
    break;
  case 2:
    if(x < ox-2 || x >= ox + w + 2)
      return;
    if(y < oy-2 || y >= oy + h + 2)
      return;
    ptlist = pt_2;
    ptcount = sizeof(pt_2)/2/sizeof(int);
    break;
  case 4:
    if(x < ox-1 || x >= ox + w + 1)
      return;
    if(y < oy-1 || y >= oy + h + 1)
      return;
    ptlist = pt_4;
    ptcount = sizeof(pt_4)/2/sizeof(int);
    break;
  default:
    if(x < ox || x >= ox + w)
      return;
    if(y < oy || y >= oy + h)
      return;
    ptlist = pt_8;
    ptcount = sizeof(pt_8)/2/sizeof(int);
    break;
  }
  for(int i=0; i != ptcount; i++) {
    int xx = x + ptlist[2*i];
    int yy = y + ptlist[2*i+1];
    if(xx < ox || xx >= ox + w)
      continue;
    if(yy < oy || yy >= oy + h)
      continue;
    *base(ids, ox, oy, w, h, xx, yy) = id;
  }
}

static void rect(unsigned int *ids, int ox, int oy, int w, int h, int z, int x1, int y1, int x2, int y2, unsigned int id)
{
  ox /= z;
  oy /= z;
  x1 /= z;
  x2 /= z;
  y1 /= z;
  y2 /= z;
  if(x2 < ox || x1 >= ox + w)
    return;
  if(y2 < oy || y1 >= oy + h)
    return;
  if(x1 < ox)
    x1 = ox;
  if(y1 < oy)
    y1 = oy;
  if(x2 >= ox + w)
    x2 = ox + w - 1;
  if(y2 >= oy + h)
    y2 = oy + h - 1;
  int sx = x2-x1+1;
  unsigned int *p = base(ids, ox, oy, w, h, x1, y1);
  for(int y=y1; y <= y2; y++) {
    for(int i=0; i<sx; i++)
      p[i] = id;
    p += w;
  }
}

static void line(unsigned int *ids, int ox, int oy, int w, int h, int z, int x1, int y1, int x2, int y2, unsigned int id)
{
  if(x1/z == x2/z) {
    if(y1 < y2)
      vline(ids, ox, oy, w, h, z, x1, y1, y2, id);
    else
      vline(ids, ox, oy, w, h, z, x1, y2, y1, id);
    return;
  }
  if(y1/z == y2/z) {
    if(x1 < x2)
      hline(ids, ox, oy, w, h, z, x1, x2, y1, id);
    else
      hline(ids, ox, oy, w, h, z, x2, x1, y1, id);
    return;
  }

  ox /= z;
  oy /= z;
  x1 /= z;
  x2 /= z;
  y1 /= z;
  y2 /= z;

  if(x1 < ox && x2 < ox)
    return;
  if(y1 < oy && y2 < oy)
    return;
  if(x1 >= ox + w && x2 >= ox + w)
    return;
  if(y1 >= oy + h && y2 >= oy + h)
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
    unsigned int *ip;
    unsigned long cpx;
    unsigned int px;
    if(y1 < oy) {
      cpx = (((unsigned long)x1) << 24) + 0x800000;
      cpx += dx*((oy-y1)*2-1)/2;
      px = (cpx - 0x7fffff) >> 24;
      ip = ids;
      y1 = 0;
    } else {
      cpx = (((unsigned long)x1) << 24) + 0x800000;
      cpx -= dx/2;
      px = x1;
      ip = ids + w*(y1-oy);
      y1 -= oy;
    }
    y2 -= oy;
    if(y2 > h)
      y2 = h;

    for(int y=y1; y != y2; y++) {
      cpx += dx;
      unsigned int nx = (cpx - 0x7fffff) >> 24;
      if(px > nx)
	px = nx;
      int xx1 = px - ox;
      int xx2 = nx - ox;
      if(xx1 >= w)
	return;
      if(xx2 >= 0) {
	if(xx1 < 0)
	  xx1 = 0;
	if(xx2 >= w)
	  xx2 = w-1;
	for(int x = xx1; x <= xx2; x++)
	  ip[x] = id;
      }
      px = nx+1;
      ip += w;
    }
    if(y2 == h)
      return;
    cpx += dx/2;
    unsigned int nx = (cpx - 0x7fffff) >> 24;
    if(px > nx)
      px = nx;
    int xx1 = px - ox;
    int xx2 = nx - ox;
    if(xx1 >= w)
      return;
    if(xx2 < 0)
      return;
    if(xx1 < 0)
      xx1 = 0;
    if(xx2 >= w)
      xx2 = w-1;
    for(int x = xx1; x <= xx2; x++)
      ip[x] = id;

  } else {
    unsigned long dx = (((unsigned long)(x1-x2)) << 24) / (y2 - y1);
    unsigned int *ip;
    unsigned long cpx;
    unsigned int px;
    if(y1 < oy) {
      cpx = (((unsigned long)x1) << 24) + 0x800000;
      cpx -= dx*((oy-y1)*2-1)/2;
      px = (cpx + 0x800000) >> 24;
      ip = ids;
      y1 = 0;
    } else {
      cpx = (((unsigned long)x1) << 24) + 0x800000;
      cpx += dx/2;
      px = x1;
      ip = ids + w*(y1-oy);
      y1 -= oy;
    }
    y2 -= oy;
    if(y2 > h)
      y2 = h;

    for(int y=y1; y != y2; y++) {
      cpx -= dx;
      unsigned int nx = (cpx + 0x800000) >> 24;
      if(px < nx)
	px = nx;
      int xx1 = nx - ox;
      int xx2 = px - ox;
      if(xx2 < 0)
	return;
      if(xx1 < w) {
	if(xx1 < 0)
	  xx1 = 0;
	if(xx2 >= w)
	  xx2 = w-1;
	for(int x = xx1; x <= xx2; x++)
	  ip[x] = id;
      }
      px = nx-1;
      ip += w;
    }
    if(y2 == h)
      return;
    cpx -= dx/2;
    unsigned int nx = (cpx + 0x800000) >> 24;
    if(px < nx)
      px = nx;
    int xx1 = nx - ox;
    int xx2 = px - ox;
    if(xx2 < 0)
      return;
    if(xx1 >= w)
      return;
    if(xx1 < 0)
      xx1 = 0;
    if(xx2 >= w)
      xx2 = w-1;
    for(int x = xx1; x <= xx2; x++)
      ip[x] = id;
  }
}

vector<node *> nodes;
vector<net *> nets;

SVMain *svmain;
const char *schem_file;

void state_load(const char *fname)
{
  state.save();
  reader rd(fname);
  if(!nodes.empty()) {
    for(unsigned int i=0; i != nodes.size(); i++)
      delete nodes[i];
    nodes.clear();
  }
  if(!nets.empty()) {
    for(unsigned int i=0; i != nets.size(); i++)
      delete nets[i];
    nets.clear();
  }
  state.sx = rd.gi();
  state.sy = rd.gi();
  state.ratio = rd.gd();
  rd.nl();

  int nn = rd.gi();
  rd.nl();
  nodes.resize(nn);
  int tmap[256];
  memset(tmap, 0xff, 256*4);
  tmap['t'] = node::T;
  tmap['d'] = node::D;
  tmap['i'] = node::I;
  tmap['v'] = node::V;
  tmap['g'] = node::G;
  tmap['p'] = node::P;
  tmap['c'] = node::C;
  for(int i=0; i != nn; i++) {
    node *n = new node();
    const char *ts = rd.gw();
    int type = tmap[(unsigned char)(ts[0])];
    if(type == -1) {
      fprintf(stderr, "Unknown type %s\n", ts);
      exit(1);
    }
    n->id = node::NODE_MARK | i;
    n->type = type;
    n->x = rd.gi()*10;
    n->y = (state.sy-rd.gi())*10;
    n->netids[node::T1] = rd.gi();
    if(type == node::T || type == node::D || type == node::I) {
      n->netids[node::GATE] = rd.gi();
      n->netids[node::T2] = rd.gi();
      n->f = rd.gd();
    } else if(type == node::C) {
      n->netids[node::T2] = rd.gi();
      n->f = rd.gd();
      n->netids[node::GATE] = -1;
    } else {
      n->netids[node::T2] = n->netids[node::GATE] = -1;
      n->f = 0;
    }

    n->orientation = type != node::V && type != node::G ? rd.gi() : 0;
    n->name = rd.gwnl();
    rd.nl();
    n->bbox();
    nodes[i] = n;
  }

  nn = rd.gi();
  rd.nl();
  nets.resize(nn);
  for(int i=0; i != nn; i++) {
    net *n = new net();
    n->id = node::NET_MARK | i;
    int nx = rd.gi();
    n->pt.resize(nx);
    for(int j=0; j != nx; j++) {
      n->pt[j].x = rd.gi()*10;
      n->pt[j].y = (state.sy-rd.gi())*10;
    }
    nx = rd.gi();
    n->lines.resize(nx);
    for(int j=0; j != nx; j++) {
      n->lines[j].p1 = rd.gi();
      n->lines[j].p2 = rd.gi();
    }
    nx = rd.gi();
    n->dots.resize(nx);
    for(int j=0; j != nx; j++)
      n->dots[j] = rd.gi();
    n->name = rd.gwnl();
    rd.nl();
    nets[i] = n;
  }
  for(unsigned int i=0; i != nodes.size(); i++) {
    node *n = nodes[i];
    for(int j=0; j<3; j++)
      if(n->netids[j] == -1)
	n->nets[j] = NULL;
      else
	n->nets[j] = nets[n->netids[j]];
  }
  state.trace_pos = 0;
  state.reload();
}

void node::bbox()
{
  switch(type) {
  case T: case D: case I:
    x0 = x-40;
    x1 = x+40;
    y0 = y-40;
    y1 = y+40;
    break;

  case G: case V:
    x0 = x-10;
    x1 = x+10;
    y0 = y-15;
    y1 = y+16;
    break;

  case P:
    x0 = x-240;
    x1 = x+240;
    y0 = y-240;
    y1 = y+240;
    freetype_render(name.c_str(), 6, 0, name_width, name_height, name_image);
    break;

  case C:
    x0 = x-10;
    x1 = x+10;
    y0 = y-10;
    y1 = y+10;
    break;

  default:
    abort();
  }
}

void node::draw(unsigned int *ids, int ox, int oy, int w, int h, int z) const
{
  if(ox > x1 || oy > y1 || x0 >= ox+z*w || y0 >= oy+z*h)
    return;

  switch(type) {
  case T: case D: case I:
    draw_mosfet(ids, ox, oy, w, h, z);
    break;
  case G:
    draw_gnd(ids, ox, oy, w, h, z);
    break;
  case V:
    draw_vcc(ids, ox, oy, w, h, z);
    break;
  case P:
    draw_pad(ids, ox, oy, w, h, z);
    break;
  case C:
    draw_capacitor(ids, ox, oy, w, h, z);
    break;
  }
}

void node::draw_mosfet(unsigned int *ids, int ox, int oy, int w, int h, int z) const
{
  switch(orientation & 3) {
  case W_S:
    vline(ids, ox, oy, w, h, z, x+10, y-40, y-20, nets[orientation == W_S ? T2 : T1]->id);
    hline(ids, ox, oy, w, h, z, x, x+10, y-20, id);
    vline(ids, ox, oy, w, h, z, x, y-20, y+20, id);
    hline(ids, ox, oy, w, h, z, x, x+10, y+20, id);
    vline(ids, ox, oy, w, h, z, x+10, y+20, y+40, nets[orientation == W_S ? T1 : T2]->id);
    hline(ids, ox, oy, w, h, z, x-40, type == I ? x-19 : x-10, y, nets[GATE]->id);
    vline(ids, ox, oy, w, h, z, x-10, y-20, y+20, id);
    if(type == I)
      invert(ids, ox, oy, w, h, z, x-15, y, id);
    if(type == D)
      rect(ids, ox, oy, w, h, z, x, y-20, x+4, y+20, id);
    break;

  case E_S:
    vline(ids, ox, oy, w, h, z, x-10, y-40, y-20, nets[orientation == E_S ? T2 : T1]->id);
    hline(ids, ox, oy, w, h, z, x-10, x, y-20, id);
    vline(ids, ox, oy, w, h, z, x, y-20, y+20, id);
    hline(ids, ox, oy, w, h, z, x-10, x, y+20, id);
    vline(ids, ox, oy, w, h, z, x-10, y+20, y+40, nets[orientation == E_S ? T1 : T2]->id);
    hline(ids, ox, oy, w, h, z, type == I ? x+19 : x+10, x+40, y, nets[GATE]->id);
    vline(ids, ox, oy, w, h, z, x+10, y-20, y+20, id);
    if(type == I)
      invert(ids, ox, oy, w, h, z, x+15, y, id);
    if(type == D)
      rect(ids, ox, oy, w, h, z, x-4, y-20, x, y+20, id);
    break;

  case N_S:
    hline(ids, ox, oy, w, h, z, x-40, x-20, y+10, nets[orientation == N_S ? T1 : T2]->id);
    vline(ids, ox, oy, w, h, z, x-20, y, y+10, id);
    hline(ids, ox, oy, w, h, z, x-20, x+20, y, id);
    vline(ids, ox, oy, w, h, z, x+20, y, y+10, id);
    hline(ids, ox, oy, w, h, z, x+20, x+40, y+10, nets[orientation == N_S ? T2 : T1]->id);
    vline(ids, ox, oy, w, h, z, x, y-40, type == I ? y-19 : y-10, nets[GATE]->id);
    hline(ids, ox, oy, w, h, z, x-20, x+20, y-10, id);
    if(type == I)
      invert(ids, ox, oy, w, h, z, x, y-15, id);
    if(type == D)
      rect(ids, ox, oy, w, h, z, x-20, y, x+20, y+4, id);
    break;

  case S_S:
    hline(ids, ox, oy, w, h, z, x-40, x-20, y-10, nets[orientation == S_S ? T1 : T2]->id);
    vline(ids, ox, oy, w, h, z, x-20, y-10, y, id);
    hline(ids, ox, oy, w, h, z, x-20, x+20, y, id);
    vline(ids, ox, oy, w, h, z, x+20, y-10, y, id);
    hline(ids, ox, oy, w, h, z, x+20, x+40, y-10, nets[orientation == S_S ? T2 : T1]->id);
    vline(ids, ox, oy, w, h, z, x, type == I ? y+19 : y+10, y+40, nets[GATE]->id);
    hline(ids, ox, oy, w, h, z, x-20, x+20, y+10, id);
    if(type == I)
      invert(ids, ox, oy, w, h, z, x, y+15, id);
    if(type == D)
      rect(ids, ox, oy, w, h, z, x-20, y-4, x+20, y, id);
    break;
  }
}

void node::draw_gnd(unsigned int *ids, int ox, int oy, int w, int h, int z) const
{
  vline(ids, ox, oy, w, h, z, x, y, y+10, nets[T1]->id);
  hline(ids, ox, oy, w, h, z, x-10, x+10, y+10, id);
  hline(ids, ox, oy, w, h, z, x-7, x+7, y+12, id);
  hline(ids, ox, oy, w, h, z, x-4, x+4, y+14, id);
  hline(ids, ox, oy, w, h, z, x-1, x+1, y+16, id);
}

void node::draw_vcc(unsigned int *ids, int ox, int oy, int w, int h, int z) const
{
  vline(ids, ox, oy, w, h, z, x, y-10, y, nets[T1]->id);
  rect(ids, ox, oy, w, h, z, x-10, y-15, x+10, y-10, id);
}

void node::draw_pad(unsigned int *ids, int ox, int oy, int w, int h, int z) const
{
  int tx = x, ty = y;
  switch(orientation & 3) {
  case W_S:
    hline(ids, ox, oy, w, h, z, x-40, x, y, nets[T1]->id);
    hline(ids, ox, oy, w, h, z, x-240, x-40, y-100, id);
    hline(ids, ox, oy, w, h, z, x-240, x-40, y+100, id);
    vline(ids, ox, oy, w, h, z, x-40, y-100, y+100, id);
    vline(ids, ox, oy, w, h, z, x-240, y-100, y+100, id);
    tx = x-140;
    ty = y;
    break;
  case E_S:
    hline(ids, ox, oy, w, h, z, x, x+40, y, nets[T1]->id);
    hline(ids, ox, oy, w, h, z, x+40, x+240, y-100, id);
    hline(ids, ox, oy, w, h, z, x+40, x+240, y+100, id);
    vline(ids, ox, oy, w, h, z, x+40, y-100, y+100, id);
    vline(ids, ox, oy, w, h, z, x+240, y-100, y+100, id);
    tx = x+140;
    ty = y;
    break;
  case N_S:
    vline(ids, ox, oy, w, h, z, x, y-40, y, nets[T1]->id);
    vline(ids, ox, oy, w, h, z, x-100, y-240, y-40, id);
    vline(ids, ox, oy, w, h, z, x+100, y-240, y-40, id);
    hline(ids, ox, oy, w, h, z, x-100, x+100, y-40, id);
    hline(ids, ox, oy, w, h, z, x-100, x+100, y-240, id);
    tx = x;
    ty = y-140;
    break;
  case S_S:
    vline(ids, ox, oy, w, h, z, x, y, y+40, nets[T1]->id);
    vline(ids, ox, oy, w, h, z, x-100, y+40, y+240, id);
    vline(ids, ox, oy, w, h, z, x+100, y+40, y+240, id);
    hline(ids, ox, oy, w, h, z, x-100, x+100, y+40, id);
    hline(ids, ox, oy, w, h, z, x-100, x+100, y+240, id);
    tx = x;
    ty = y+140;
    break;
  }

  bitmap_blend_8_32(ids, w, h, name_image, name_width, name_height, tx-name_width/2-ox, ty-name_height/2-oy, z, id);
}

void node::draw_capacitor(unsigned int *ids, int ox, int oy, int w, int h, int z) const
{
  switch(orientation & 3) {
  case W_S:
    vline(ids, ox, oy, w, h, z, x-2, y-10, y+10, id);
    vline(ids, ox, oy, w, h, z, x+2, y-10, y+10, id);
    hline(ids, ox, oy, w, h, z, x-10, x-2, y, nets[orientation == W_S ? T1 : T2]->id);
    hline(ids, ox, oy, w, h, z, x+2, x+10, y, nets[orientation == W_S ? T2 : T1]->id);
    break;

  case N_S:
    hline(ids, ox, oy, w, h, z, x-10, x+10, y-2, id);
    hline(ids, ox, oy, w, h, z, x-10, x+10, y+2, id);
    vline(ids, ox, oy, w, h, z, x, y-10, y-2, nets[orientation == N_S ? T2 : T1]->id);
    vline(ids, ox, oy, w, h, z, x, y+2, y+10, nets[orientation == N_S ? T1 : T2]->id);
    break;
  }
}

void net::draw(unsigned int *ids, int ox, int oy, int w, int h, int z) const
{
  for(unsigned int i=0; i != lines.size(); i++) {
    int p1 = lines[i].p1;
    int p2 = lines[i].p2;
    ::line(ids, ox, oy, w, h, z, pt[p1].x, pt[p1].y, pt[p2].x, pt[p2].y, id);
  }
  for(unsigned int i=0; i != dots.size(); i++) {
    int p = dots[i];
    int x = pt[p].x;
    int y = pt[p].y;
    hline(ids, ox, oy, w, h, z, x-2, x+2, y-3, id);
    hline(ids, ox, oy, w, h, z, x-3, x+3, y-2, id);
    hline(ids, ox, oy, w, h, z, x-4, x+4, y-1, id);
    hline(ids, ox, oy, w, h, z, x-4, x+4, y,   id);
    hline(ids, ox, oy, w, h, z, x-4, x+4, y+1, id);
    hline(ids, ox, oy, w, h, z, x-3, x+3, y+2, id);
    hline(ids, ox, oy, w, h, z, x-2, x+2, y+3, id);
  }
}

string id_to_name(unsigned int id)
{
  switch(id & node::TYPE_MASK) {
  case node::NODE_MARK:
    return nodes[id & node::ID_MASK]->name;
  case node::NET_MARK:
    return nets[id & node::ID_MASK]->name;
  }
  return "";
}

state_t state;

state_t::state_t()
{
  register_solvers();
}

void state_t::save()
{
}

void state_t::load_trace()
{
  int nn = nets.size();
  int fs = (nn+7)/8;
  int max_trace = trace_size/fs;
  if(trace_size < fs)
    return;
  if(trace_pos >= max_trace)
    trace_pos = max_trace-1;
  const unsigned char *tp = trace_data + fs*trace_pos;
  for(int i=0; i != nn; i++)
    power[i] = (tp[i >> 3] & (1 << (i & 7))) ? 50 : 0;
}

void state_t::trace_next()
{
  trace_pos++;
  load_trace();
}

void state_t::trace_prev()
{
  if(trace_pos) {
    trace_pos--;
    load_trace();
  }
}

void state_t::trace_info(int &tp, int &ts)
{
  tp = trace_pos;
  int nn = nets.size();
  int fs = (nn+7)/8;
  ts = trace_size/fs;
}

void state_t::reload()
{
  build();

  set<int> changed;
  for(int i=0; i != int(nets.size()); i++)
    if(power[i])
      changed.insert(i);
  apply_changed(changed);

  if(trace_data)
    load_trace();
}

void state_t::build()
{
  int nn = nets.size();
  int nt = nodes.size();
  selectable_net.resize(nn);
  highlight.resize(nn);
  power.resize(nn);
  is_fixed.resize(nn);
  fixed_level.resize(nn);
  delay.resize(nn);

  term_to_trans.resize(nn);
  gate_to_trans.resize(nn);

  // Default initialisation to 0V, 10 units delay, selectable, not highlighted, not fixed level
  for(int i=0; i != nn; i++) {
    selectable_net[i] = true;
    highlight[i] = false;
    is_fixed[i] = false;
    fixed_level[i] = 0;
    power[i] = 0;
    delay[i] = 10;
  }
  
  // Mark vcc/gnd nets
  for(int i=0; i != nt; i++)
    if(nodes[i]->type == node::V || nodes[i]->type == node::G) {
      int id = nodes[i]->netids[node::T1];
      selectable_net[id] = false;
      is_fixed[id] = true;
      fixed_level[id] = power[id] = nodes[i]->type == node::V ? 50 : 0;
      fixed_level[id] = power[id] = 0;
    }

  // Add the capacities induced delays
  for(int i=0; i != nt; i++)
    if(nodes[i]->type == node::C) {
      node *c = nodes[i];
      delay[c->netids[node::T1]] += c->f;
      delay[c->netids[node::T2]] += c->f;
    }

  // Build the lookup tables
  for(int i=0; i != nt; i++)
    if(nodes[i]->type == node::T || nodes[i]->type == node::D || nodes[i]->type == node::I) {
      node *m = nodes[i];
      gate_to_trans[m->netids[node::GATE]].push_back(i);
      term_to_trans[m->netids[node::T1]].push_back(i);
      term_to_trans[m->netids[node::T2]].push_back(i);
   }
}

void state_t::add_net(int nid, vector<int> &nids, set<int> &nid_set, set<int> &changed, set<node *> &accepted_trans, map<int, list<node *> > &rejected_trans_per_gate)
{
  if(nid_set.find(nid) == nid_set.end()) {
    nids.push_back(nid);
    nid_set.insert(nid);
    set<int>::iterator ci = changed.find(nid);
    if(ci != changed.end())
      changed.erase(ci);
    map<int, list<node *> >::iterator j = rejected_trans_per_gate.find(nid);
    if(j != rejected_trans_per_gate.end()) {
      list<node *> n = j->second;
      rejected_trans_per_gate.erase(j);
      for(list<node *>::const_iterator k = n.begin(); k != n.end(); k++)
	add_transistor(*k, nids, nid_set, changed, accepted_trans, rejected_trans_per_gate);
    }
  }
}

void state_t::add_transistor(node *tr, vector<int> &nids, set<int> &nid_set, set<int> &changed, set<node *> &accepted_trans, map<int, list<node *> > &rejected_trans_per_gate)
{
  accepted_trans.insert(tr);
  add_net(tr->netids[node::T1], nids, nid_set, changed, accepted_trans, rejected_trans_per_gate);
  add_net(tr->netids[node::T2], nids, nid_set, changed, accepted_trans, rejected_trans_per_gate);
}

void state_t::build_equation(string &equation, vector<int> &constants, const vector<int> &nids_to_solve, const vector<int> &levels, const set<node *> &accepted_trans, const map<int, int> &nid_to_index) const
{
  constants.clear();
  equation = "";
  for(set<node *>::const_iterator j = accepted_trans.begin(); j != accepted_trans.end(); j++) {
    node *tr = *j;
    int nt1 = tr->netids[node::T1];
    int ng  = tr->netids[node::GATE];
    int nt2 = tr->netids[node::T2];

    map<int, int>::const_iterator k;

    k = nid_to_index.find(nt1);
    int t1_id   = k == nid_to_index.end() ? -1 : k->second;
    k = nid_to_index.find(ng);
    int gate_id = k == nid_to_index.end() ? -1 : k->second;
    k = nid_to_index.find(nt2);
    int t2_id   = k == nid_to_index.end() ? -1 : k->second;

    int pnt1 = t1_id   == -1 ? power[nt1] : levels[t1_id];
    int png  = gate_id == -1 ? power[ng]  : levels[gate_id];
    int pnt2 = t2_id   == -1 ? power[nt2] : levels[t2_id];

    constants.push_back(int(tr->f*1000));
    if(!equation.empty())
      equation += ' ';
    if(tr->type == node::T)
      equation += 'T';
    else if(tr->type == node::I)
      equation += 'I';
    else
      equation += 'D';

    if(t2_id != -1 && (t1_id == -1 || t1_id > t2_id)) {
      // Invert t2 and t1
      equation += i2v(t2_id);
      equation += gate_id == -1 ? "." : i2v(gate_id);
      equation += t1_id == -1 ? "." : i2v(t1_id);
      if(gate_id == -1)
	constants.push_back(png);
      if(t1_id == -1)
	constants.push_back(pnt1);
    } else {
      equation += t1_id == -1 ? "." : i2v(t1_id);
      equation += gate_id == -1 ? "." : i2v(gate_id);
      equation += t2_id == -1 ? "." : i2v(t2_id);
      if(t1_id == -1)
	constants.push_back(pnt1);
      if(gate_id == -1)
	constants.push_back(png);
      if(t2_id == -1)
	constants.push_back(pnt2);
    }
  }
}

void state_t::minmax(int &minv, int &maxv, int value)
{
  if(value < minv)
    minv = value;
  if(value > maxv)
    maxv = value;
}

string state_t::i2v(int id)
{
  char buf[32];
  if(id < 26) {
    buf[0] = 'a'+id;
    buf[1] = 0;
  } else if(id < 52) {
    buf[0] = 'A'+(id-26);
    buf[1] = 0;
  } else
    sprintf(buf, "<%d>", id-52);
  return buf;
}

void state_t::apply_changed(set<int> changed)
{
  int ctime = 0;
  map<int, map<int, int> > future_changes;
  map<int, int> future_changes_times;
  while(!changed.empty()) {
    list<int> changed_through_gate;
    for(set<int>::const_iterator i = changed.begin(); i != changed.end(); i++)
      for(vector<int>::const_iterator j = gate_to_trans[*i].begin(); j != gate_to_trans[*i].end(); j++) {
	changed_through_gate.push_back(nodes[*j]->netids[node::T1]);
	changed_through_gate.push_back(nodes[*j]->netids[node::T2]);
      }
    for(list<int>::const_iterator i = changed_through_gate.begin(); i != changed_through_gate.end(); i++)
      changed.insert(*i);

    while(!changed.empty()) {
      bool verb = false;
      vector<int> nids;
      set<int> nid_set;
      set<node *> accepted_trans;
      map<int, list<node *> > rejected_trans_per_gate;
      int nid = *changed.begin();
      changed.erase(changed.begin());
      nids.push_back(nid);
      nid_set.insert(nid);
      for(int nididx=0; nididx != int(nids.size()); nididx++) {
	int nid = nids[nididx];
	for(vector<int>::const_iterator i = term_to_trans[nid].begin(); i != term_to_trans[nid].end(); i++) {
	  node *tr = nodes[*i];
	  int nt1 = tr->netids[node::T1];
	  int nt2 = tr->netids[node::T2];
	  int ng = tr->netids[node::GATE];
	  int thr = power[ng] - (tr->type == node::T ? ET : ED);
	  if(nid_set.find(ng) != nid_set.end() || thr-power[nt1] > 0 || thr-power[nt2] > 0)
	    add_transistor(tr, nids, nid_set, changed, accepted_trans, rejected_trans_per_gate);
	  else
	    rejected_trans_per_gate[ng].push_back(tr);
	}
      }

      vector<int> nids_to_solve;
      for(vector<int>::const_iterator i = nids.begin(); i != nids.end(); i++)
	if(!is_fixed[*i]) {
	  assert(nets[*i]->name != "gnd" && nets[*i]->name != "vcc");
	  nids_to_solve.push_back(*i);
	}
      if(!nids_to_solve.empty() && !accepted_trans.empty()) {
	int minv = INT_MAX, maxv = INT_MIN;
	for(set<node *>::const_iterator j = accepted_trans.begin(); j != accepted_trans.end(); j++) {
	  node *tr = *j;
	  int nt1 = tr->netids[node::T1];
	  int nt2 = tr->netids[node::T2];
	  int ng = tr->netids[node::GATE];
	  minmax(minv, maxv, power[nt1]);
	  minmax(minv, maxv, power[nt2]);
	  minmax(minv, maxv, power[ng]);
	}

	map<int, int> nid_to_index;
	for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	  nid_to_index[nids_to_solve[i]] = i;

	vector<int> levels;
	levels.resize(nids_to_solve.size());
	for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	  levels[i] = power[nids_to_solve[i]];

	string equation;
	vector<int> constants;
	build_equation(equation, constants, nids_to_solve, levels, accepted_trans, nid_to_index);
	if(equation == "Ta.. Daa. Taab")
	  verb = true;
	map<string, void (*)(const vector<int> &constants, vector<int> &level)>::const_iterator sp = solvers.find(equation);
	if(sp == solvers.end()) {
	  printf("Unhandled equation system.\n");
	  dump_equation_system(equation, constants, nids_to_solve, accepted_trans);
	  //	  for(unsigned int i=0; i != nids_to_solve.size(); i++)
	  //	    highlight[nids_to_solve[i]] = true;
	  continue;
	}

	if(verb)
	  dump_equation_system(equation, constants, nids_to_solve, accepted_trans);
	sp->second(constants, levels);
	if(verb) {
	  printf("  levels:\n");
	  for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	    printf("   %s: %d.%d\n", i2v(i).c_str(), levels[i]/10, levels[i]%10);
	}

	bool out_of_range = false;
	for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	  if(levels[i] < minv || levels[i] > maxv)
	    out_of_range = true;

	if(out_of_range) {
	  printf("Out of range %d.%d %d.%d\n", minv/10, minv%10, maxv/10, maxv%10);
	  dump_equation_system(equation, constants, nids_to_solve, accepted_trans);
	  printf("  levels:\n");
	  for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	    printf("   %s: %d.%d\n", i2v(i).c_str(), levels[i]/10, levels[i]%10);
	  for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	    levels[i] = minv;
	}

	for(unsigned int i = 0; i != nids_to_solve.size(); i++) {
	  int nid = nids_to_solve[i];
	  if(levels[i] != power[nid]) {
	    int ntime = ctime + delay[nid];
	    map<int, int>::const_iterator j = future_changes_times.find(nid);
	    if(j != future_changes_times.end())
	      ntime = j->second;
	    else
	      future_changes_times[nid] = ntime;
	    future_changes[ntime][nid] = levels[i];
	  }
	}
      }
    }

    if(!future_changes.empty()) {
      ctime = future_changes.begin()->first;
      const map<int, int> &mc = future_changes.begin()->second;
      for(map<int, int>::const_iterator i = mc.begin(); i != mc.end(); i++) {
	future_changes_times.erase(future_changes_times.find(i->first));
	power[i->first] = i->second;
	changed.insert(i->first);
	//	printf("time %6d node %s (%d) = %d.%d\n", ctime, nodes[i->first]->name.c_str(), i->first, i->second/10, i->second%10);
      }
      future_changes.erase(future_changes.begin());
    }
  }
}

string state_t::c2s(int vr, const vector<int> &constants, int pos)
{
  char buf[64];
  if(vr)
    sprintf(buf, "k%d", pos);
  else
    sprintf(buf, "%d", constants[pos]);
  return buf;
}

int state_t::get_id(string equation, unsigned int &pos)
{
  char c = equation[pos++];
  if(c == '.')
    return -1;
  if(c >= 'a' && c <= 'z')
    return c-'a';
  if(c >= 'A' && c <= 'Z')
    return c-'A'+26;
  if(c != '<')
    fprintf(stderr, "[%s] %d %c\n", equation.c_str(), pos-1, c);
  assert(c == '<');
  string r;
  while(equation[pos] != '>')
    r = r + equation[pos++];
  pos++;
  return strtol(r.c_str()+52, 0, 10);
}

void state_t::dump_equation_system(string equation, const vector<int> &constants, const vector<int> &nids_to_solve, const set<node *> &accepted_trans)
{
  printf("  key: %s\n", equation.c_str());
  printf("  fct: ");
  for(unsigned int i=0; i != equation.length(); i++) {
    char c = equation[i];
    if(c == ' ' || c == '.' || c == '<' || c == '>')
      c = '_';
    else if(c == '+')
      c = 'p';
    else if(c == '-')
      c = 'm';

    printf("%c", c);
  }
  printf("\n");
  printf("  nets:");
  for(unsigned int i=0; i != nids_to_solve.size(); i++)
    printf(" %s", nets[nids_to_solve[i]]->name.c_str());
  printf("\n");  

  for(int vr=0; vr<2; vr++) {
    set<node *>::const_iterator aci = accepted_trans.begin();
    if(vr)
      printf("  mosfets k:\n");
    else
      printf("  mosfets inst:\n");
    unsigned int pos = 0, cpos = 0;
    while(pos != equation.size()) {
      printf("   ");
      char type = equation[pos++];
      int  t1   = get_id(equation, pos);
      int  tg   = get_id(equation, pos);
      int  t2   = get_id(equation, pos);
      printf("%c.%s.(", type, c2s(vr, constants, cpos++).c_str());
      if(t1 == -1)
	printf("%s, ", c2s(vr, constants, cpos++).c_str());
      else
	printf("%s, ", i2v(t1).c_str());
      if(tg == -1)
	printf("%s, ", c2s(vr, constants, cpos++).c_str());
      else
	printf("%s, ", i2v(tg).c_str());
      if(t2 == -1)
	printf("%s)", c2s(vr, constants, cpos++).c_str());
      else
	printf("%s)", i2v(t2).c_str());
      if(!vr) {
	node *n = *aci++;
	printf("  %d %d (%d %d)\n",
	       n->x/10, state.sy - n->y/10, int(n->x/10*ratio+0.5), int((state.sy - n->y/10)*ratio+0.5));
      } else
	printf("\n");

      if(pos != equation.size()) {
	assert(equation[pos] == ' ');
	pos++;
      }
    }
  }
}


void state_t::pull(int &term, int gate, int oterm)
{
  int thr = gate - ET;
  if(term < thr)
    term = oterm < thr ? oterm : thr;
  else if(oterm < thr)
    term = oterm;
}

void state_t::Ta__(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  pull(level[0], constants[1], constants[2]);
}

void state_t::Ta_b(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, b)
  if(level[0] == level[1])
    return;
  int lim = constants[1] - ET;
  if(level[0] >= lim && level[1] >= lim)
    return;
  int mid = (level[0] + level[1])/2;
  if(mid <= lim) {
    level[0] = level[1] = mid;
    return;
  }
  if(level[0] > lim) {
    level[0] -= lim - level[1];
    level[1] = lim;
  } else {
    level[1] -= lim - level[0];
    level[0] = lim;
  }
}

void state_t::Daa_(const vector<int> &constants, vector<int> &level)
{
  // D.k0.(a, a, k1)
  level[0] = constants[1];
}


void state_t::Da__(const vector<int> &constants, vector<int> &level)
{
  // D.k0.(a, k1, k2)
  int thr = constants[1] - ED;
  if(thr > constants[2])
    thr = constants[2];
  if(level[0] < thr)
    level[0] = thr;
}

void state_t::Daa__Ta_b(const vector<int> &constants, vector<int> &level)
{
  // D.k0.(a, a, k1)
  // T.k2.(a, k3, b)
  level[0] = constants[1];
  pull(level[1], constants[3], level[0]);
}

void state_t::Dbb__Ta_b(const vector<int> &constants, vector<int> &level)
{
  // D.k0.(b, b, k1)
  // T.k2.(a, k3, b)
  level[1] = constants[1];
  pull(level[0], constants[3], level[1]);
}

void state_t::Ta___Ta_b(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // T.k3.(a, k4, b)
  pull(level[0], constants[1], constants[2]);
  pull(level[1], constants[4], level[0]);
}

void state_t::Tb___Ta_b(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(b, k1, k2)
  // T.k3.(a, k4, b)
  pull(level[1], constants[1], constants[2]);
  pull(level[0], constants[4], level[1]);
}

void state_t::Ta_b_Ta_c(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, b)
  // T.k2.(a, k3, c)
  if(level[0] == level[1] && level[1] == level[2])
    return;
  int lim1 = constants[1] - ET;
  int lim3 = constants[3] - ET;
  if(level[0] >= lim1 && level[0] >= lim3 && level[1] >= lim1 && level[2] >= lim3)
    return;
  //  abort();
}

void state_t::Ta___Daa_(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // D.k3.(a, a, k4)
  int thr = constants[1] - ET;
  double dt = thr*thr - double(constants[3])/constants[0]*ED*ED + constants[2]*(constants[2]-2*thr);
  if(dt < 0) {
    thr = constants[4] + ED;
    dt = thr*thr - double(constants[0])/constants[3]*pow(constants[1]-ET-constants[2], 2) - constants[4]*(2*ED+constants[4]);
    level[0] = int(thr+sqrt(dt)+0.5);

  } else {
    level[0] = int(thr-sqrt(dt)+0.5);
    assert(level[0] < constants[4] + ED);
  }
}

void state_t::Ta___Ta__(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // T.k3.(a, k4, k5)

  if(constants[2] <= constants[1] - ET && constants[5] >= constants[4] - ET) {
    // First transistor linear, second saturates
    double thrl = constants[1] - ET;
    double thrs = constants[4] - ET;
    double a = constants[0] + constants[3];
    double b = thrl*constants[0] + thrs*constants[3];
    double c = constants[3]*thrs*thrs - constants[0]*constants[2]*(constants[2]-2*thrl);
    double dt = b*b-a*c;
    if(dt >= 0) {
      double r =(b-sqrt(dt))/a;
      level[0] = int(r + 0.5);
    } else
      level[0] = int(0.5 + constants[4] - ET - sqrt(double(constants[0])/constants[3])*(constants[1]-ET-constants[2]));

  } else if(constants[2] >= constants[1] - ET && constants[5] <= constants[4] - ET) {
    // Second transistor linear, first saturates
    double thrl = constants[4] - ET;
    double thrs = constants[1] - ET;
    double a = constants[3] + constants[0];
    double b = thrl*constants[3] + thrs*constants[0];
    double c = constants[0]*thrs*thrs - constants[3]*constants[5]*(constants[5]-2*thrl);
    double dt = sqrt(b*b-a*c);
    double r =(b-dt)/a;
    level[0] = int(r + 0.5);

  } else if(constants[2] <= constants[1] - ET && constants[5] <= constants[4] - ET && constants[2] == constants[5]) {
    level[0] = constants[2];

  } else {
    fprintf(stderr, "Ta___Ta__ %d %d %d | %d %d %d\n", constants[0], constants[1], constants[2], constants[3], constants[4], constants[5]);
    //    abort();
  }
}

void state_t::Ta___Ta___Ta_b(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // T.k3.(a, k4, k5)
  // T.k6.(a, k7, b)

  if(constants[2] <= constants[1] - ET && constants[5] >= constants[4] - ET) {
    // First transistor linear, second saturates
    double thrl = constants[1] - ET;
    double thrs = constants[4] - ET;
    double a = constants[0] + constants[3];
    double b = thrl*constants[0] + thrs*constants[3];
    double c = constants[3]*thrs*thrs - constants[0]*constants[2]*(constants[2]-2*thrl);
    double dt = sqrt(b*b-a*c);
    double r =(b-dt)/a;
    level[0] = int(r + 0.5);

  } else if(constants[2] >= constants[1] - ET && constants[5] <= constants[4] - ET) {
    // Second transistor linear, first saturates
    double thrl = constants[4] - ET;
    double thrs = constants[1] - ET;
    double a = constants[3] + constants[0];
    double b = thrl*constants[3] + thrs*constants[0];
    double c = constants[0]*thrs*thrs - constants[3]*constants[5]*(constants[5]-2*thrl);
    double dt = sqrt(b*b-a*c);
    double r =(b-dt)/a;
    level[0] = int(r + 0.5);

  } else {
    fprintf(stderr, "Ta___Ta___Ta_b %d %d %d | %d %d %d %d\n", constants[0], constants[1], constants[2], constants[3], constants[4], constants[5], constants[6]);
    //    abort();
  }
  pull(level[1], constants[6], level[0]);
}

void state_t::Tbb__Taa__Ta___Taab(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(b, b, k1)
  // T.k2.(a, a, k3)
  // T.k4.(a, k5, k6)
  // T.k7.(a, a, b)
  // Expect all mosfets are saturated, a>b

  double ratio = sqrt(double(constants[0])/constants[7]);
  double ax1 = 1+ratio;
  double ax3 = ratio*(ET-constants[1]);

  double AA = ax1*ax1*(constants[4]-constants[2]) - constants[7]*(ax1-1)*(ax1-1);
  double BB = constants[2]*ax1*(ax3+constants[3]) - constants[4]*ax1*(constants[5]-2*ET+ax3) + constants[7]*(ax1-1)*ax3;
  double CC = -constants[2]*pow(ax3-constants[3], 2) + constants[4]*pow(constants[5]-2*ET+ax3, 2) - constants[7]*ax3*ax3;

  double dt = sqrt(BB*BB-AA*CC);
  double rb = (-BB-dt)/AA;
  double ra = ax1*rb-ax3+ET;

  level[0] = int(ra+0.5);
  level[1] = int(rb+0.5);
}

void state_t::Dbb__Ta_b_Tb_c(const vector<int> &constants, vector<int> &level)
{
  // D.k0.(b, b, k1)
  // T.k2.(a, k3, b)
  // T.k4.(b, k5, c)
  level[1] = constants[1];
  pull(level[0], constants[3], level[1]);
  pull(level[2], constants[5], level[1]);
}

void state_t::Daa__Ta_b_Ta_c(const vector<int> &constants, vector<int> &level)
{
  // D.k0.(a, a, k1)
  // T.k2.(a, k3, b)
  // T.k4.(a, k5, c)
  level[0] = constants[1];
  pull(level[1], constants[3], level[0]);
  pull(level[2], constants[5], level[0]);
}

void state_t::Ta_b_Tb_c(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, b)
  // T.k2.(b, k3, c)
  if(level[0] == level[1] && level[1] == level[2])
    return;
  int lim1 = constants[1] - ET;
  int lim3 = constants[3] - ET;
  if(level[1] >= lim1 && level[1] >= lim3 && level[0] >= lim1 && level[2] >= lim3)
    return;
  //  abort();
}

void state_t::Ta___Dabb(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // D.k3.(a, b, b)
  level[0] = constants[2] - ET;
  level[1] = level[0];
}

void state_t::Tc___Dbb__Ta___Tbac_Taab(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(c, k1, k2)
  // D.k3.(b, b, k4)
  // T.k5.(a, k6, k7)
  // T.k8.(b, a, c)
  // T.k9.(a, a, b)
  level[1] = constants[4];
  level[2] = constants[2] - ET;
  level[0] = constants[7] - ET;
}

void state_t::Dbb__Tc___Ta___Tbac_Taab(const vector<int> &constants, vector<int> &level)
{
  // D.k0.(b, b, k1)
  // T.k2.(c, k3, k4)
  // T.k5.(a, k6, k7)
  // T.k8.(b, a, c)
  // T.k9.(a, a, b)
  level[1] = constants[1];
  level[2] = constants[4] - ET;
  level[0] = constants[7] - ET;
}

void state_t::Ta___Dabb_Dacc_Dadd_Daee(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // D.k3.(a, b, b)
  // D.k4.(a, c, c)
  // D.k5.(a, d, d)
  // D.k6.(a, e, e)
  level[0] = constants[2] - ET;
  level[1] = level[0];
  level[2] = level[0];
  level[3] = level[0];
  level[4] = level[0];
}

void state_t::Ta___Dabb_Dacc_Dadd_Daee_Daff(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // D.k3.(a, b, b)
  // D.k4.(a, c, c)
  // D.k5.(a, d, d)
  // D.k6.(a, e, e)
  // D.k7.(a, f, f)
  level[0] = constants[2] - ET;
  level[1] = level[0];
  level[2] = level[0];
  level[3] = level[0];
  level[4] = level[0];
  level[5] = level[0];
}

void state_t::Daa__Taab(const vector<int> &constants, vector<int> &level)
{
  // D.k0.(a, a, k1)
  // T.k2.(a, a, b)
  level[0] = constants[1];
  pull(level[1], level[0], level[0]);
}

void state_t::Tbb__Daa__Taab(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(b, b, k1)
  // D.k2.(a, a, k3)
  // T.k4.(a, a, b)

  double ratio = sqrt(double(constants[0])/constants[4]);
  double ax1 = 1+ratio;
  double ax2 = ET-ratio*(ET+constants[1]);

  double AA = -constants[2]*ax1*ax1 -constants[4]*pow(ax1-1, 2);
  double BB = constants[2]*ax1*(-ax2 + constants[3] +ED) - constants[4]*(ax1-1)*(ax2-ET);
  double CC = constants[2]*(-ax2+constants[3])*(ax2-constants[3]-2*ED) - constants[4]*pow(ax2-ET, 2);

  double dt = sqrt(BB*BB-AA*CC);
  double rb = (-BB-dt)/AA;
  double ra = ax1*rb+ax2;

  level[0] = int(ra+0.5);
  level[1] = int(rb+0.5);
}

void state_t::Ta___Ta___Daa_(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // T.k3.(a, k4, k5)
  // D.k6.(a, a, k7)

  double AA = constants[0]+constants[3];
  double BB = constants[0]*(constants[1]+constants[2]-ET)+constants[3]*(constants[4]+constants[5]-ET);
  double CC = constants[0]*constants[2]*(2*(constants[1]-ET)-constants[2]) + constants[3]*constants[5]*(2*(constants[4]-ET)-constants[5]) + constants[6]*ED*ED;

  double dt = sqrt(BB*BB-AA*CC);
  double ra = (BB-dt)/AA;
  level[0] = int(ra*10+0.5);
}

void state_t::Tb___Tc___Te___Ta_b_Ta_c_Ta_d_Ta_e_Ta_f(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(b, k1, k2)
  // T.k3.(c, k4, k5)
  // T.k6.(e, k7, k8)
  // T.k9.(a, k10, b)
  // T.k11.(a, k12, c)
  // T.k13.(a, k14, d)
  // T.k15.(a, k16, e)
  // T.k17.(a, k18, f)
  assert(constants[2] == 0 && constants[5] == 0 && constants[8] == 0);
  level[1] = 0;
  level[2] = 0;
  level[4] = 0;
  level[0] = 0;
  level[3] = 0;
  level[5] = 0;
}

void state_t::Ta___Daa__Taab(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // D.k3.(a, a, k4)
  // T.k5.(a, a, b)

  int thr = constants[1] - ET;
  double dt1 = thr*thr - double(constants[3])/constants[0]*ED*ED + constants[2]*(constants[2]-2*thr);
  if(dt1 >= 0) {
    double dt = sqrt(dt1);
    level[0] = int(thr-dt+0.5);
  } else {
    dt1 = pow(constants[3]*(-ED-constants[4]), 2)-constants[3]*(constants[0]*pow(constants[1]-ET-constants[2], 2) - constants[3]*constants[4]*(-2*ED-constants[4]));
    fprintf(stderr, "%f\n", pow(constants[3]*(-ED-constants[4]), 2));
    double dt = sqrt(dt1);
    level[0] = int(constants[4]+ED+dt/constants[3]+0.5);
  }
  assert(level[0] >= constants[2] && level[0] <= constants[4]);
  pull(level[1], level[0], level[0]);
}

void state_t::Tb___Dbb__Tabb(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(b, k1, k2)
  // D.k3.(b, b, k4)
  // T.k5.(a, b, b)

  int thr = constants[1] - ET;
  double dt1 = thr*thr - double(constants[3])/constants[0]*ED*ED + constants[2]*(constants[2]-2*thr);
  if(dt1 >= 0) {
    double dt = sqrt(dt1);
    level[1] = int(thr-dt+0.5);
  } else {
    dt1 = pow(constants[3]*(-ED-constants[4]), 2)-constants[3]*(constants[0]*pow(constants[1]-ET-constants[2], 2) - constants[3]*constants[4]*(-2*ED-constants[4]));
    fprintf(stderr, "%f\n", pow(constants[3]*(-ED-constants[4]), 2));
    double dt = sqrt(dt1);
    level[1] = int(constants[4]+ED+dt/constants[3]+0.5);
  }
  assert(level[1] >= constants[2] && level[1] <= constants[4]);
  pull(level[0], level[1], level[1]);
}

void state_t::Tb___Dbb__Ta_b(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(b, k1, k2)
  // D.k3.(b, b, k4)
  // T.k5.(a, k6, b)

  int thr = constants[1] - ET;
  double dt1 = thr*thr - double(constants[3])/constants[0]*ED*ED + constants[2]*(constants[2]-2*thr);
  if(dt1 >= 0) {
    double dt = sqrt(dt1);
    level[1] = int(thr-dt+0.5);
  } else {
    dt1 = pow(constants[3]*(-ED-constants[4]), 2)-constants[3]*(constants[0]*pow(constants[1]-ET-constants[2], 2) - constants[3]*constants[4]*(-2*ED-constants[4]));
    fprintf(stderr, "%f\n", pow(constants[3]*(-ED-constants[4]), 2));
    double dt = sqrt(dt1);
    level[1] = int(constants[4]+ED+dt/constants[3]+0.5);
  }
  assert(level[1] >= constants[2] && level[1] <= constants[4]);
  pull(level[0], constants[6], level[1]);
}

void state_t::Ta___Ta___Ta___Daa_(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // T.k3.(a, k4, k5)
  // T.k6.(a, k7, k8)
  // D.k9.(a, a, k10)

  double AA = constants[0]+constants[3]+constants[6];
  double BB =
    constants[ 0]*(constants[ 1]+constants[ 2]-ET)+
    constants[ 3]*(constants[ 4]+constants[ 5]-ET)+
    constants[ 6]*(constants[ 7]+constants[ 8]-ET);
  double CC =
    constants[ 0]*constants[ 2]*(2*(constants[ 1]-ET)-constants[ 2])+
    constants[ 3]*constants[ 5]*(2*(constants[ 4]-ET)-constants[ 5])+
    constants[ 6]*constants[ 8]*(2*(constants[ 7]-ET)-constants[ 8])+
    constants[ 9]*ED*ED;

  double dt = sqrt(BB*BB-AA*CC);
  double ra = (BB-dt)/AA;
  level[0] = int(ra*10+0.5);
}

void state_t::Ta___Ta___Ta___Ta___Daa_(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // T.k3.(a, k4, k5)
  // T.k6.(a, k7, k8)
  // T.k6.(a, k10, k11)
  // D.k12.(a, a, k13)

  double AA = constants[0]+constants[3]+constants[6]+constants[9];
  double BB =
    constants[ 0]*(constants[ 1]+constants[ 2]-ET)+
    constants[ 3]*(constants[ 4]+constants[ 5]-ET)+
    constants[ 6]*(constants[ 7]+constants[ 8]-ET)+
    constants[ 9]*(constants[10]+constants[11]-ET);
  double CC =
    constants[ 0]*constants[ 2]*(2*(constants[ 1]-ET)-constants[ 2])+
    constants[ 3]*constants[ 5]*(2*(constants[ 4]-ET)-constants[ 5])+
    constants[ 6]*constants[ 8]*(2*(constants[ 7]-ET)-constants[ 8])+
    constants[ 9]*constants[11]*(2*(constants[10]-ET)-constants[11])+
    constants[12]*ED*ED;

  double dt = sqrt(BB*BB-AA*CC);
  double ra = (BB-dt)/AA;
  level[0] = int(ra*10+0.5);
}

void state_t::Ta___Ta___Ta___Ta___Ta___Ta___Ta___Daa_(const vector<int> &constants, vector<int> &level)
{
  // T.k0.(a, k1, k2)
  // T.k3.(a, k4, k5)
  // T.k6.(a, k7, k8)
  // T.k9.(a, k10, k11)
  // T.k12.(a, k13, k14)
  // T.k15.(a, k16, k17)
  // T.k18.(a, k19, k20)
  // D.k21.(a, a, k22)

  double AA = constants[0]+constants[3]+constants[6]+constants[9]+constants[12]+constants[15]+constants[18];
  double BB =
    constants[ 0]*(constants[ 1]+constants[ 2]-ET)+
    constants[ 3]*(constants[ 4]+constants[ 5]-ET)+
    constants[ 6]*(constants[ 7]+constants[ 8]-ET)+
    constants[ 9]*(constants[10]+constants[11]-ET)+
    constants[12]*(constants[13]+constants[14]-ET)+
    constants[15]*(constants[16]+constants[17]-ET)+
    constants[18]*(constants[19]+constants[20]-ET);
  double CC =
    constants[ 0]*constants[ 2]*(2*(constants[ 1]-ET)-constants[ 2])+
    constants[ 3]*constants[ 5]*(2*(constants[ 4]-ET)-constants[ 5])+
    constants[ 6]*constants[ 8]*(2*(constants[ 7]-ET)-constants[ 8])+
    constants[ 9]*constants[11]*(2*(constants[10]-ET)-constants[11])+
    constants[12]*constants[14]*(2*(constants[13]-ET)-constants[14])+
    constants[15]*constants[17]*(2*(constants[16]-ET)-constants[17])+
    constants[18]*constants[20]*(2*(constants[19]-ET)-constants[20])+
    constants[21]*ED*ED;

  double dt = sqrt(BB*BB-AA*CC);
  double ra = (BB-dt)/AA;
  level[0] = int(ra*10+0.5);
}

void state_t::register_solvers()
{
  //  solvers[""]                                        = ;
  solvers["Ta.."]                                    = Ta__;
  solvers["Ta.b"]                                    = Ta_b;
  solvers["Da.."]                                    = Da__;
  solvers["Daa."]                                    = Daa_;
  solvers["Daa. Ta.b"]                               = Daa__Ta_b;
  solvers["Dbb. Ta.b"]                               = Dbb__Ta_b;
  solvers["Ta.. Ta.b"]                               = Ta___Ta_b;
  solvers["Tb.. Ta.b"]                               = Tb___Ta_b;
  solvers["Ta.b Ta.c"]                               = Ta_b_Ta_c;
  solvers["Ta.. Ta.."]                               = Ta___Ta__;
  solvers["Ta.. Ta.. Ta.b"]                          = Ta___Ta___Ta_b;
  solvers["Tbb. Taa. Ta.. Taab"]                     = Tbb__Taa__Ta___Taab;
  solvers["Dbb. Ta.b Tb.c"]                          = Dbb__Ta_b_Tb_c;
  solvers["Daa. Ta.b Ta.c"]                          = Daa__Ta_b_Ta_c;
  solvers["Ta.b Tb.c"]                               = Ta_b_Tb_c;
  solvers["Ta.. Dabb"]                               = Ta___Dabb;
  solvers["Tc.. Dbb. Ta.. Tbac Taab"]                = Tc___Dbb__Ta___Tbac_Taab;
  solvers["Dbb. Tc.. Ta.. Tbac Taab"]                = Dbb__Tc___Ta___Tbac_Taab;
  solvers["Ta.. Dabb Dacc Dadd Daee"]                = Ta___Dabb_Dacc_Dadd_Daee;
  solvers["Ta.. Dabb Dacc Dadd Daee Daff"]           = Ta___Dabb_Dacc_Dadd_Daee_Daff;
  solvers["Daa. Taab"]                               = Daa__Taab;
  solvers["Tbb. Daa. Taab"]                          = Tbb__Daa__Taab;
  solvers["Tb.. Tc.. Te.. Ta.b Ta.c Ta.d Ta.e Ta.f"] = Tb___Tc___Te___Ta_b_Ta_c_Ta_d_Ta_e_Ta_f;
  solvers["Ta.. Daa. Taab"]                          = Ta___Daa__Taab;
  solvers["Tb.. Dbb. Tabb"]                          = Tb___Dbb__Tabb;
  solvers["Tb.. Dbb. Ta.b"]                          = Tb___Dbb__Ta_b;
  solvers["Ta.. Daa."]                               = Ta___Daa_;
  solvers["Ta.. Ta.. Daa."]                          = Ta___Ta___Daa_;
  solvers["Ta.. Ta.. Ta.. Daa."]                     = Ta___Ta___Ta___Daa_;
  solvers["Ta.. Ta.. Ta.. Ta.. Daa."]                = Ta___Ta___Ta___Ta___Daa_;
  solvers["Ta.. Ta.. Ta.. Ta.. Ta.. Ta.. Ta.. Daa."] = Ta___Ta___Ta___Ta___Ta___Ta___Ta___Daa_;
}
