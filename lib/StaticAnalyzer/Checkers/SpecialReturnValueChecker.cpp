//==- SpecialReturnValueChecker.cpp ------------------------------*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "ClangSACheckers.h"
#include "clang/StaticAnalyzer/Checkers/LoadMetadata.h"
#include "clang/StaticAnalyzer/Core/BugReporter/BugType.h"
#include "clang/StaticAnalyzer/Core/Checker.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CallEvent.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/CheckerContext.h"
#include "llvm/Support/YAMLTraits.h"

using namespace clang;
using namespace ento;

namespace {

struct FuncRetValToCheck {
  std::string Name;
  BinaryOperator::Opcode Rel;
  llvm::APSInt Val;
  bool operator<(const FuncRetValToCheck& rhs) const { return Name < rhs.Name; }
};

}

LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(FuncRetValToCheck)

namespace llvm {
namespace yaml {

template <> struct ScalarEnumerationTraits<BinaryOperator::Opcode> {
  static void enumeration(IO &io, BinaryOperator::Opcode &value) {
    io.enumCase(value, "EQ", BO_EQ);
    io.enumCase(value, "NE", BO_NE);
    io.enumCase(value, "LT", BO_LT);
    io.enumCase(value, "GT", BO_GT);
    io.enumCase(value, "LE", BO_LE);
    io.enumCase(value, "GE", BO_GE);
  }
};

template <> struct ScalarTraits<llvm::APSInt> {
  static void output(const llvm::APSInt &val, void*, llvm::raw_ostream &out) {
    val.print(out, val.isSigned());
  }
  static StringRef input(StringRef scalar, void*, llvm::APSInt &value) {
    value = llvm::APSInt(scalar);
    return StringRef();
  }
  static bool mustQuote(StringRef) { return false; }
};

template <> struct MappingTraits<FuncRetValToCheck> {
  static void mapping(IO &io, FuncRetValToCheck &s) {
    io.mapRequired("name", s.Name);
    io.mapRequired("relation", s.Rel);
    io.mapRequired("value", s.Val);
  }
};

}
}

static std::map<std::string, FuncRetValToCheck> FuncRetValsToCheck;

namespace {
class SpecialReturnValueChecker : public Checker<check::PostCall> {
public:
  void checkPostCall (const CallEvent &Call, CheckerContext &C) const;
};
}

void SpecialReturnValueChecker::checkPostCall (const CallEvent &Call,
                                               CheckerContext &C) const {
  if (FuncRetValsToCheck.empty())
    return;

  const auto *FD = dyn_cast_or_null<FunctionDecl>(Call.getDecl());
  if (!FD)
    return;

  std::string FullName = FD->getQualifiedNameAsString();
  if (FullName.empty())
    return;

  auto ToCheck = FuncRetValsToCheck.find(FullName);
  if (ToCheck == FuncRetValsToCheck.end())
    return;

  const auto RetVal = Call.getReturnValue();
  if (!RetVal.getAs<DefinedSVal>())
    return;

  auto State = C.getState();
  auto &SVB = C.getSValBuilder();
  auto &BVF = State->getBasicVals();
  auto IntType = BVF.getAPSIntType(Call.getResultType());
  auto RefInt = BVF.getValue(IntType.convert(ToCheck->second.Val));
  SVal RefVal = RetVal.getAs<NonLoc>() ?
    static_cast<SVal>(nonloc::ConcreteInt(RefInt)) :
    static_cast<SVal>(loc::ConcreteInt(RefInt));
  const auto Relation =
    SVB.evalBinOp(State, ToCheck->second.Rel, RetVal, RefVal,
                  SVB.getConditionType()).getAs<DefinedOrUnknownSVal>();
  if (!Relation)
    return;

  ProgramStateRef StateError, StateNormal;
  std::tie(StateError, StateNormal) = State->assume(*Relation);
  if (StateError) {
    C.addTransition(StateError);
  }
  if (StateNormal) {
    C.addTransition(StateNormal);
  }
}

void ento::registerAPISpecialReturn(CheckerManager &mgr) {
  mgr.registerChecker<SpecialReturnValueChecker>();

  llvm::Optional<std::vector<FuncRetValToCheck>> ToCheckVec;
  const auto metadataPath =
    mgr.getAnalyzerOptions().getOptionAsString("api-metadata-path", "");

  metadata::loadYAMLData(metadataPath, "SpecialReturn.yaml", "1.0",
                         mgr.getCurrentCheckName(), ToCheckVec);
  for (const auto FRVTC: *ToCheckVec) {
    FuncRetValsToCheck[FRVTC.Name] = FRVTC;
  }
}
