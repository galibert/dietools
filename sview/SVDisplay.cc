#include "SVDisplay.h"
#include "SVMain.h"
#include "state.h"

#include <QtGui/QPainter>
#include <QMouseEvent>

SVDisplay::SVDisplay(QWidget *parent) : QWidget(parent)
{
  xc = -1;
  yc = -1;
  z = 32;
  qimg = 0;
  generated_image = NULL;
  ids_image = NULL;
}
  
SVDisplay::~SVDisplay()
{
  if(qimg) {
    delete qimg;
    delete[] generated_image;
    delete[] ids_image;
  }
}

void SVDisplay::generate_ids()
{
  int w = size().width();
  int h = size().height();

  memset(ids_image, 0, w*h*4);

  for(unsigned int i=0; i != nodes.size(); i++)
    nodes[i]->draw(ids_image, x0, y0, w, h, z);

  for(unsigned int i=0; i != nets.size(); i++)
    nets[i]->draw(ids_image, x0, y0, w, h, z);

  generate_image();
}

void SVDisplay::generate_image()
{
  int w = size().width();
  int h = size().height();

  const unsigned int *p = ids_image;
  unsigned int *q = generated_image;
  for(int y = 0; y != h; y++)
    for(int x = 0; x != w; x++) {
      unsigned int id = *p++;
      unsigned int c = 0xffffffff;
      if((id & node::TYPE_MASK) == node::NET_MARK) {
	int nid = id & node::ID_MASK;
	if(state.highlight[nid])
	  c = 0xffff00ff;
	else {
	  int pp = state.power[nid];
	  if(pp <= 50) {
	    unsigned char c1 = pp*255/50;
	    c = 0xff000000 | (c1 << 16);
	  } else {
	    unsigned char c1 = (pp-50)*255/50;
	    c = 0xffff0000 | (c1 << 8);
	  }
	}
      } else if(id)
	c = 0xff000000;
      *q++ = c;
    }

  switch(z) {
  case 1: {
    int xb = ((x0+9)/10)*10;
    int yb = ((y0+9)/10)*10;
    int x1 = x0+w;
    int y1 = y0+h;

    for(int x=xb; x<x1; x+=10) {
      unsigned int *d = generated_image + (x-x0);
      unsigned int c = x % 50 ? 0xffe0e0ff : 0xff8080ff;
      for(int y=0; y<h; y++) {
	if(*d == 0xffffffff)
	  *d = c;
	d += w;
      }
    }
    for(int y=yb; y<y1; y+=10) {
      unsigned int *d = generated_image + w*(y-y0);
      unsigned int c = y % 50 ? 0xffe0e0ff : 0xff8080ff;
      for(int x=0; x<w; x++) {
	if(*d == 0xffffffff)
	  *d = c;
	d ++;
      }
    }
    break;
  }
  case 2: {
    int xb = ((x0+9)/10)*10;
    int yb = ((y0+9)/10)*10;
    int x1 = x0+2*w;
    int y1 = y0+2*h;

    for(int x=xb; x<x1; x+=10) {
      unsigned int *d = generated_image + (x-x0)/2;
      unsigned int c = x % 50 ? 0xffe0e0ff : 0xff8080ff;
      for(int y=0; y<h; y++) {
	if(*d == 0xffffffff)
	  *d = c;
	d += w;
      }
    }
    for(int y=yb; y<y1; y+=10) {
      unsigned int *d = generated_image + w*((y-y0)/2);
      unsigned int c = y % 50 ? 0xffe0e0ff : 0xff8080ff;
      for(int x=0; x<w; x++) {
	if(*d == 0xffffffff)
	  *d = c;
	d ++;
      }
    }
    break;
  }
  case 4: {
    int xb = ((x0+49)/50)*50;
    int yb = ((y0+49)/50)*50;
    int x1 = x0+4*w;
    int y1 = y0+4*h;

    for(int x=xb; x<x1; x+=50) {
      unsigned int *d = generated_image + (x-x0)/4;
      unsigned int c = x % 100 ? 0xffe0e0ff : 0xff8080ff;
      for(int y=0; y<h; y++) {
	if(*d == 0xffffffff)
	  *d = c;
	d += w;
      }
    }
    for(int y=yb; y<y1; y+=50) {
      unsigned int *d = generated_image + w*((y-y0)/4);
      unsigned int c = y % 100 ? 0xffe0e0ff : 0xff8080ff;
      for(int x=0; x<w; x++) {
	if(*d == 0xffffffff)
	  *d = c;
	d ++;
      }
    }
    break;
  }
  case 8: {
    int xb = ((x0+49)/50)*50;
    int yb = ((y0+49)/50)*50;
    int x1 = x0+8*w;
    int y1 = y0+8*h;

    for(int x=xb; x<x1; x+=50) {
      unsigned int *d = generated_image + (x-x0)/8;
      unsigned int c = x % 100 ? 0xffe0e0ff : 0xff8080ff;
      for(int y=0; y<h; y++) {
	if(*d == 0xffffffff)
	  *d = c;
	d += w;
      }
    }
    for(int y=yb; y<y1; y+=50) {
      unsigned int *d = generated_image + w*((y-y0)/8);
      unsigned int c = y % 100 ? 0xffe0e0ff : 0xff8080ff;
      for(int x=0; x<w; x++) {
	if(*d == 0xffffffff)
	  *d = c;
	d ++;
      }
    }
    break;
  }
  }
}

