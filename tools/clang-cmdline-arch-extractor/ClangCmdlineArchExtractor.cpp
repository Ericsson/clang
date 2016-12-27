//===- ClangCmdlineArchExtractor.cpp ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//
//
// Clang tool which prints architecture type for a given command line.
//
//===--------------------------------------------------------------------===//

#include "clang/Basic/LLVM.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/Utils.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include <string>
#include <vector>

using namespace llvm;
using namespace clang;

static std::string getTripleSuffix(const Triple &Triple) {
  // We are not going to support vendor and don't support OS and environment.
  // FIXME: support OS and environment correctly
  Triple::ArchType T = Triple.getArch();
  if (T == Triple::thumb)
    T = Triple::arm;
  return Triple.getArchTypeName(T);
}

int main(int argc, const char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0], true);
  PrettyStackTraceProgram X(argc, argv);

  SmallVector<StringRef, 4> Sources;
  std::vector<const char*> Args;
  const StringRef cppFile = ".cpp", ccFile = ".cc", cFile = ".c",
                  cxxFile = ".cxx";
  for (int i = 1; i < argc; i++) {
    StringRef Arg = argv[i];
    if (Arg.endswith(cppFile) || Arg.endswith(ccFile) || Arg.endswith(cFile) ||
        Arg.endswith(cxxFile)) {
      Sources.push_back(argv[i]);
    } else {
      Args.push_back(argv[i]);
    }
  }

  if (Sources.empty())
    return 1;

  Args.push_back(Sources[0].data());
  std::unique_ptr<CompilerInvocation> CI(
      createInvocationFromCommandLine(Args));

  const std::string Suffix =
      "@" + getTripleSuffix(llvm::Triple(CI->getTargetOpts().Triple));

  for (StringRef SourceFile : Sources) {
    const char *Path = realpath(SourceFile.data(), nullptr);
    if (Path)
      outs() << Path << Suffix << " ";
  }

  return 0;
}
