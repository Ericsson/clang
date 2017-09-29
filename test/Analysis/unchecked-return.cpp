// RUN: %clang_analyze_cc1 -std=c++11 -analyzer-checker=core,api.UncheckedReturnValue -analyzer-config api-metadata-path="%S"/Inputs -verify %s
// RUN: %clang_analyze_cc1 -std=c++11 -analyzer-checker=core,api.UncheckedReturnValue -analyzer-config api-metadata-path="%S" %s 2>&1 | FileCheck %s -check-prefix=BADPATH
// RUN: %clang_analyze_cc1 -std=c++11 -analyzer-checker=core,api.UncheckedReturnValue %s 2>&1 | FileCheck %s -check-prefix=BADPATH

// BADPATH: warning: Could not find API data for api.UncheckedReturnValue, skipping checks

int inData1();
int inData2();
namespace ns {
  int inData3();
}
template<class T> T templateTest();

int notInData1();
int notInData2();

void f0() {
  int x = inData1(); // no-warning
  inData1(); // expected-warning{{Return value is not checked in call to 'inData1'}}
  inData2(); // expected-warning{{Return value is not checked in call to 'inData2'}}
  ns::inData3(); // expected-warning{{Return value is not checked in call to 'inData3'}}

  templateTest<int>(); // expected-warning{{Return value is not checked in call to 'templateTest'}}

  notInData1(); // no-warning
  notInData2(); // no-warning
}