void SVDisplay::paintEvent(QPaintEvent *e)
{
  if(!qimg)
    return;
  QPainter p(this);
  p.drawImage(0, 0, *qimg);
}

void SVDisplay::rewamp_image()
{
  int w = size().width();
  int h = size().height();

  svmain->set_steps(z*10, z*10, z*w/20, z*h/20);

  x0 = xc - z*w/2;
  y0 = yc - z*h/2;
  generate_ids();
}

void SVDisplay::resizeEvent(QResizeEvent *e)
{
  if(qimg) {
    delete qimg;
    delete[] generated_image;
    delete[] ids_image;
  }

  int w = size().width();
  int h = size().height();

  generated_image = new unsigned int[w*h];
  ids_image = new unsigned int[w*h];
  qimg = new QImage((uchar *)generated_image, w, h, QImage::Format_RGB32);
  rewamp_image();
}

void SVDisplay::mouseMoveEvent(QMouseEvent *e)
{
  int w = size().width();
  int h = size().height();
  int xc = e->x();
  int yc = e->y();
  int xs0 = xc < 10 ? 0 : xc-10;
  int ys0 = yc < 10 ? 0 : yc-10;
  int xs1 = xc >= w-10 ? w-1 : xc+10;
  int ys1 = yc >= h-10 ? h-1 : yc+10;

  unsigned int *ids = ids_image + ys0*w;
  int x = z*e->x();
  int y = z*e->y();

  unsigned int id = 0;
  int did = 0;
  for(int yy=ys0; yy<=ys1; yy++) {
    for(int xx=xs0; xx<=xs1; xx++) {
      unsigned int id1 = ids[xx];
      if(id1) {
	int dt = (xx-xc)*(xx-xc) + (yy-yc)*(yy-yc);
	if(id == 0 || dt < did) {
	  did = dt;
	  id = id1;
	}
      }
    }
    ids += w;
  }
  char msg[4096];
  char *p = msg + sprintf(msg, "%5d %5d %s", (x0+x)/10, 4666-(y0+y)/10, id_to_name(id).c_str());
  if((id & node::TYPE_MASK) == node::NET_MARK) {
    int pp = state.power[id & node::ID_MASK];
    p += sprintf(p, " %d.%dV", pp/10, pp%10);
  }
  svmain->set_status(msg);
}

void SVDisplay::mousePressEvent(QMouseEvent *e)
{
  int w = size().width();
  int h = size().height();
  int xc = e->x();
  int yc = e->y();
  int xs0 = xc < 10 ? 0 : xc-10;
  int ys0 = yc < 10 ? 0 : yc-10;
  int xs1 = xc >= w-10 ? w-1 : xc+10;
  int ys1 = yc >= h-10 ? h-1 : yc+10;

  unsigned int *ids = ids_image + ys0*w;

  unsigned int id = 0;
  int did = 0;
  for(int yy=ys0; yy<=ys1; yy++) {
    for(int xx=xs0; xx<=xs1; xx++) {
      unsigned int id1 = ids[xx];
      if(id1) {
	int dt = (xx-xc)*(xx-xc) + (yy-yc)*(yy-yc);
	if(id == 0 || dt < did) {
	  did = dt;
	  id = id1;
	}
      }
    }
    ids += w;
  }
  if((id & node::TYPE_MASK) == node::NET_MARK && state.selectable_net[id & node::ID_MASK])
    track(id & node::ID_MASK);
}

void SVDisplay::hscroll(int pos)
{
  xc = pos*10;
  x0 = xc - z*size().width()/2;
  generate_ids();
  update();
}

void SVDisplay::vscroll(int pos)
{
  yc = pos*10;
  y0 = yc - z*size().height()/2;
  generate_ids();
  update();
}

void SVDisplay::zoom_in()
{
  if(z > 1) {
    z /= 2;
    rewamp_image();
    update();
  }
}

void SVDisplay::zoom_out()
{
  if(z < 128) {
    z *= 2;
    rewamp_image();
    update();
  }
}

void SVDisplay::reload()
{
  state_load(schem_file);
  if(xc == -1 && yc == -1) {
    xc = state.sx*5;
    yc = state.sy*5;
    svmain->set_scroll_pos_range(xc, yc, state.sx*10, state.sy*10);
  }

  generate_ids();
  update();
}

void SVDisplay::state_changed()
{
  generate_image();
  update();
}
