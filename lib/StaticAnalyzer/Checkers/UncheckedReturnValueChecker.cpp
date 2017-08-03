//==- UncheckedReturnValueChecker.cpp ----------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/StaticAnalyzer/Checkers/LoadMetadata.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/AnalysisManager.h"
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

static llvm::Optional<std::vector<std::string>> APIFuncsReturningError;

void UncheckedReturnValueVisitor::checkUncheckedReturnValue(CallExpr *CE) {
  assert(APIFuncsReturningError.hasValue());
  if (APIFuncsReturningError->empty())
    return;

  const FunctionDecl *FD = CE->getDirectCallee();
  if (!FD)
    return;
  std::string FullName = FD->getQualifiedNameAsString();
  if (FullName.empty())
    return;

  assert(std::is_sorted(APIFuncsReturningError->begin(),
                        APIFuncsReturningError->end()));
  if (!std::binary_search(APIFuncsReturningError->begin(),
                          APIFuncsReturningError->end(), FullName))
    return;

  // Issue a warning.
  SmallString<256> buf;
  llvm::raw_svector_ostream os(buf);
  os << "Return value is not checked in call to '" << *FD << '\'';

  const char *bugType = "Unchecked return value";

  PathDiagnosticLocation CELoc =
      PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);

  BR.EmitBasicReport(AC->getDecl(), CN, bugType, "API", os.str(), CELoc);
}

namespace {
class UncheckedReturnValueChecker : public Checker<check::ASTCodeBody> {
public:
  std::unique_ptr<BugType> UncheckedReturnValueBugType;

  UncheckedReturnValueChecker() {
    UncheckedReturnValueBugType.reset(
        new BugType(getCheckName(),
                    "Return value unchecked", "Misuse of APIs"));
    UncheckedReturnValueBugType->setSuppressOnSink(true);
  }

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

  const auto metadataPath =
    mgr.getAnalyzerOptions().getOptionAsString("api-metadata-path", "");

  metadata::loadYAMLData(metadataPath, "UncheckedReturn.yaml", "1.0",
                         mgr.getCurrentCheckName(), APIFuncsReturningError);
  std::sort(APIFuncsReturningError->begin(), APIFuncsReturningError->end());
}
