//===-- SpecialReturnValueStatisticsCollector.cpp -----------------*- C++ -*--//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"

using namespace clang;
using namespace ento;

namespace {

struct SpecialReturnValue {
private:
  const CallExpr *Call;
  bool checkedForNegative, checkedForNull;

  SpecialReturnValue(const CallExpr *CE, bool cNe, bool cNu)
      : Call(CE), checkedForNegative(cNe), checkedForNull(cNu) {}

public:
  const CallExpr *getCall() const { return Call; }
  bool isCheckedForNegative() const { return checkedForNegative; }
  bool isCheckedForNull() const { return checkedForNull; }

  static SpecialReturnValue getUsage(const CallExpr *CE) {
    return SpecialReturnValue(CE, false, false);
  }

  SpecialReturnValue checkForNegative() const {
    return SpecialReturnValue(Call, true, checkedForNull);
  }

  SpecialReturnValue checkForNull() const {
    return SpecialReturnValue(Call, checkedForNegative, true);
  }

  bool operator==(const SpecialReturnValue &X) const {
    return Call == X.Call && checkedForNegative == X.checkedForNegative &&
           checkedForNull == X.checkedForNull;
  }

  bool operator!=(const SpecialReturnValue &X) const {
    return Call != X.Call || checkedForNegative != X.checkedForNegative ||
           checkedForNull != X.checkedForNull;
  }

  void Profile(llvm::FoldingSetNodeID &ID) const {
    ID.AddPointer(Call);
    ID.AddBoolean(checkedForNegative);
    ID.AddBoolean(checkedForNull);
  }
};

std::map<const CallExpr *, std::pair<bool, bool>> CheckedCalls;

class SpecialReturnValueStatisticsCollector
    : public Checker<check::PostCall, check::PostStmt<BinaryOperator>,
                     check::DeadSymbols, check::EndOfTranslationUnit> {

  std::unique_ptr<BugType> StatisticsBugType;
  AnalysisDeclContext *AC;

  void handleComparison(BinaryOperator::Opcode, SymbolRef Sym, const SVal &Val,
                        CheckerContext &C) const;

public:
  SpecialReturnValueStatisticsCollector();

  void checkPostCall(const CallEvent &Call, CheckerContext &C) const;
  void checkPostStmt(const BinaryOperator *BO, CheckerContext &C) const;
  void checkDeadSymbols(SymbolReaper &SR, CheckerContext &C) const;
  void checkEndOfTranslationUnit(const TranslationUnitDecl *TU,
                                 AnalysisManager &Mgr, BugReporter &BR) const;
};
} // namespace

REGISTER_MAP_WITH_PROGRAMSTATE(SpecialReturnValueMap, SymbolRef,
                               SpecialReturnValue)

SpecialReturnValueStatisticsCollector::SpecialReturnValueStatisticsCollector() {
  StatisticsBugType.reset(
      new BugType(this, "Special return value", "Statistics"));
  StatisticsBugType->setSuppressOnSink(true);
}

void SpecialReturnValueStatisticsCollector::checkPostCall(
    const CallEvent &Call, CheckerContext &C) const {
  const auto *Func = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!Func)
    return;

  if (Func->getReturnType()->isVoidType())
    return;

  const auto RetSym = Call.getReturnValue().getAsSymbol();
  if (!RetSym)
    return;

  const auto *Orig = dyn_cast_or_null<CallExpr>(Call.getOriginExpr());
  if (!Orig)
    return;

  auto State = C.getState();
  State = State->set<SpecialReturnValueMap>(RetSym,
                                            SpecialReturnValue::getUsage(Orig));
  C.addTransition(State);
}

void SpecialReturnValueStatisticsCollector::checkPostStmt(
    const BinaryOperator *BO, CheckerContext &C) const {
  if (!BO->isRelationalOp() && !BO->isEqualityOp())
    return;

  auto State = C.getState();
  const auto *LCtx = C.getLocationContext();

  const auto LVal = State->getSVal(BO->getLHS(), LCtx),
             RVal = State->getSVal(BO->getRHS(), LCtx);

  if (const auto LSym = LVal.getAsSymbol()) {
    handleComparison(BO->getOpcode(), LSym, RVal, C);
  }
  if (const auto RSym = RVal.getAsSymbol()) {
    handleComparison(BinaryOperator::reverseComparisonOp(BO->getOpcode()), RSym,
                     LVal, C);
  }
}

