#undef _FORTIFY_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include <librsvg/rsvg.h>
//#include <librsvg/rsvg-cairo.h>
#include <cairo.h>

#include <vector>
#include <string>
#include <map>

#include <reader.h>

using namespace std;

map<string, int> layer_offsets;
int sx, sx8, sy;

void bad_pixel(const char *layer, int pix, int p2, const unsigned char *src)
{
  int x = (pix % sx8) * 8 + p2;
  int y = sy - 1 - pix / sx8;
  fprintf(stderr, "Bad pixel in layer %s, x=%d y=%d color=#%02x%02x%02x%02x\n", layer, x, y, src[3], src[2], src[1], src[0]);
  exit(1);
}

static int iabs(int v)
{
  return v >= 0 ? v : -v;
}

void test_pixel_blue(const char *layer, int pix, int p2, const unsigned char *src)
{
  if(iabs(src[3] - src[0]) > 5 || src[1] != src[2])
    bad_pixel(layer, pix, p2, src);
}

void test_pixel_red(const char *layer, int pix, int p2, const unsigned char *src)
{
  if(iabs(src[3] - src[2]) > 10 || src[0] != src[1])
    bad_pixel(layer, pix, p2, src);
}

void test_pixel_black(const char *layer, int pix, int p2, const unsigned char *src)
{
  if(src[2] || src[1] || src[0])
    bad_pixel(layer, pix, p2, src);
}

void test_pixel_pink(const char *layer, int pix, int p2, const unsigned char *src)
{
  if(src[2] != src[3] || src[1] || src[0] != src[3])
    bad_pixel(layer, pix, p2, src);
}

void test_pixel_vias(const char *layer, int pix, int p2, const unsigned char *src)
{
  if(src[0] != src[1] || src[0] != src[2])
    bad_pixel(layer, pix, p2, src);
}

void test_pixel_green(const char *layer, int pix, int p2, const unsigned char *src)
{
  if(src[0] != src[2])
    bad_pixel(layer, pix, p2, src);
}

struct test_f {
  const char *name;
  void (*test_function)(const char *, int, int, const unsigned char *);
};

test_f test_functions[] = {
  { "blue",  test_pixel_blue },
  { "red",   test_pixel_red },
  { "black", test_pixel_black },
  { "pink",  test_pixel_pink },
  { "vias",  test_pixel_vias },
  { "green", test_pixel_green },
  { NULL,    NULL },
};

int main(int argc, char **argv)
{
  if(argc != 2) {
    fprintf(stderr, "Usage:\n%s config.txt\n", argv[0]);
    exit(1);
  }

  reader rd(argv[1]);
  const char *fname = rd.gw();
  sx = rd.gi();
  sy = rd.gi();
  rd.nl();
  sx8 = (sx+7)/8;

  char buf[4096];
  vector<char> svg;
  vector<unsigned char> pbm;

  pbm.resize(256);
  int off = sprintf((char *)&pbm[0], "P4\n%d %d\n", sx, sy);

  pbm.resize(off + sx8*sy);

  sprintf(buf, "Open %s", fname);
  int fd = open(fname, O_RDONLY);
  if(fd<0) {
    perror(buf);
    exit(2);
  }

  int size = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);

  svg.resize(size);
  read(fd, &svg[0], size);
  close(fd);

  for(int pos=0; pos < int(svg.size()) - 32; pos++) {
    if(!memcmp(&svg[pos], "inkscape:label=\"", 16)) {
      int l = 0;
      while(l < 511) {
	if(svg[pos+16+l] == '"')
	  break;
	buf[l] = svg[pos+16+l];
	l++;
      }
      buf[l] = 0;
      while(memcmp(&svg[pos], "style=\"display:", 15))
	pos++;
      pos += 15;
      if(!memcmp(&svg[pos], "none\"", 5)) {
	svg.resize(svg.size()+2);
	memmove(&svg[pos]+2, &svg[pos], svg.size()-2-pos);
      }
      memcpy(&svg[pos], "none\"  ", 7);

      layer_offsets[buf] = pos;
      fprintf(stderr, "layer [%s]\n", buf);
    }
  }

  g_type_init();
  rsvg_set_default_dpi(72.0);
  cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, sx, sy);
  cairo_t *cr = cairo_create(surface);
  while(!rd.eof()) {
    const char *image_name = rd.gw();
    const char *test_name = rd.gw();
    const char *layer_name = rd.gwnl();
    rd.nl();

    printf("%s\n", layer_name);
    fflush(stdout);
    for(map<string, int>::const_iterator j = layer_offsets.begin(); j != layer_offsets.end(); j++)
      memcpy(&svg[j->second], j->first == layer_name ? "inline\"" : "none\"  ", 7);

    GError *error = NULL;
    RsvgHandle *handle = rsvg_handle_new_from_data((const guint8 *)&svg[0], svg.size(), &error);
    if(error != NULL) {
      fprintf(stderr, "rvsg parsing failure %s\n", error->message);
      exit(1);
    }
    cairo_save (cr);
    cairo_set_source_rgba (cr, 0, 0, 0, 0);
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    cairo_paint (cr);
    cairo_restore (cr);
    rsvg_handle_render_cairo(handle, cr);

    cairo_status_t status = cairo_status(cr);
    if(status) {
      fprintf(stderr, "Cairo failure: %s\n", cairo_status_to_string(status));
      exit(1);
    }

    cairo_surface_flush(surface);
    const unsigned char *src = cairo_image_surface_get_data(surface);
    unsigned char *dst = &pbm[off];

    void (*test_pixel)(const char *layer, int, int, const unsigned char *) = NULL;
    for(int j=0; test_functions[j].name; j++)
      if(!strcmp(test_name, test_functions[j].name)) {
	test_pixel = test_functions[j].test_function;
	break;
      }
    if(!test_pixel) {
      fprintf(stderr, "Error: test function %s not found\n", test_name);
      exit(1);
    }

    for(int pix=0; pix<sx8*sy; pix++) {
      unsigned char v = 0;
      for(int p2=0; p2<8; p2++) {
	if((pix % sx8)*8 + p2 >= sx)
	  v |= 0x80 >> p2;
	else {
	  if(!src[3])
	    v |= 0x80 >> p2;
	  else
	    test_pixel(layer_name, pix, p2, src);
	  src += 4;
	}
      }
      *dst++ = v;
    }

    sprintf(buf, "%s.pbm", image_name);

    fd = open(buf, O_RDWR|O_CREAT|O_TRUNC, 0666);
    if(fd<0) {
      perror(buf);
      exit(2);
    }

    write(fd, &pbm[0], pbm.size());
    close(fd);

    rsvg_handle_close(handle, &error);
  }

  cairo_destroy(cr);
  cairo_surface_destroy(surface);

  return 0;
}
