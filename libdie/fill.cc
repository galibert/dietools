#include "fill.h"

#include <list>

using namespace std;

struct fill_coord {
  int x, y;
  fill_coord(int _x, int _y) { x = _x; y = _y; }
};


static void fill_1(list<fill_coord> &fc, int x, int y, int sx, int sy, int color,
		   boost::function<void(int, int)> set_pixel,
		   boost::function<int(int, int)> read_color,
		   boost::function<bool(int, int)> test_pixel)
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
