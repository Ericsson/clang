// RUN: mkdir -p %T/xtudir2
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-pch -o %T/xtudir2/xtu-other.c.ast %S/Inputs/xtu-other.c
// RUN: cp %S/Inputs/externalFnMap2.txt %T/xtudir2/externalFnMap.txt
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fsyntax-only -analyze -analyzer-checker=core,debug.ExprInspection -analyzer-config xtu-dir=%T/xtudir2 -analyzer-config reanalyze-xtu-visited=true -verify %s

void clang_analyzer_eval(int);

typedef struct {
  int a;
  int b;
} foobar;

int f(int);
extern foobar fb;

int main() {
  clang_analyzer_eval(f(5) == 1); // expected-warning{{TRUE}}
  return 0;
}
