#define _FILE_OFFSET_BITS 64
#undef _FORTIFY_SOURCE

#include "images.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#ifdef _WIN32
  #include <windows.h>
#else
  #include <sys/mman.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

void map_file_ro(const char *fname, unsigned char *&data, long &size, bool accept_not_here)
{
  char msg[4096];
  sprintf(msg, "Error opening %s for reading", fname);
  #ifdef _WIN32
    HANDLE fd = CreateFile(fname, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(fd == NULL) {
      if(GetLastError() == ERROR_FILE_NOT_FOUND && accept_not_here) {
        data = 0;
        size = 0;
        return;
      }
      perror(msg);
      exit(1);
    }
    size = GetFileSize(fd, NULL);
    HANDLE map = CreateFileMapping(fd, NULL, PAGE_READONLY, 0, 0, fname);
    data = (unsigned char *) MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(fd);
  #else
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
  #endif
}

void create_file_rw(const char *fname, unsigned char *&data, long size)
{
  char msg[4096];
  sprintf(msg, "Error opening %s for writing", fname);
  #ifdef _WIN32
    /* Try FILE_SHARED_READ if this fails. */
    HANDLE fd = CreateFile(fname, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS|TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(fd == NULL) {
      perror(msg);
      exit(1);
    }
    size = GetFileSize(fd, NULL);
    HANDLE map = CreateFileMapping(fd, NULL, PAGE_READWRITE, 0, 0, fname);
    data = (unsigned char *) MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    CloseHandle(fd);
  #else
    int fd = open(fname, O_RDWR|O_CREAT|O_TRUNC, 0666);
    if(fd < 0) {
      perror(msg);
      exit(1);
    }
    ftruncate(fd, size);
    data = (unsigned char *)mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
  #endif
}

void create_file_rw_header(const char *fname, unsigned char *&map_adr, unsigned char *&data, long &size, int dsize, const char *header)
{
  int hsize = strlen(header);
  size = hsize + dsize;
  create_file_rw(fname, map_adr, size);
  memcpy(map_adr, header, hsize);
  data = map_adr + hsize;
}

pbm::pbm(const char *fname)
{
  map_file_ro(fname, map_adr, size, false);
  assert(!memcmp(map_adr, "P4\n", 3));
  const char *p = (const char *)map_adr+3;
  while(*p == '#')
    p = strchr(p, '\n')+1;
  sx = strtol(p, 0, 10);
  sy = strtol(strchr(p, ' ')+1, 0, 10);
  img = (unsigned char *)strchr(p, '\n')+1;
  sxb = (sx+7) >> 3;
}

pbm::pbm(const char *fname, int _sx, int _sy, bool &created)
{
  map_file_ro(fname, map_adr, size, true);
  if(map_adr) {
    assert(!memcmp(map_adr, "P4\n", 3));
    const char *p = (const char *)map_adr+3;
    while(*p == '#')
      p = strchr(p, '\n')+1;
    sx = strtol(p, 0, 10);
    sy = strtol(strchr(p, ' ')+1, 0, 10);
    assert(sx == _sx && sy == _sy);
    img = (unsigned char *)strchr(p, '\n')+1;
    sxb = (sx+7) >> 3;
    created = false;

  } else {
    sx = _sx;
    sy = _sy;
    sxb = (sx+7) >> 3;

    char header[256];
    sprintf(header, "P4\n%d %d\n", sx, sy);
    create_file_rw_header(fname, map_adr, img, size, sxb*sy, header);
    memset(img, 0xff, sxb*sy);
    created = true;
  }
}

pbm::pbm(int _sx, int _sy)
{
  sx = _sx;
  sy = _sy;
  sxb = (sx+7) >> 3;
  map_adr = NULL;
  img = new unsigned char[sxb*sy];
  memset(img, 0xff, sxb*sy);
  size = 0;
}

pbm::~pbm()
{
  if(map_adr)
    #ifdef _WIN32
      UnmapViewOfFile(map_adr);
    #else
      munmap(map_adr, size);
    #endif
}

pgm::pgm(const char *fname)
{
  map_file_ro(fname, map_adr, size, false);
  assert(!memcmp(map_adr, "P5\n", 3));
  const char *p = (const char *)map_adr+3;
  while(*p == '#')
    p = strchr(p, '\n')+1;
  sx = strtol(p, 0, 10);
  sy = strtol(strchr(p, ' ')+1, 0, 10);
  img = (unsigned char *)strchr(strchr(p, '\n')+1, '\n')+1;
}

pgm::pgm(const char *fname, int _sx, int _sy, bool &created)
{
  map_file_ro(fname, map_adr, size, true);
  if(map_adr) {
    assert(!memcmp(map_adr, "P5\n", 3));
    const char *p = (const char *)map_adr+3;
    while(*p == '#')
      p = strchr(p, '\n')+1;
    sx = strtol(p, 0, 10);
    sy = strtol(strchr(p, ' ')+1, 0, 10);
    assert(sx == _sx && sy == _sy);
    img = (unsigned char *)strchr(strchr(p, '\n')+1, '\n')+1;
    created = false;

  } else {
    sx = _sx;
    sy = _sy;

    char header[256];
    sprintf(header, "P5\n%d %d\n255\n", sx, sy);
    create_file_rw_header(fname, map_adr, img, size, sx*sy, header);
    created = true;
  }
}

pgm::~pgm()
{
  #ifdef _WIN32
    UnmapViewOfFile(map_adr);
  #else
    munmap(map_adr, size);
  #endif
}

ppm::ppm(const char *fname)
{
  map_file_ro(fname, map_adr, size, false);
  assert(!memcmp(map_adr, "P6\n", 3));
  const char *p = (const char *)map_adr+3;
  while(*p == '#')
    p = strchr(p, '\n')+1;
  sx = strtol(p, 0, 10);
  sy = strtol(strchr(p, ' ')+1, 0, 10);
  img = (unsigned char *)strchr(strchr(p, '\n')+1, '\n')+1;
}

ppm::ppm(const char *fname, int _sx, int _sy, bool &created)
{
  map_file_ro(fname, map_adr, size, true);
  if(map_adr) {
    assert(!memcmp(map_adr, "P6\n", 3));
    const char *p = (const char *)map_adr+3;
    while(*p == '#')
      p = strchr(p, '\n')+1;
    sx = strtol(p, 0, 10);
    sy = strtol(strchr(p, ' ')+1, 0, 10);
    assert(sx == _sx && sy == _sy);
    img = (unsigned char *)strchr(strchr(p, '\n')+1, '\n')+1;
    created = false;

  } else {
    sx = _sx;
    sy = _sy;

    char header[256];
    sprintf(header, "P6\n%d %d\n255\n", sx, sy);
    create_file_rw_header(fname, map_adr, img, size, 3*sx*sy, header);
    created = true;
  }
}

ppm::~ppm()
{
  #ifdef _WIN32
    UnmapViewOfFile(map_adr);
  #else
    munmap(map_adr, size);
  #endif
}
