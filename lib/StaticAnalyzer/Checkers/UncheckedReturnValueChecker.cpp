//==- UncheckedReturnValueChecker.cpp ----------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// This checker finds calls to functions where the return value of the called
// function should be checked but it is not used in any way: not stored,
// not compared to some value, not passed as argument of another function etc.
//
// The names of functions whose return value is to be checked must be listed
// in a YAML file called `UncheckedReturn.yaml`. The location of this file
// must be passed to the checker as analyzer option `api-metadata-path`.
//
// Example YAML file:
//
//--- UncheckedReturn.yaml ---------------------------------------------------//
//
// #
// # UncheckedReturn metadata format 1.0
//
// - functionName1
// - functionName2
// - namespaceName::functionName3
//
//----------------------------------------------------------------------------//
//
// To auto-generate this YAML file on statistical base see checker
// `statisticsCollector.ReturnValueCheck`.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/StaticAnalyzer/Checkers/LoadMetadata.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/YAMLTraits.h"

using namespace clang;
using namespace ento;

namespace {
class UncheckedReturnValueVisitor :
    public StmtVisitor<UncheckedReturnValueVisitor> {

  BugReporter &BR;
  AnalysisDeclContext* AC;
  const CheckName &CN;
  void checkUncheckedReturnValue(CallExpr *CE);
  
public:
  UncheckedReturnValueVisitor(BugReporter &br, AnalysisDeclContext* ac,
                              const CheckName &cn)
      : BR(br), AC(ac), CN(cn) {}

  void VisitCompoundStmt(CompoundStmt *S);
};

}

void UncheckedReturnValueVisitor::VisitCompoundStmt(CompoundStmt *S) {
  for (Stmt *Child : S->children()) {
    if (Child) {
      if (CallExpr *CE = dyn_cast<CallExpr>(Child)) {
        checkUncheckedReturnValue(CE);
      }
      Visit(Child);
    }
  }
}

static llvm::StringSet<> FuncsReturningError;

void UncheckedReturnValueVisitor::checkUncheckedReturnValue(CallExpr *CE) {
  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return;
  std::string FullName = FD->getQualifiedNameAsString();
  if (FullName.empty())
    return;

  if (!FuncsReturningError.count(FullName))
    return;

  // Issue a warning.
  SmallString<256> buf;
  llvm::raw_svector_ostream os(buf);
  os << "Return value is not checked in call to '" << *FD << '\'';

  PathDiagnosticLocation CELoc =
      PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);

  BR.EmitBasicReport(AC->getDecl(), CN, "Unchecked return value", "API",
                     os.str(), CELoc);
}

namespace {
class UncheckedReturnValueChecker : public Checker<check::ASTCodeBody> {
public:
  UncheckedReturnValueChecker() {}

  void checkASTCodeBody(const Decl *D, AnalysisManager& mgr,
                        BugReporter &BR) const {
    UncheckedReturnValueVisitor visitor(BR, mgr.getAnalysisDeclContext(D),
                                        getCheckName());
    visitor.Visit(D->getBody());
  }
};
}

void ento::registerAPIUncheckedReturn(CheckerManager &mgr) {
  mgr.registerChecker<UncheckedReturnValueChecker>();

  llvm::Optional<std::vector<std::string>> ReturningErrorVec;
  const auto metadataPath =
    mgr.getAnalyzerOptions().getOptionAsString("api-metadata-path", "");

  metadata::loadYAMLData(metadataPath, "UncheckedReturn.yaml", "1.0",
                         mgr.getCurrentCheckName(), ReturningErrorVec);
  for (const auto FREV: *ReturningErrorVec) {
    FuncsReturningError.insert(FREV);
  }
}
