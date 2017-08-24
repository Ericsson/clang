//===-- DirtyScalarChecker.cpp ------------------------------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Reports the usage of dirty integers in code.
// A dirty value is tainted and wasn't bound checked properly by the programmer.
// By default (criticalOnly == true) reports dirty usage in
//   - memcpy, malloc, calloc, strcpy, strncpy, memmove functions
//   - array indexing
//   - memory allocation with new
//   - pointer arithmetic
// otherwise (criticalOnly == false) it also reports usage as
//   - function argument
//   - loop condition
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMap.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {

class DirtyScalarChecker
    : public Checker<check::PreCall, check::PreStmt<ArraySubscriptExpr>,
                     check::PostStmt<CXXNewExpr>,
                     check::PreStmt<BinaryOperator>, check::BranchCondition> {
public:
  // Typical loop conditions worth checking are not deeper than this limit
  static const int LogicalOpCheckDepth = 3;

  DefaultBool IsCriticalOnly;
  mutable std::unique_ptr<BugType> UnboundedBugType;

  void checkPreCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPreStmt(const ArraySubscriptExpr *ASE, CheckerContext &C) const;
  void checkPostStmt(const CXXNewExpr *NE, CheckerContext &C) const;
  void checkPreStmt(const BinaryOperator *BO, CheckerContext &C) const;
  void checkBranchCondition(const Stmt *Cond, CheckerContext &C) const;

private:
  void checkLoopCond(const Stmt *Cond, CheckerContext &C,
                     int RecurseLimit = LogicalOpCheckDepth) const;
  void checkUnbounded(CheckerContext &C, ProgramStateRef State,
                      const Stmt *S) const;
  bool isUnbounded(CheckerContext &C, ProgramStateRef State, SVal V) const;
  void reportBug(CheckerContext &C, ProgramStateRef State, SVal V) const;
};

} // end of anonymous namespace

void DirtyScalarChecker::checkPreCall(const CallEvent &Call,
                                      CheckerContext &C) const {
  const Expr *E = Call.getOriginExpr();
  if (!E)
    return;
  const CallExpr *CE = dyn_cast<CallExpr>(E);
  const FunctionDecl *FDecl = C.getCalleeDecl(CE);
  if (!FDecl || FDecl->getKind() != Decl::Function)
    return;
  StringRef FName = C.getCalleeName(FDecl);
  if (FName.empty())
    return;
  if (IsCriticalOnly) {
    bool AllowedFunc = llvm::StringSwitch<bool>(FName)
                           .Case("memcpy", true)
                           .Case("malloc", true)
                           .Case("calloc", true)
                           .Case("strcpy", true)
                           .Case("strncpy", true)
                           .Case("memmove", true)
                           .Default(false);
    if (!AllowedFunc)
      return;
  }
  ProgramStateRef State = C.getState();
  for (unsigned int I = 0, E = CE->getNumArgs(); I < E; ++I) {
    const Expr *Arg = CE->getArg(I);
    checkUnbounded(C, State, Arg);
  }
}

void DirtyScalarChecker::checkPreStmt(const ArraySubscriptExpr *ASE,
                                      CheckerContext &C) const {
  checkUnbounded(C, C.getState(), ASE->getIdx());
}

void DirtyScalarChecker::checkPostStmt(const CXXNewExpr *NE,
                                       CheckerContext &C) const {
  if (!NE->isArray())
    return;
  checkUnbounded(C, C.getState(), NE->getArraySize());
}

void DirtyScalarChecker::checkPreStmt(const BinaryOperator *BO,
                                      CheckerContext &C) const {
  if (!BO->isAdditiveOp())
    return;
  Expr *LHS = BO->getLHS();
  Expr *RHS = BO->getRHS();
  if (RHS->getType()->isPointerType()) {
    std::swap(LHS, RHS);
  }
  if (!LHS->getType()->isPointerType() || !RHS->getType()->isIntegerType())
    return;
  checkUnbounded(C, C.getState(), RHS);
}

// We want to check the whole loop condition so we catch the direct descendant
// statement of the loop only.
void DirtyScalarChecker::checkBranchCondition(const Stmt *Cond,
                                              CheckerContext &C) const {
  if (IsCriticalOnly)
    return;
  ParentMap &PM = C.getLocationContext()->getParentMap();
  const Stmt *P = PM.getParentIgnoreParenCasts(Cond);
  if (!P || (!isa<ForStmt>(P) && !isa<WhileStmt>(P) && !isa<DoStmt>(P)))
    return;
  checkLoopCond(Cond, C);
}

