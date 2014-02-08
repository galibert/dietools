#include "MVDisplay.h"
#include "MVMain.h"
#include "globals.h"

#include <QtGui/QPainter>
#include <QMouseEvent>

MVDisplay::MVDisplay(QWidget *parent) : QWidget(parent)
{
  zoom = 1;
  xc = state->cmap.sx/2;
  yc = state->cmap.sy/2;
  qimg = 0;
  generated_image = 0;
  posx = 0;
  posy = 0;
  active_on = poly_on = metal_on = levels_on = true;
}
  
MVDisplay::~MVDisplay()
{
  if(qimg) {
    delete qimg;
    delete[] generated_image;
    delete[] posx;
    delete[] posy;
  }
}

void MVDisplay::set_zoom(double _zoom)
{
  zoom = _zoom;
  regen_scale();
  regen_pos();
  generate_image();
  update();
}

void MVDisplay::regen_scale()
{
  int w = size().width();
  int h = size().height();
  double sx = state->cmap.sx/zoom;  
  mvmain->set_steps(10*sx/w, sx/2, 10*sx/h, sx/2*w/h);
}

void MVDisplay::regen_pos()
{
  int w = size().width();
  int h = size().height();
  double sx = state->cmap.sx/zoom;  
  for(int x=-1; x<=w; x++)
    posx[x+1] = xc + (x-0.5*w)*sx/w + 0.5;
  for(int y=-1; y<=h; y++)
    posy[y+1] = yc + (y-0.5*h)*sx/w + 0.5;
}

void MVDisplay::get(int l, int x, int y, int &circ, bool &inside, int &net, char &type, int &power)
{
  net = -1;
  type = '*';
  inside = false;
  power = State::S_FLOAT;
  circ = state->cmap.p(l, posx[x+1], posy[y+1]);
  if(circ == -1)
    return;
  inside =
    state->cmap.p(l, posx[x], posy[y+1]) == circ && 
    state->cmap.p(l, posx[x+2], posy[y+1]) == circ && 
    state->cmap.p(l, posx[x+1], posy[y]) == circ && 
    state->cmap.p(l, posx[x+1], posy[y+2]) == circ;
  net = state->info.circs[circ].net;
  type = state->info.circs[circ].type;
  if(net != -1)
    power = state->power[net];
}

unsigned int MVDisplay::alpha(unsigned int cur, unsigned int val, double level)
{
  if(level == 1)
    return val;
  unsigned char r = ((cur >> 16) & 0xff) * (1-level) + ((val >> 16) & 0xff) * level;
  unsigned char g = ((cur >>  8) & 0xff) * (1-level) + ((val >>  8) & 0xff) * level;
  unsigned char b = ((cur      ) & 0xff) * (1-level) + ((val      ) & 0xff) * level;
  return (r << 16) | (g << 8) | b;
}

void MVDisplay::generate_image()
{
  int w = size().width();
  int h = size().height();
  unsigned int *dest = generated_image;
  for(int y=0; y<h; y++)
    for(int x=0; x<w; x++) {
      int circ_active, circ_poly, circ_metal;
      bool inside_active, inside_poly, inside_metal;
      int net_active, net_poly, net_metal;
      int power_active, power_poly, power_metal;
      char type_active, type_poly, type_metal;
      get(0, x, y, circ_active, inside_active, net_active, type_active, power_active);
      get(1, x, y, circ_poly, inside_poly, net_poly, type_poly, power_poly);
      get(2, x, y, circ_metal, inside_metal, net_metal, type_metal, power_metal);
      bool in_via = net_metal != -1 &&
	((net_active != -1 && net_active == net_metal) ||
	 ((net_poly != -1 && net_poly == net_metal)));

      static double transp_per_level[3] = { 0.7, 0.135, 0.4 };

      unsigned int col = 0xffffff;
      if(active_on && circ_active != -1 && type_active == 'a') {
	col = alpha(col, 0x0000ff, inside_active ? levels_on ? transp_per_level[power_active] : 0.135 : 1);
      }
      if(poly_on && circ_poly != -1 && type_poly == 'p') {
	col = alpha(col, 0xff0000, inside_poly ? levels_on ? transp_per_level[power_poly] : 0.135 : 1);
      }
      if((active_on || poly_on) && type_active == 't') {
	if(state->depletion[state->info.circs[circ_active].trans])
	  col = 0x008000;
	else
	  col = levels_on && power_active == State::S_1 ? 0xffc040 : 0xff8000;
      }
      if((active_on || poly_on) && type_active == 'c')
	col = 0x808000;
      if((active_on || poly_on) && type_active == 'b') {
	col = 0xff00ff;
      }
      if(metal_on && circ_metal != -1) {
	static double transp_per_level[3] = { 0.7, 0.135, 0.4 };
	bool transp = inside_metal && !(in_via && !((x-y) & 3));
	col = alpha(col, 0x0000000, transp ? levels_on ? transp_per_level[power_metal] : 0.135 : 1);
      }

      if(active_on && net_active != -1 && state->display[net_active])
	col = 0x00ffff;
      if(poly_on && net_poly != -1 && state->display[net_poly])
	col = 0x00ffff;
      if(metal_on && net_metal != -1 && state->display[net_metal])
	col = 0x00ffff;

      *dest++ = col | 0xff000000;
    }
}

