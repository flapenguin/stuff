#pragma once

#define DUMPF(x, prefix, suffix) _Generic((x), \
  char: prefix "%c" suffix, \
  signed char: prefix "%hhd" suffix, \
  unsigned char: prefix "%hhu" suffix, \
  signed short: prefix "%hd" suffix, \
  unsigned short: prefix "%hu" suffix, \
  signed int: prefix "%d" suffix, \
  unsigned int: prefix "%u" suffix, \
  long int: prefix "%ld" suffix, \
  unsigned long int: prefix "%lu" suffix, \
  long long int: prefix "%lld" suffix, \
  unsigned long long int: prefix "%llu" suffix, \
  float: prefix "%f" suffix, \
  double: prefix "%f" suffix, \
  long double: prefix "%Lf" suffix, \
  char *: prefix "\"%s\"" suffix, \
  void *: prefix "%p" suffix \
)
