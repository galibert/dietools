#define _FILE_OFFSET_BITS 64
#undef _FORTIFY_SOURCE

#include "circuit_map.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static void map_file_ro(const char *fname, unsigned char *&data, long &size, bool accept_not_here)
{
  char msg[4096];
  sprintf(msg, "Error opening %s for reading", fname);
  int fd = open(fname, O_RDONLY);
  if(fd < 0) {
    if(errno == ENOENT && accept_not_here) {
      data = 0;
      size = 0;
      return;
    }
    perror(msg);
    exit(1);
  }
  size = lseek(fd, 0, SEEK_END);
  data = (unsigned char *)mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
  close(fd);
}

static void create_file_rw(const char *fname, unsigned char *&data, long size)
{
  char msg[4096];
  sprintf(msg, "Error opening %s for writing", fname);
  int fd = open(fname, O_RDWR|O_CREAT|O_TRUNC, 0666);
  if(fd < 0) {
    perror(msg);
    exit(1);
  }
  ftruncate(fd, size);
  data = (unsigned char *)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
  close(fd);
}

circuit_map::circuit_map(const char *fname, int _sx, int _sy, bool create)
{
  sx = _sx;
  sy = _sy;
  unsigned char *map_adr;
  if(create) {
    create_file_rw(fname, map_adr, long(3)*4*sx*sy);
    memset(map_adr, 0xff, long(3)*sx*sy*4);

  } else {
    long size;
    map_file_ro(fname, map_adr, size, false);
  }

  data = (int *)map_adr;
}

circuit_map::~circuit_map()
{
  munmap(data, long(3)*sx*sy*4);
}