void SpecialReturnValueStatisticsCollector::checkDeadSymbols(
    SymbolReaper &SR, CheckerContext &C) const {
  auto State = C.getState();

  auto SymbolMap = State->get<SpecialReturnValueMap>();
  for (const auto Sym : SymbolMap) {
    if (!SR.isLive(Sym.first)) {
      CheckedCalls[Sym.second.getCall()];
      if (Sym.second.isCheckedForNegative()) {
        CheckedCalls[Sym.second.getCall()].first = true;
      }
      if (Sym.second.isCheckedForNull()) {
        CheckedCalls[Sym.second.getCall()].second = true;
      }
      State = State->remove<SpecialReturnValueMap>(Sym.first);
    }
  }

  C.addTransition(State);
}

void SpecialReturnValueStatisticsCollector::checkEndOfTranslationUnit(
    const TranslationUnitDecl *TU, AnalysisManager &Mgr,
    BugReporter &BR) const {
  const auto &SM = Mgr.getASTContext().getSourceManager();
  for (const auto C : CheckedCalls) {
    const auto *CE = C.first;
    const auto *FD = dyn_cast<FunctionDecl>(CE->getCalleeDecl());
    const auto &AC = Mgr.getAnalysisDeclContext(FD);
    const auto Checks = C.second;

    SmallString<256> Buf;
    llvm::raw_svector_ostream Out(Buf);
    Out << "Special Return Value: " << CE->getLocStart().printToString(SM)
        << "," << FD->getNameAsString() << "," << (int)Checks.first << ","
        << Checks.second;

    PathDiagnosticLocation CELoc =
        PathDiagnosticLocation::createBegin(CE, BR.getSourceManager(), AC);
    BR.EmitBasicReport(FD, getCheckName(), "Statistics", "API", Out.str(),
                       CELoc);
  }
}

void SpecialReturnValueStatisticsCollector::handleComparison(
    BinaryOperator::Opcode Op, SymbolRef Sym, const SVal &Val,
    CheckerContext &C) const {
  auto State = C.getState();
  auto &CM = State->getConstraintManager();

  const auto *Usage = State->get<SpecialReturnValueMap>(Sym);
  if (!Usage)
    return;

  if (Usage->isCheckedForNegative() || Usage->isCheckedForNull())
    return;

  const auto T = Sym->getType();
  if (T->isIntegerType() || T->isPointerType()) {
    const llvm::APSInt *IntVal = nullptr;
    switch (Val.getSubKind()) {
    default:
      break;
    case nonloc::ConcreteIntKind:
      IntVal = &Val.castAs<nonloc::ConcreteInt>().getValue();
      break;
    case loc::ConcreteIntKind:
      IntVal = &Val.castAs<loc::ConcreteInt>().getValue();
      break;
    case nonloc::SymbolValKind:
      IntVal = CM.getSymVal(State, Val.castAs<nonloc::SymbolVal>().getSymbol());
    }

    if (IntVal) {
      if (T->isIntegerType() &&
          (((Op == BO_GE || Op == BO_LT) && *IntVal == 0) ||
           ((Op == BO_GT || Op == BO_LE) && *IntVal == -1))) {
        C.addTransition(
            State->set<SpecialReturnValueMap>(Sym, Usage->checkForNegative()));
        return;
      }

      if (T->isPointerType() && (Op == BO_EQ || Op == BO_NE) && *IntVal == 0) {
        C.addTransition(
            State->set<SpecialReturnValueMap>(Sym, Usage->checkForNull()));
        return;
      }
    }
  }

  if (const auto RSym = Val.getAsSymbol()) {
    if (const auto *RUsage = State->get<SpecialReturnValueMap>(RSym)) {
      if (RUsage->isCheckedForNegative()) {
        C.addTransition(
            State->set<SpecialReturnValueMap>(Sym, Usage->checkForNegative()));
        return;
      }
      if (RUsage->isCheckedForNull()) {
        C.addTransition(
            State->set<SpecialReturnValueMap>(Sym, Usage->checkForNull()));
        return;
      }
    }
  }
}

void ento::registerSpecialReturnValueStatisticsCollector(CheckerManager &Mgr) {
  Mgr.registerChecker<SpecialReturnValueStatisticsCollector>();
}
