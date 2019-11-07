#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include "wasm.h"

WASM_FORMAT_ATTRIBUTE int wasm_die(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);

  exit(1);
}

WASM_FORMAT_ATTRIBUTE int wasm_log(const char* format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(stdout, format, args);
  va_end(args);
}

int main(int argc, const uint8_t* argv[]) {
  if (argc != 2) wasm_die("Usage: %s input.wasm\n", argv[0]);

  int fd = open(argv[1], O_RDONLY);
  if (fd == -1) wasm_die("%s - no such file\n", argv[1]);

  struct stat stat;
  fstat(fd, &stat);
  uint8_t* content = mmap(0, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

  struct wasm_module module = parse_module(content, stat.st_size);

  return 0;
}
