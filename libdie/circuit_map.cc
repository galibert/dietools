#define _FILE_OFFSET_BITS 64
#undef _FORTIFY_SOURCE

#include "circuit_map.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
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

static void map_file_ro(const char *fname, unsigned char *&data, int64_t &size, bool accept_not_here)
{
  char msg[4096];
  #ifdef _WIN32
    HANDLE fd = CreateFile(fname, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if(fd == INVALID_HANDLE_VALUE) {
      if(GetLastError() == ERROR_FILE_NOT_FOUND && accept_not_here) {
        data = 0;
        size = 0;
        return;
      }
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg, 4096, NULL);
      fprintf(stderr, "CreateFile failed. Windows error code: %s", msg);
      exit(1);
    }
    size = GetFileSize(fd, NULL);
    HANDLE map = CreateFileMapping(fd, NULL, PAGE_READONLY, 0, 0, fname);
    data = (unsigned char *) MapViewOfFile(map, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(fd);
  #else
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
  #endif
}

static void create_file_rw(const char *fname, unsigned char *&data, int64_t size)
{
  char msg[4096];
  #ifdef _WIN32
    /* Try FILE_SHARED_READ if this fails. */
    HANDLE fd = CreateFile(fname, GENERIC_READ|GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if(fd == INVALID_HANDLE_VALUE) {
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg, 4096, NULL);
      fprintf(stderr, "CreateFile failed. Windows error code: %s", msg);
      exit(1);
    }

    LARGE_INTEGER full_size;
    full_size.QuadPart = size;
    HANDLE map = CreateFileMapping(fd, NULL, PAGE_READWRITE, full_size.HighPart, full_size.LowPart, fname);
    if(map == NULL) {
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg, 4096, NULL);
      fprintf(stderr, "CreateFileMapping failed. Windows error code: %s", msg);
      exit(1);
    }
    data = (unsigned char *) MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if(data == NULL) {
      FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msg, 4096, NULL);
      fprintf(stderr, "MapViewOfFile failed. Windows error code: %s", msg);
      exit(1);
    }
    CloseHandle(fd);
  #else
    sprintf(msg, "Error opening %s for writing", fname);
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
    int64_t size;
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
