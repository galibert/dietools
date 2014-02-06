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

#include <ft2build.h>
#include FT_FREETYPE_H

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
  rd.nl();

  int nn = rd.gi();
  rd.nl();
  nodes.resize(nn);
  int tmap[256];
  memset(tmap, 0xff, 256*4);
  tmap['t'] = node::T;
  tmap['d'] = node::D;
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
    n->y = (4666-rd.gi())*10;
    n->netids[node::T1] = rd.gi();
    if(type == node::T || type == node::D) {
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
      n->pt[j].y = (4666-rd.gi())*10;
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
  state.reload();
}

void node::bbox()
{
  switch(type) {
  case T: case D:
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
  case T: case D:
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
    hline(ids, ox, oy, w, h, z, x-40, x-10, y, nets[GATE]->id);
    vline(ids, ox, oy, w, h, z, x-10, y-20, y+20, id);
    if(type == D)
      rect(ids, ox, oy, w, h, z, x, y-20, x+4, y+20, id);
    break;

  case E_S:
    vline(ids, ox, oy, w, h, z, x-10, y-40, y-20, nets[orientation == E_S ? T2 : T1]->id);
    hline(ids, ox, oy, w, h, z, x-10, x, y-20, id);
    vline(ids, ox, oy, w, h, z, x, y-20, y+20, id);
    hline(ids, ox, oy, w, h, z, x-10, x, y+20, id);
    vline(ids, ox, oy, w, h, z, x-10, y+20, y+40, nets[orientation == E_S ? T1 : T2]->id);
    hline(ids, ox, oy, w, h, z, x+10, x+40, y, nets[GATE]->id);
    vline(ids, ox, oy, w, h, z, x+10, y-20, y+20, id);
    if(type == D)
      rect(ids, ox, oy, w, h, z, x-4, y-20, x, y+20, id);
    break;

  case N_S:
    hline(ids, ox, oy, w, h, z, x-40, x-20, y+10, nets[orientation == N_S ? T1 : T2]->id);
    vline(ids, ox, oy, w, h, z, x-20, y, y+10, id);
    hline(ids, ox, oy, w, h, z, x-20, x+20, y, id);
    vline(ids, ox, oy, w, h, z, x+20, y, y+10, id);
    hline(ids, ox, oy, w, h, z, x+20, x+40, y+10, nets[orientation == N_S ? T2 : T1]->id);
    vline(ids, ox, oy, w, h, z, x, y-40, y-10, nets[GATE]->id);
    hline(ids, ox, oy, w, h, z, x-20, x+20, y-10, id);
    if(type == D)
      rect(ids, ox, oy, w, h, z, x-20, y, x+20, y+4, id);
    break;

  case S_S:
    hline(ids, ox, oy, w, h, z, x-40, x-20, y-10, nets[orientation == S_S ? T1 : T2]->id);
    vline(ids, ox, oy, w, h, z, x-20, y-10, y, id);
    hline(ids, ox, oy, w, h, z, x-20, x+20, y, id);
    vline(ids, ox, oy, w, h, z, x+20, y-10, y, id);
    hline(ids, ox, oy, w, h, z, x+20, x+40, y-10, nets[orientation == S_S ? T2 : T1]->id);
    vline(ids, ox, oy, w, h, z, x, y+10, y+40, nets[GATE]->id);
    hline(ids, ox, oy, w, h, z, x-20, x+20, y+10, id);
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

void state_t::reload()
{
  build();

  set<int> changed;
  for(int i=0; i != int(nets.size()); i++)
    if(power[i])
      changed.insert(i);
  apply_changed(changed);
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
    if(nodes[i]->type == node::T || nodes[i]->type == node::D) {
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

void state_t::build_equation(string &equation, vector<int> &constants, const vector<int> & nids_to_solve, const vector<int> &levels, const set<node *> &accepted_trans, const map<int, int> &nid_to_index) const
{
  constants.clear();
  equation = "";
  for(unsigned int i = 0; i != nids_to_solve.size(); i++) {
    int nid = nids_to_solve[i];
    for(set<node *>::const_iterator j = accepted_trans.begin(); j != accepted_trans.end(); j++) {
      node *tr = *j;
      int nt1 = tr->netids[node::T1];
      int nt2 = tr->netids[node::T2];
      int ng  = tr->netids[node::GATE];
      if(nt1 != nid && nt2 != nid)
	continue;
      if(nt1 == nid) {
	nt1 = nt2;
	nt2 = nid;
      }
      map<int, int>::const_iterator k = nid_to_index.find(nt1);
      int t1_id = k == nid_to_index.end() ? -1 : k->second;
      k = nid_to_index.find(ng);
      int gate_id = k == nid_to_index.end() ? -1 : k->second;

      int pnt1 = t1_id == -1 ? power[nt1] : levels[t1_id];
      int pnt2 = levels[i];
      int png  = gate_id == -1 ? power[ng] : levels[gate_id];

      int ithr = tr->type == node::T ? 7 : -30;
      int thr = png - ithr;
      // L abc (c-a)*(2*(b-t)-c-a)
      // S abc (b-t-a)**2
      if(nid == 973 || nid == 801)
	printf("add trans %s (%d %d %d) %f\n", tr->name.c_str(), pnt1, png, pnt2, tr->f);
      if(pnt1 < pnt2 && thr >= pnt1) {
	constants.push_back(int(tr->f*1000));
	constants.push_back(pnt1);
	if(thr < pnt2) {
	  // Saturation S -> D
	  if(t1_id != -1) {
	    if(gate_id != -1) {
	      constants.push_back(ithr);
	      equation = equation + "+S" + char('a'+t1_id) + char('a'+gate_id) + '.';
	    } else {
	      constants.push_back(thr);
	      equation = equation + "+S" + char('a'+t1_id) + "..";
	    }
	  } else {
	    if(gate_id != -1) {
	      constants.push_back(ithr);
	      equation = equation + "+S" + '.' + char('a'+gate_id) + '.';
	    } else {
	      constants.push_back(thr);
	      equation = equation + "+S" + "...";
	    }
	  }
	} else {
	  // Linear S -> D
	  if(t1_id != -1) {
	    if(gate_id != -1) {
	      constants.push_back(ithr);
	      equation = equation + "+L" + char('a'+t1_id) + char('a'+gate_id) + char('a'+i);
	    } else {
	      constants.push_back(thr);
	      equation = equation + "+L" + char('a'+t1_id) + '.' + char('a'+i);
	    }
	  } else {
	    if(gate_id != -1) {
	      constants.push_back(ithr);
	      equation = equation + "+L" + '.' + char('a'+gate_id) + char('a'+i);
	    } else {
	      constants.push_back(thr);
	      equation = equation + "+L" + ".." + char('a'+i);
	    }
	  }
	}
      } else if(pnt1 > pnt2 && thr >= pnt2) {
	constants.push_back(int(tr->f*1000));
	constants.push_back(pnt1);
	if(thr < pnt1) {
	  // Saturation D -> S
	  if(t1_id != -1) {
	    if(gate_id != -1) {
	      constants.push_back(ithr);
	      equation = equation + "-S" + char('a'+i) + char('a'+gate_id) + '.';
	    } else {
	      constants.push_back(thr);
	      equation = equation + "-S" + char('a'+i) + "..";
	    }
	  } else {
	    if(gate_id != -1) {
	      constants.push_back(ithr);
	      equation = equation + "-S" + char('a'+i) + char('a'+gate_id) + '.';
	    } else {
	      constants.push_back(thr);
	      equation = equation + "-S" + char('a'+i) + "..";
	    }
	  }
	} else {
	  // Linear S -> D
	  if(t1_id != -1) {
	    if(gate_id != -1) {
	      constants.push_back(ithr);
	      equation = equation + "-L" + char('a'+i) + char('a'+gate_id) + char('a'+t1_id);
	    } else {
	      constants.push_back(thr);
	      equation = equation + "-L" + char('a'+i) + '.' + char('a'+t1_id);
	    }
	  } else {
	    if(gate_id != -1) {
	      constants.push_back(ithr);
	      equation = equation + "-L" + char('a'+i) + char('a'+gate_id) + '.';
	    } else {
	      constants.push_back(thr);
	      equation = equation + "-L" + char('a'+i) + "..";
	    }
	  }
	}
      }
    }
    equation += ';';
  }
}

void state_t::apply_changed(set<int> changed)
{
  int ctime = 0;
  map<int, map<int, int> > future_changes;
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
	  int thr = power[ng] - (tr->type == node::T ? 7 : -30);
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
      if(!nids_to_solve.empty()) {
	map<int, int> nid_to_index;
	for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	  nid_to_index[nids_to_solve[i]] = i;
	string prev_eq;
	vector<int> levels;
	levels.resize(nids_to_solve.size());
	for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	  levels[i] = power[nids_to_solve[i]];
	if(nids_to_solve[0] == 973) {
	  levels[0] = 29;
	  levels[1] = 15;
	}
	for(;;) {
	  string equation;
	  vector<int> constants;
	  build_equation(equation, constants, nids_to_solve, levels, accepted_trans, nid_to_index);
	  if(equation == prev_eq)
	    break;

	  map<string, void (*)(const vector<int> &constants, vector<int> &level)>::const_iterator sp = solvers.find(equation);
	  if(sp == solvers.end()) {
	    printf("Unhandled equation system.\n");
	    dump_equation_system(equation, constants, nids_to_solve);
	    for(unsigned int i=0; i != nids_to_solve.size(); i++)
	      highlight[nids_to_solve[i]] = true;
	    return;
	  }

	  dump_equation_system(equation, constants, nids_to_solve);
	  sp->second(constants, levels);
	  printf("  levels:\n");
	  for(unsigned int i = 0; i != nids_to_solve.size(); i++)
	    printf("   %c: %d.%d\n", 'a'+i, levels[i]/10, levels[i]%10);

	  prev_eq = equation;
	}
      }
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

void state_t::dump_equation_system(string equation, const vector<int> &constants, const vector<int> &nids_to_solve)
{
  printf("  key: %s\n", equation.c_str());
  printf("  fct: ");
  for(unsigned int i=0; i != equation.length(); i++) {
    char c = equation[i];
    if(c == ';' || c == '.')
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

  for(int vr = 0; vr < 2; vr++) {
    if(vr)
      printf("  alt:\n");
    else
      printf("  system:\n");
    unsigned int pos = 0, cpos = 0;
    for(unsigned int i=0; i != nids_to_solve.size(); i++) {
      printf("   ");
      while(equation[pos] != ';') {
	printf(" %c %s*", equation[pos++], c2s(vr, constants, cpos++).c_str());
	char type = equation[pos++];
	char t1   = equation[pos++];
	char tg   = equation[pos++];
	char t2   = equation[pos++];

	int id = (type == 'S' ? 8 : 0) | (t1 == '.' ? 4 : 0) | (t2 == '.' ? 2 : 0) | (tg == '.' ? 1 : 0);
	switch(id) {
	case 0x01:
	  printf("(%c-%c)*(2*%s-%c-%c)", t1, t2, c2s(vr, constants, cpos+1).c_str(), t1, t2);
	  break;

	case 0x02:
	  printf("(%c-%s)*(2*(%c-%s)-%c-%s)", t1, c2s(vr, constants, cpos).c_str(), tg, c2s(vr, constants, cpos+1).c_str(), t1, c2s(vr, constants, cpos).c_str());
	  break;
	  
	case 0x0a:
	  printf("(%c-%s-%c [%s])^2", tg, c2s(vr, constants, cpos+1).c_str(), t1, c2s(vr, constants, cpos).c_str());
	  break;

	case 0x0b:
	  printf("(%s-%c)^2", c2s(vr, constants, cpos+1).c_str(), t1);
	  break;

	case 0x0e:
	  printf("(%c-%s-%s)^2", tg, c2s(vr, constants, cpos+1).c_str(), c2s(vr, constants, cpos).c_str());
	  break;

	default:
	  printf("#%x#", id);
	  break;
	}
	cpos += 2;
      }
      pos++;
      printf(" = 0\n");
    }
    assert(pos == equation.size());
  }
}

void state_t::_(const vector<int> &constants, vector<int> &level)
{
}

void state_t::__(const vector<int> &constants, vector<int> &level)
{
}

void state_t::___(const vector<int> &constants, vector<int> &level)
{
}

void state_t::____(const vector<int> &constants, vector<int> &level)
{
}

void state_t::_____(const vector<int> &constants, vector<int> &level)
{
}

void state_t::______(const vector<int> &constants, vector<int> &level)
{
}

void state_t::mSa___(const vector<int> &constants, vector<int> &level)
{
  // - k0*(k1-a)^2 = 0
  level[0] = constants[1];
}

void state_t::mSa____(const vector<int> &constants, vector<int> &level)
{
  // - k0*(k1-a)^2 = 0
  level[0] = constants[1];
}

void state_t::mSa______(const vector<int> &constants, vector<int> &level)
{
  // - k0*(k1-a)^2 = 0
  level[0] = constants[1];
}

void state_t::_mSb___(const vector<int> &constants, vector<int> &level)
{
  // - k0*(k1-a)^2 = 0
  level[1] = constants[1];
}

void state_t::mSaa___(const vector<int> &constants, vector<int> &level)
{
  // - k0*(a-k2-a [k1])^2 = 0
  // special case, limit to the other side power level since saturation is going to switch to linear
  level[0] = constants[1];
}

void state_t::_mSbb__(const vector<int> &constants, vector<int> &level)
{
  // - k0*(b-k2-b [k1])^2 = 0
  // special case, limit to the other side power level since saturation is going to switch to linear
  level[1] = constants[1];
}

void state_t::mSa___pSa___(const vector<int> &constants, vector<int> &level)
{
  // - k0*(k2-a)^2 = 0
  // + k3*(k5-a)^2 = 0
  assert(constants[2] == constants[5]);
  level[0] = constants[2];
}

void state_t::pSb___mSb___(const vector<int> &constants, vector<int> &level)
{
  // + k0*(k2-b)^2 = 0
  // - k3*(k5-b)^2 = 0
  assert(constants[2] == constants[5]);
  level[0] = constants[2];
}

void state_t::pS_a_mSa__pSba__mSba__(const vector<int> &constants, vector<int> &level)
{
  // + k0*(a-k2-k1)^2 - k3*(k5-a)^2 + k6*(a-k8-b [k7])^2 = 0
  // - k9*(a-k11-b [k10])^2 = 0
  assert(constants[8] == constants[11]);

  /*
    k0.(a-kx)^2 - k3.(a-k5)^2 = 0
    (k0-k3).a^2 + 2.(k3.k5 - k0.kx).a +k0.kx^2-k3.k5^2 = 0
    A = k0-k3
    B = 2.(k3.k5-k0.kx)
    C = k0.kx^2 - k3.k5^2
    d = (k3.k5-k0.kx)^2 - (k0-k3).(k0.kx^2-k3.k5^2)
      = k3^2.k5^2 - 2.k3.k5.k0.kx + k0^2.kx^2 - k0^2.kx^2 + k0.k3.k5^2 + k0.k3.kx^2 - k3^2.k5^2
      = 2.k3.k5.k0.kx + k0.k3.k5^2 + k0.k3.kx^2
      = k0.k3.(2.k5.kx + k5^2 + kx^2)
      = k0.k3.(k5+kx)^2
    sqrt(d) = sqrt(k0.k3)*(k5+kx)
    a = (k0.kx-k3.k5 - sqrt(k0.k3)(k5+kx))/(k0-k3)
  */
  int kx = constants[1] - constants[2];
  level[0] = int((constants[0]*kx - constants[3]*constants[5] - (constants[5]+kx)*sqrt(constants[0]*constants[3]))/(constants[0] - constants[3])+0.5);
  level[1] = level[0] - constants[8];
}

void state_t::pS_a_pSba__mSba__(const vector<int> &constants, vector<int> &level)
{
  // + k0*(a-k2-k1)^2 + k3*(a-k5-b [k4])^2 = 0
  // - k6*(a-k8-b [k7])^2 = 0
  assert(constants[5] == constants[8]);
  level[0] = constants[1] + constants[2];
  level[1] = level[0] - constants[5];
}

void state_t::mLaa_pLb_a_mLb_a_(const vector<int> &constants, vector<int> &level)
{
  // - k0*(a-k1)*(2*(a-k2)-a-k1) + k3*(b-a)*(2*k4-b-a) = 0
  // - k5*(b-a)*(2*k6-b-a) = 0
  level[0] = level[1] = constants[1];
}

void state_t::mSa__pLb_a_mLb_a_(const vector<int> &constants, vector<int> &level)
{
  // - k0*(k1-a)^2 + k2*(b-a)*(2*k3-b-a) = 0
  // - k4*(b-a)*(2*k5-b-a) = 0
  level[0] = level[1] = constants[1];
}

void state_t::register_solvers()
{
  solvers[";"]                      = _;
  solvers[";;"]                     = __;
  solvers[";;;"]                    = ___;
  solvers[";;;;"]                   = ____;
  solvers[";;;;;"]                  = _____;
  solvers[";;;;;;"]                 = ______;
  solvers["-Sa..;"]                 = mSa___;
  solvers["-Sa..;;"]                = mSa____;
  solvers["-Sa..;;;;"]              = mSa______;
  solvers[";-Sb..;"]                = _mSb___;
  solvers["-Saa.;;"]                = mSaa___;
  solvers[";-Sbb.;"]                = _mSbb__;
  solvers["-Sa..;+Sa..;"]           = mSa___pSa___;
  solvers["+Sb..;-Sb..;"]           = pSb___mSb___;
  solvers["+S.a.+Sba.;-Sba.;"]      = pS_a_pSba__mSba__;
  solvers["-Laa.+Lb.a;-Lb.a;"]      = mLaa_pLb_a_mLb_a_;
  solvers["-Sa..+Lb.a;-Lb.a;"]      = mSa__pLb_a_mLb_a_;
  solvers["+S.a.-Sa..+Sba.;-Sba.;"] = pS_a_mSa__pSba__mSba__;
}
