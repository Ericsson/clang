//===- ClangFnMapGen.cpp -----------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===--------------------------------------------------------------------===//
//
// Clang tool which creates a list of defined functions and the files in which
// they are defined.
//
//===--------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/AST/Mangle.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include <sstream>
#include <string>
#include <vector>

using namespace llvm;
using namespace clang;
using namespace clang::tooling;

static cl::OptionCategory ClangFnMapGenCategory("clang-fnmapgen options");

class MapFunctionNamesConsumer : public ASTConsumer {
private:
  ASTContext &Ctx;
  ItaniumMangleContext *ItaniumCtx;
  std::stringstream DefinedFuncsStr;

public:
  MapFunctionNamesConsumer(ASTContext &Context, ItaniumMangleContext *MangleCtx)
      : Ctx(Context), ItaniumCtx(MangleCtx) {}
  std::string CurrentFileName;

  ~MapFunctionNamesConsumer();
  virtual void HandleTranslationUnit(ASTContext &Ctx) {
    handleDecl(Ctx.getTranslationUnitDecl());
  }

private:
  std::string getMangledName(const FunctionDecl *FD);
  void handleDecl(const Decl *D);
};

std::string MapFunctionNamesConsumer::getMangledName(const FunctionDecl *FD) {
  std::string MangledName;
  llvm::raw_string_ostream os(MangledName);
  if (const auto *CCD = dyn_cast<CXXConstructorDecl>(FD))
    // FIXME: Use correct Ctor/DtorType.
    ItaniumCtx->mangleCXXCtor(CCD, Ctor_Complete, os);
  else if (const auto *CDD = dyn_cast<CXXDestructorDecl>(FD))
    ItaniumCtx->mangleCXXDtor(CDD, Dtor_Complete, os);
  else
    ItaniumCtx->mangleName(FD, os);
  os.flush();
  return MangledName;
}

void MapFunctionNamesConsumer::handleDecl(const Decl *D) {
  if (!D)
    return;

  if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    if (FD->isThisDeclarationADefinition()) {
      if (const Stmt *Body = FD->getBody()) {
        std::string MangledName = getMangledName(FD);
        const SourceManager &SM = Ctx.getSourceManager();
        if (CurrentFileName.empty()) {
          StringRef SMgrName =
              SM.getFileEntryForID(SM.getMainFileID())->getName();
          char *Path = realpath(SMgrName.str().c_str(), nullptr);
          CurrentFileName = Path;
          free(Path);
        }

        switch (FD->getLinkageInternal()) {
        case ExternalLinkage:
        case VisibleNoLinkage:
        case UniqueExternalLinkage:
          if (SM.isInMainFile(Body->getLocStart()))
            DefinedFuncsStr << MangledName << " " << CurrentFileName << "\n";
        default:
          break;
        }
      }
    }
  }

  if (const auto *DC = dyn_cast<DeclContext>(D))
    for (const Decl *D : DC->decls())
      handleDecl(D);
}

MapFunctionNamesConsumer::~MapFunctionNamesConsumer() {
  // Flush results to standard output.
  llvm::outs() << DefinedFuncsStr.str();
}

class MapFunctionNamesAction : public ASTFrontendAction {
protected:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                 llvm::StringRef) {
    ItaniumMangleContext *ItaniumCtx =
        ItaniumMangleContext::create(CI.getASTContext(), CI.getDiagnostics());
    ItaniumCtx->setShouldForceMangleProto(true);
    std::unique_ptr<ASTConsumer> PFC(
        new MapFunctionNamesConsumer(CI.getASTContext(), ItaniumCtx));
    return PFC;
  }
};

static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

int main(int argc, const char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0], false);
  PrettyStackTraceProgram X(argc, argv);

  const char *Overview = "\nThis tool collects the mangled name and location "
                         "of all functions definitions in the source files "
                         "(excluding headers).\n";
  CommonOptionsParser OptionsParser(argc, argv, ClangFnMapGenCategory,
                                    cl::ZeroOrMore, Overview);

  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  Tool.run(newFrontendActionFactory<MapFunctionNamesAction>().get());
  return 0;
}
