// RUN: %clang_cc1 -analyze -analyzer-checker=alpha.security.taint,alpha.security.DirtyScalar -verify -analyzer-config alpha.security.DirtyScalar:criticalOnly=true %s
// RUN: %clang_cc1 -analyze -analyzer-checker=alpha.security.taint,alpha.security.DirtyScalar -verify -analyzer-config alpha.security.DirtyScalar:criticalOnly=false -DDIRTYSCALARSTRICT=1 %s

#include "Inputs/system-header-simulator.h"

typedef long ssize_t;

ssize_t recv(int s, void *buf, size_t len, int flags);

void gets_tainted_ival(int val) {
  (void)val;
}

void gets_tainted_uval(unsigned int val) {
  (void)val;
}

int tainted_usage() {
  int size;
  scanf("%d", &size);
  char *buff = new char[size]; // expected-warning{{Tainted variable is used without proper bound checking}}
  for (int i = 0; i < size; ++i) {
#if DIRTYSCALARSTRICT
// expected-warning@-2{{Tainted variable is used without proper bound checking}}
#endif
    scanf("%d", &buff[i]);
  }
  buff[size - 1] = 0;     // expected-warning{{Tainted variable is used without proper bound checking}}
  *(buff + size - 2) = 0; // expected-warning{{Tainted variable is used without proper bound checking}}
  gets_tainted_ival(size);
#if DIRTYSCALARSTRICT
// expected-warning@-2{{Tainted variable is used without proper bound checking}}
#endif

  return 0;
}

int tainted_usage_checked() {
  int size;
  scanf("%d", &size);
  if (size < 0 || size > 255)
    return -1;
  char *buff = new char[size];     // no warning
  for (int i = 0; i < size; ++i) { // no warning
    scanf("%d", &buff[i]);         // no warning
  }
  buff[size - 1] = 0;      // no warning
  *(buff + size - 2) = 0;  // no warning
  gets_tainted_ival(size); // no warning

  unsigned int idx;
  scanf("%d", &idx);
  if (idx > 255)
    return -1;
  gets_tainted_uval(idx); // no warning

  return 0;
}

int detect_tainted(char const **messages) {
  int sock, index;
  scanf("%d", &sock);
  if (recv(sock, &index, sizeof(index), 0) != sizeof(index)) {
#if DIRTYSCALARSTRICT
// expected-warning@-2{{Tainted variable is used without proper bound checking}}
#endif
    return -1;
  }
  int index2 = index;
  printf("%s\n", messages[index]);  // expected-warning{{Tainted variable is used without proper bound checking}}
  printf("%s\n", messages[index2]); // expected-warning{{Tainted variable is used without proper bound checking}}

  return 0;
}

int skip_sizes_likely_used_for_table_access(char const **messages) {
  int sock;
  char byte;

  scanf("%d", &sock);
  if (recv(sock, &byte, sizeof(byte), 0) != sizeof(byte)) {
#if DIRTYSCALARSTRICT
// expected-warning@-2{{Tainted variable is used without proper bound checking}}
#endif
    return -1;
  }
  char byte2 = byte;
  printf("%s\n", messages[byte]);  // no warning
  printf("%s\n", messages[byte2]); // no warning

  return 0;
}

