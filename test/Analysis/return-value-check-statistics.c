// RUN: %clang_analyze_cc1 -analyzer-checker=statisticsCollector.ReturnValueCheck %s 2>&1 | "%S"/../../tools/statistics/gen_yaml_for_return_value_checks.sh | FileCheck %s 

int to_check();
int not_to_check();

int check();

void assign() {
  int n = to_check();
}

void cond() {
  if(to_check()) {}
}

void loop1() {
  while(to_check()) {}
}

void loop2() {
  do {} while(to_check());
}

void loop3() {
  for(;to_check(););
}

void compare1() {
  if (to_check() >= 0) {}
}

void compare2() {
  if (to_check < 0) {}
}

void arg() {
  check(to_check());
}

void oops() {
  to_check();
}

void ok() {
  not_to_check();
}

void unnecessary() {
  if(not_to_check()) {}
}

// CHECK: #
// CHECK-NEXT: # UncheckedReturn metadata format 1.0
// CHECK: - to_check
