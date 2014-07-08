#ifndef FILL_H
#define FILL_H

#include <boost/function.hpp>
#include <boost/bind.hpp>

void fill(int x, int y, int sx, int sy, int color, boost::function<void(int, int)> set_pixel, boost::function<int(int, int)> read_color, boost::function<bool(int, int)> test_pixel);

#endif
