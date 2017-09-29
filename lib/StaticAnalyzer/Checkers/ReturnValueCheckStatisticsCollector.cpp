//==- ReturnValueCheckStatisticsCollector.cpp --------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// This checker collects statistics about calls whether their return value
// is used (assigned, compared or passed as argument).  Warnings emitted are
// not for human consumption. Instead, the output of the checker must be piped
// into `tools/gen_yaml_for_return_value_checks.sh` in order to generate file
// `UncheckedReturn.yaml` for checker `api.UncheckedReturn`.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"

using namespace clang;
using namespace ento;

namespace {
class ReturnValueCheckVisitor : public StmtVisitor<ReturnValueCheckVisitor> {

  BugReporter &BR;
  AnalysisDeclContext *AC;
  const CheckName &CN;
  llvm::DenseMap<const CallExpr *, bool> Calls;

public:
  ReturnValueCheckVisitor(BugReporter &br, AnalysisDeclContext *ac,
                          const CheckName &cn)
      : BR(br), AC(ac), CN(cn) {}

  ~ReturnValueCheckVisitor() {
    for (const auto Call : Calls) {
      const FunctionDecl *FD = Call.first->getDirectCallee();
      if (!FD)
        continue;

      std::string FullName = FD->getQualifiedNameAsString();
      if (FullName.empty())
        continue;

      const auto &SM = AC->getASTContext().getSourceManager();
      SmallString<1024> buf;
      llvm::raw_svector_ostream os(buf);
      os << "Return Value Check:" << Call.first->getLocStart().printToString(SM)
         << "," << *FD << "," << (unsigned)Call.second << "\n";

      const char *bugType = "Statistics";

      PathDiagnosticLocation CELoc = PathDiagnosticLocation::createBegin(
          Call.first, BR.getSourceManager(), AC);

      BR.EmitBasicReport(AC->getDecl(), CN, bugType, "API", os.str(), CELoc);
    }
  }

  void VisitStmt(Stmt *S);
  void VisitCallExpr(CallExpr *CE);
  void VisitCompoundStmt(CompoundStmt *S);
};

} // namespace

void ReturnValueCheckVisitor::VisitStmt(Stmt *S) {
  for (Stmt *Child : S->children()) {
    if (Child) {
      Visit(Child);
    }
  }
}

void ReturnValueCheckVisitor::VisitCallExpr(CallExpr *CE) {
  Calls[CE];
  for (Stmt *Child : CE->children()) {
    if (Child) {
      Visit(Child);
    }
  }
}

void ReturnValueCheckVisitor::VisitCompoundStmt(CompoundStmt *S) {
  for (Stmt *Child : S->children()) {
    if (CallExpr *CE = dyn_cast_or_null<CallExpr>(Child)) {
      Calls[CE] = true;
    }
    Visit(Child);
  }
}

namespace {
class ReturnValueCheckStatisticsCollector : public Checker<check::ASTCodeBody> {
public:
  ReturnValueCheckStatisticsCollector() {}

  void checkASTCodeBody(const Decl *D, AnalysisManager &Mgr,
                        BugReporter &BR) const {
    ReturnValueCheckVisitor visitor(BR, Mgr.getAnalysisDeclContext(D),
                                    getCheckName());
    visitor.Visit(D->getBody());
  }
};
} // namespace

void ento::registerReturnValueCheckStatisticsCollector(CheckerManager &mgr) {
  mgr.registerChecker<ReturnValueCheckStatisticsCollector>();
}