// The heuristic implemented here tries to get conditions where the
// loop variable will be run in relation to an unbounded and tainted value.
void DirtyScalarChecker::checkLoopCond(const Stmt *Cond, CheckerContext &C,
                                       int RecurseLimit) const {
  if (RecurseLimit == 0)
    return;
  const BinaryOperator *BO = dyn_cast<BinaryOperator>(Cond);
  if (!BO)
    return;
  if (BO->isLogicalOp()) {
    Expr *LHS = BO->getLHS();
    Expr *RHS = BO->getRHS();
    checkLoopCond(LHS, C, RecurseLimit - 1);
    checkLoopCond(RHS, C, RecurseLimit - 1);
    return;
  }
  if (!BO->isComparisonOp())
    return;
  Expr *LHS = BO->getLHS();
  Expr *RHS = BO->getRHS();
  checkUnbounded(C, C.getState(), LHS);
  checkUnbounded(C, C.getState(), RHS);
}

void DirtyScalarChecker::checkUnbounded(CheckerContext &C,
                                        ProgramStateRef State,
                                        const Stmt *S) const {
  SVal Val = C.getSVal(S);
  if (Val.isUndef() || !State->isTainted(Val))
    return;
  if (isUnbounded(C, State, Val))
    reportBug(C, State, Val);
}

// We make here an indirect query on in-place constraints of V.
// If it can be assumed that a value cannot be the highest value of that type
// it surely has an upper bound. The same is true for lower bounds in case of
// signed types.
bool DirtyScalarChecker::isUnbounded(CheckerContext &C, ProgramStateRef State,
                                     SVal V) const {
  const int TooNarrowForBoundCheck = 8;

  SValBuilder &SVB = C.getSValBuilder();
  ASTContext &Ctx = SVB.getContext();
  const SymExpr *SE = V.getAsSymExpr();
  if (!SE)
    return false;
  QualType Ty = SE->getType();
  if (Ty.isNull())
    Ty = Ctx.IntTy;
  if (!Ty->isIntegerType() || Ctx.getIntWidth(Ty) <= TooNarrowForBoundCheck)
    return false;

  BasicValueFactory &BVF = SVB.getBasicValueFactory();
  nonloc::ConcreteInt Max(BVF.getMaxValue(Ty));
  SVal LTCond =
      SVB.evalBinOpNN(State, BO_LT, V.castAs<NonLoc>(), Max, Ctx.IntTy);
  if (LTCond.isUnknownOrUndef())
    return false;
  ProgramStateRef StateNotLess =
      State->assume(LTCond.castAs<DefinedSVal>(), false);
  if (StateNotLess)
    return true;

  if (Ty->isSignedIntegerType()) {
    nonloc::ConcreteInt Min(BVF.getMinValue(Ty));
    SVal GTCond =
        SVB.evalBinOpNN(State, BO_GT, V.castAs<NonLoc>(), Min, Ctx.IntTy);
    if (GTCond.isUnknownOrUndef())
      return false;
    ProgramStateRef StateNotGreater =
        State->assume(GTCond.castAs<DefinedSVal>(), false);
    if (StateNotGreater)
      return true;
  }

  return false;
}

void DirtyScalarChecker::reportBug(CheckerContext &C, ProgramStateRef State,
                                   SVal V) const {
  ExplodedNode *EN = C.generateNonFatalErrorNode(State);
  if (!UnboundedBugType)
    UnboundedBugType.reset(new BugType(this, "Unchecked tainted variable usage",
                                       "Insecure usage"));
  auto BR = llvm::make_unique<BugReport>(
      *UnboundedBugType,
      "Tainted variable is used without proper bound checking", EN);
  BR->markInteresting(C.getLocationContext());
  BR->markInteresting(V);
  C.emitReport(std::move(BR));
}

void ento::registerDirtyScalarChecker(CheckerManager &mgr) {
  DirtyScalarChecker *checker = mgr.registerChecker<DirtyScalarChecker>();
  checker->IsCriticalOnly =
      mgr.getAnalyzerOptions().getBooleanOption("criticalOnly", true, checker);
}

