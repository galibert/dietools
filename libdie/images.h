#ifndef IMAGES_H
#define IMAGES_H

struct ppm {
  unsigned char *img;
  int sx;
  int sy;

  unsigned char *map_adr;
  long size;

  unsigned char *p(int x, int y) const {
    return img+3*(y*sx+x);
  }

  unsigned char gr(int x, int y) const {
    const unsigned char *pix = p(x, y);
    return (77*pix[0] + 151*pix[1] + 28*pix[2]) >> 8;
  }

  ppm(const char *fname);
  ppm(const char *fname, int sx, int sy, bool &created);
  ~ppm();
};

struct pgm {
  unsigned char *img;
  int sx;
  int sy;

  unsigned char *map_adr;
  long size;

  unsigned char &p(int x, int y) {
    return img[y*sx+x];
  }

  pgm(const char *fname);
  pgm(const char *fname, int sx, int sy, bool &created);
  ~pgm();
};

struct pbm {
  unsigned char *img;
  int sx, sxb;
  int sy;

  unsigned char *map_adr;
  long size;

  bool p(int x, int y) const {
    return !((img[y*sxb + (x >> 3)] >> (7^(x & 7))) & 1);
  }

  void s(int x, int y, bool v) {
    if(v)
      img[y*sxb + (x >> 3)] &= ~(0x80 >> (x & 7));
    else
      img[y*sxb + (x >> 3)] |= 0x80 >> (x & 7);
  }
  pbm(const char *fname);
  pbm(const char *fname, int sx, int sy, bool &created);
  pbm(int sx, int sy);
  ~pbm();
};

void map_file_ro(const char *fname, unsigned char *&data, long &size, bool accept_not_here);
void create_file_rw(const char *fname, unsigned char *&data, long size);
void create_file_rw_header(const char *fname, unsigned char *&map_adr, unsigned char *&data, long &size, int dsize, const char *header);

#endif