void MVDisplay::paintEvent(QPaintEvent *e)
{
  if(!qimg)
    return;
  QPainter p(this);
  p.drawImage(0, 0, *qimg);
}

void MVDisplay::resizeEvent(QResizeEvent *e)
{
  if(qimg) {
    delete qimg;
    delete[] generated_image;
    delete[] posx;
    delete[] posy;
  }

  int w = size().width();
  int h = size().height();

  generated_image = new unsigned int[w*h];
  qimg = new QImage((uchar *)generated_image, w, h, QImage::Format_RGB32);
  posx = new int[w+2];
  posy = new int[h+2];
  regen_pos();
  generate_image();
}


void MVDisplay::mousePressEvent(QMouseEvent *e)
{
  int x = e->x();
  int y = e->y();
  int circ_active, circ_poly, circ_metal;
  bool inside_active, inside_poly, inside_metal;
  int net_active, net_poly, net_metal;
  char type_active, type_poly, type_metal;
  int power_active, power_poly, power_metal;
  get(0, x, y, circ_active, inside_active, net_active, type_active, power_active);
  get(1, x, y, circ_poly, inside_poly, net_poly, type_poly, power_poly);
  get(2, x, y, circ_metal, inside_metal, net_metal, type_metal, power_metal);

  if(active_on && net_active != -1)
    track(net_active);
  if(poly_on && net_poly != -1)
    track(net_poly);
  if(metal_on && net_metal != -1)
    track(net_metal);
}

void MVDisplay::mouseMoveEvent(QMouseEvent *e)
{
  int x = e->x();
  int y = e->y();
  int circ_active, circ_poly, circ_metal;
  bool inside_active, inside_poly, inside_metal;
  int net_active, net_poly, net_metal;
  char type_active, type_poly, type_metal;
  int power_active, power_poly, power_metal;
  get(0, x, y, circ_active, inside_active, net_active, type_active, power_active);
  get(1, x, y, circ_poly, inside_poly, net_poly, type_poly, power_poly);
  get(2, x, y, circ_metal, inside_metal, net_metal, type_metal, power_metal);

  char msg[4096];
  char *p = msg + sprintf(msg, "%5d %5d %c:%d:%s:%c %c:%d:%s:%c %c:%d:%s:%c",
			  posx[e->x()+1], 13999-posy[e->y()+1],
			  type_active, circ_active, state->ninfo.net_name(net_active).c_str(), "01-"[power_active],
			  type_poly, circ_poly, state->ninfo.net_name(net_poly).c_str(), "01-"[power_poly],
			  type_metal, circ_metal, state->ninfo.net_name(net_metal).c_str(), "01-"[power_metal]);
  if(type_poly == 't') {
    const tinfo &ti = state->info.trans[state->info.circs[circ_poly].trans];
    p += sprintf(p, " | %s-%s:%g", state->ninfo.net_name(ti.t1).c_str(), state->ninfo.net_name(ti.t2).c_str(), ti.f);
  }
  mvmain->set_status(msg);
}

void MVDisplay::mouseReleaseEvent(QMouseEvent *e)
{
}

void MVDisplay::fit_to_window()
{
  set_zoom(1);
}

void MVDisplay::zoom_in()
{
  set_zoom(zoom*1.25);
}

void MVDisplay::zoom_out()
{
  set_zoom(zoom*0.8);
}

void MVDisplay::set_active(bool state)
{
  active_on = state;
  generate_image();
  update();
}

void MVDisplay::set_poly(bool state)
{
  poly_on = state;
  generate_image();
  update();
}

void MVDisplay::set_metal(bool state)
{
  metal_on = state;
  generate_image();
  update();
}

void MVDisplay::set_levels(bool state)
{
  levels_on = state;
  generate_image();
  update();
}

void MVDisplay::hscroll(int pos)
{
  xc = pos;
  regen_pos();
  generate_image();
  update();
}

void MVDisplay::vscroll(int pos)
{
  yc = pos;
  regen_pos();
  generate_image();
  update();
}

void MVDisplay::state_changed()
{
  generate_image();
  update();
}
