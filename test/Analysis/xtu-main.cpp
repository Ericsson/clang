// RUN: mkdir -p %T/xtudir
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-pch -o %T/xtudir/xtu-other.cpp.ast %S/Inputs/xtu-other.cpp
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -emit-pch -o %T/xtudir/xtu-chain.cpp.ast %S/Inputs/xtu-chain.cpp
// RUN: cp %S/Inputs/externalFnMap.txt %T/xtudir/
// RUN: %clang_cc1 -triple x86_64-pc-linux-gnu -fsyntax-only -analyze -analyzer-checker=core,debug.ExprInspection -analyzer-config xtu-dir=%T/xtudir -analyzer-config reanalyze-xtu-visited=true -verify %s

void clang_analyzer_eval(int);

int f(int);
int g(int);
int h(int);

int callback_to_main(int x) { return x + 1; }

int main() {
  clang_analyzer_eval(f(3) == 2); // expected-warning{{TRUE}}
  clang_analyzer_eval(f(4) == 3); // expected-warning{{TRUE}}
  clang_analyzer_eval(f(5) == 3); // expected-warning{{FALSE}}
  clang_analyzer_eval(g(4) == 6); // expected-warning{{TRUE}}
  clang_analyzer_eval(h(2) == 8); // expected-warning{{TRUE}}
}
