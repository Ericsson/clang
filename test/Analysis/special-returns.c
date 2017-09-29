// RUN: %clang_analyze_cc1 -analyzer-checker=core,api.SpecialReturnValue -analyzer-config api-metadata-path="%S"/Inputs -verify %s
// RUN: %clang_analyze_cc1 -analyzer-checker=core,api.SpecialReturnValue -analyzer-config api-metadata-path="%S" %s 2>&1 | FileCheck %s -check-prefix=BADPATH
// RUN: %clang_analyze_cc1 -analyzer-checker=core,api.SpecialReturnValue %s 2>&1 | FileCheck %s -check-prefix=BADPATH

// BADPATH: warning: Could not find API data for api.SpecialReturnValue, skipping checks

#define NULL 0

int negative_return();
int nonnegative_return();
int *null_return();
int *nonnull_return();

void good_negative() {
  int n = negative_return();
  if (n < 0)
    return;
  int v[n]; // no-warning
}

void bad_negative() {
  int n = negative_return();
  int v[n]; // expected-warning {{Declared variable-length array (VLA) has negative size}}
}

void nonnegative() {
  int n = nonnegative_return();
  int v[n]; // no-warning
}

void good_null() {
  int *p = null_return();
  if (p == NULL)
    return;
  int n = *p; // no-warning
}

void bad_null() {
  int *p = null_return();
  int n = *p; //expected-warning {{Dereference of null pointer (loaded from variable 'p')}}
}

void nonnull() {
  int *p = nonnull_return();
  int n = *p; // no-warning
}
