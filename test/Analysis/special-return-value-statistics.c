// RUN: %clang_analyze_cc1 -analyzer-checker=statisticsCollector.SpecialReturnValue %s 2>&1 | "%S"/../../tools/statistics/gen_yaml_for_special_return_values.sh | FileCheck %s 

int negative_return();
int nonnegative_return();
int *null_return();
int *nonnull_return();

#define NULL 0

void checked_negative1() {
  if (negative_return() < 0) {}
}

void checked_negative2() {
  if (negative_return() >= 0) {}
}

void checked_negative3_4() {
  int n = negative_return();
  if (n < 0) {}
  if (negative_return() == n) {}
}

void checked_negative5_6() {
  int n = negative_return();
  if (n >= 0) {}
  if (negative_return() >= n) {}
}

void unchecked_negative() {
  int n = negative_return();
}

void checked_null1() {
  if (null_return() == NULL) {}
}

void checked_null2() {
  if (null_return() != NULL) {}
}

void checked_null3_4() {
  int *n = null_return();
  if (n == NULL) {}
  if (null_return() == n) {}
}

void checked_null5_6() {
  int *n = null_return();
  if (n != NULL) {}
  if (null_return() == n) {}
}

void unchecked_null() {
  int *n = null_return();
}

// CHECK: #
// CHECK-NEXT: # SpecialReturn metadata format 1.0
// CHECK: {name: negative_return, relation: LT, value: 0} 
// CHECK: {name: null_return, relation: EQ, value: 0} 
