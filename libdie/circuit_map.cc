#define _FILE_OFFSET_BITS 64
#undef _FORTIFY_SOURCE

#include "circuit_map.h"

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

static void map_file_ro(const char *fname, unsigned char *&data, long &size, bool accept_not_here)
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

static void create_file_rw(const char *fname, unsigned char *&data, long size)
{
  char msg[4096];
  sprintf(msg, "Error opening %s for writing", fname);
  #ifdef _WIN32
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

circuit_map::circuit_map(const char *fname, int _nl, int _sx, int _sy, bool create)
{
  sx = _sx;
  sy = _sy;
  nl = _nl;
  unsigned char *map_adr;
  if(create) {
    create_file_rw(fname, map_adr, long(nl)*4*sx*sy);
    memset(map_adr, 0xff, long(nl)*sx*sy*4);

  } else {
    long size;
    map_file_ro(fname, map_adr, size, false);
  }

  data = (int *)map_adr;
}

circuit_map::~circuit_map()
{
  #ifdef _WIN32
    UnmapViewOfFile(data);
  #else
    munmap(data, long(nl)*sx*sy*4);
  #endif
}
