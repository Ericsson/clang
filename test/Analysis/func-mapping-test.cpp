// RUN: %clang_func_map %s -- -target x86_64-unknown-linux-gnu | FileCheck %s

int f(int) {
  return 0;
}

// CHECK: _Z1fi
