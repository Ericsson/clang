//===--- CrossTranslationUnit.cpp - -----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the CrossTranslationUnit interface.
//
//===----------------------------------------------------------------------===//
#include "clang/CrossTU/CrossTranslationUnit.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/Decl.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CrossTU/CrossTUDiagnostic.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Index/USRGeneration.h"
#include "clang/Tooling/JSONCompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace clang {
namespace cross_tu {

namespace {

#define DEBUG_TYPE "CrossTranslationUnit"
STATISTIC(NumGetCTUCalled, "The # of getCTUDefinition function called");
STATISTIC(
    NumNotInOtherTU,
    "The # of getCTUDefinition called but the function is not in any other TU");
STATISTIC(NumGetCTUSuccess,
          "The # of getCTUDefinition successfully returned the "
          "requested function's body");
STATISTIC(NumUnsupportedNodeFound, "The # of imports when the ASTImporter "
                                   "encountered an unsupported AST Node");
STATISTIC(NumNameConflicts, "The # of imports when the ASTImporter "
                            "encountered an ODR error");
STATISTIC(NumTripleMismatch, "The # of triple mismatches");
STATISTIC(NumLangMismatch, "The # of language mismatches");
STATISTIC(NumLangDialectMismatch, "The # of language dialect mismatches");
STATISTIC(NumASTLoadThresholdReached,
          "The # of ASTs not loaded because of threshold");

// Same as Triple's equality operator, but we check a field only if that is
// known in both instances.
bool hasEqualKnownFields(const llvm::Triple &Lhs, const llvm::Triple &Rhs) {
  using llvm::Triple;
  if (Lhs.getArch() != Triple::UnknownArch &&
      Rhs.getArch() != Triple::UnknownArch && Lhs.getArch() != Rhs.getArch())
    return false;
  if (Lhs.getSubArch() != Triple::NoSubArch &&
      Rhs.getSubArch() != Triple::NoSubArch &&
      Lhs.getSubArch() != Rhs.getSubArch())
    return false;
  if (Lhs.getVendor() != Triple::UnknownVendor &&
      Rhs.getVendor() != Triple::UnknownVendor &&
      Lhs.getVendor() != Rhs.getVendor())
    return false;
  if (!Lhs.isOSUnknown() && !Rhs.isOSUnknown() &&
      Lhs.getOS() != Rhs.getOS())
    return false;
  if (Lhs.getEnvironment() != Triple::UnknownEnvironment &&
      Rhs.getEnvironment() != Triple::UnknownEnvironment &&
      Lhs.getEnvironment() != Rhs.getEnvironment())
    return false;
  if (Lhs.getObjectFormat() != Triple::UnknownObjectFormat &&
      Rhs.getObjectFormat() != Triple::UnknownObjectFormat &&
      Lhs.getObjectFormat() != Rhs.getObjectFormat())
    return false;
  return true;
}

// FIXME: This class is will be removed after the transition to llvm::Error.
class IndexErrorCategory : public std::error_category {
public:
  const char *name() const noexcept override { return "clang.index"; }

  std::string message(int Condition) const override {
    switch (static_cast<index_error_code>(Condition)) {
    case index_error_code::unspecified:
      return "An unknown error has occurred.";
    case index_error_code::missing_index_file:
      return "The index file is missing.";
    case index_error_code::invalid_index_format:
      return "Invalid index file format.";
    case index_error_code::multiple_definitions:
      return "Multiple definitions in the index file.";
    case index_error_code::missing_definition:
      return "Missing definition from the index file.";
    case index_error_code::failed_import:
      return "Failed to import the definition.";
    case index_error_code::failed_to_get_external_ast:
      return "Failed to load external AST source.";
    case index_error_code::failed_to_load_compilation_database:
      return "Failed to load compilation database.";
    case index_error_code::failed_to_generate_usr:
      return "Failed to generate USR.";
    case index_error_code::triple_mismatch:
      return "Triple mismatch";
    case index_error_code::lang_mismatch:
      return "Language mismatch";
    case index_error_code::lang_dialect_mismatch:
      return "Language dialect mismatch";
    case index_error_code::load_threshold_reached:
      return "Load threshold reached";
    case index_error_code::ambiguous_compile_commands_database:
      return "Compile commands database contains multiple references to the "
             "same sorce file.";
    }
    llvm_unreachable("Unrecognized index_error_code.");
  }
};

static llvm::ManagedStatic<IndexErrorCategory> Category;
} // end anonymous namespace

char IndexError::ID;

void IndexError::log(raw_ostream &OS) const {
  OS << Category->message(static_cast<int>(Code)) << '\n';
}

std::error_code IndexError::convertToErrorCode() const {
  return std::error_code(static_cast<int>(Code), *Category);
}

llvm::Expected<llvm::StringMap<std::string>>
parseCrossTUIndex(StringRef IndexPath, StringRef CrossTUDir,
                  llvm::Optional<StringRef> OnDemandParsingDatabase) {
  std::ifstream ExternalMapFile(IndexPath);
  if (!ExternalMapFile)
    return llvm::make_error<IndexError>(index_error_code::missing_index_file,
                                        IndexPath.str());

  llvm::StringMap<std::string> Result;
  std::string Line;
  unsigned LineNo = 1;
  while (std::getline(ExternalMapFile, Line)) {
    const size_t Pos = Line.find(" ");
    if (Pos > 0 && Pos != std::string::npos) {
      StringRef LineRef{Line};
      StringRef LookupName = LineRef.substr(0, Pos);
      if (Result.count(LookupName))
        return llvm::make_error<IndexError>(
            index_error_code::multiple_definitions, IndexPath.str(), LineNo);
      StringRef FileName = LineRef.substr(Pos + 1);
      // AST-dump based analysis requires a prefixed path.
      if (!OnDemandParsingDatabase) {
        SmallString<256> FilePath = CrossTUDir;
        llvm::sys::path::append(FilePath, FileName);
        Result[LookupName] = FilePath.str().str();
      } else {
        Result[LookupName] = FileName.str();
      }
    } else
      return llvm::make_error<IndexError>(
          index_error_code::invalid_index_format, IndexPath.str(), LineNo);
    LineNo++;
  }
  return Result;
}

std::string
createCrossTUIndexString(const llvm::StringMap<std::string> &Index) {
  std::ostringstream Result;
  for (const auto &E : Index)
    Result << E.getKey().str() << " " << E.getValue() << '\n';
  return Result.str();
}

CrossTranslationUnitContext::CrossTranslationUnitContext(CompilerInstance &CI)
    : CI(CI), Context(CI.getASTContext()) {}

CrossTranslationUnitContext::~CrossTranslationUnitContext() {}

std::string CrossTranslationUnitContext::getLookupName(const NamedDecl *ND) {
  SmallString<128> DeclUSR;
  bool Ret = index::generateUSRForDecl(ND, DeclUSR); (void)Ret;
  assert(!Ret && "Unable to generate USR");
  return DeclUSR.str();
}

/// Recursively visits the function decls of a DeclContext, and looks up a
/// function based on USRs.
const FunctionDecl *
CrossTranslationUnitContext::findFunctionInDeclContext(const DeclContext *DC,
                                                       StringRef LookupFnName) {
  assert(DC && "Declaration Context must not be null");
  for (const Decl *D : DC->decls()) {
    const auto *SubDC = dyn_cast<DeclContext>(D);
    if (SubDC)
      if (const auto *FD = findFunctionInDeclContext(SubDC, LookupFnName))
        return FD;

    const auto *ND = dyn_cast<FunctionDecl>(D);
    const FunctionDecl *ResultDecl;
    if (!ND || !ND->hasBody(ResultDecl))
      continue;
    if (getLookupName(ResultDecl) != LookupFnName)
      continue;
    return ResultDecl;
  }
  return nullptr;
}

llvm::Expected<const FunctionDecl *>
CrossTranslationUnitContext::getCrossTUDefinition(
    const FunctionDecl *FD, StringRef CrossTUDir, StringRef IndexName,
    bool DisplayCTUProgress, unsigned CTULoadThreshold,
    llvm::Optional<StringRef> OnDemandParsingDatabase) {
  assert(FD && "FD is missing, bad call to this function!");
  assert(!FD->hasBody() && "FD has a definition in current translation unit!");
  ++NumGetCTUCalled;
  const std::string LookupFnName = getLookupName(FD);
  if (LookupFnName.empty())
    return llvm::make_error<IndexError>(
        index_error_code::failed_to_generate_usr);
  llvm::Expected<ASTUnit *> ASTUnitOrError =
      loadExternalAST(LookupFnName, CrossTUDir, IndexName, DisplayCTUProgress,
                      CTULoadThreshold, OnDemandParsingDatabase);
  if (!ASTUnitOrError)
    return ASTUnitOrError.takeError();
  ASTUnit *Unit = *ASTUnitOrError;
  assert(&Unit->getFileManager() ==
         &Unit->getASTContext().getSourceManager().getFileManager());

  const llvm::Triple &TripleTo = Context.getTargetInfo().getTriple();
  const llvm::Triple &TripleFrom =
      Unit->getASTContext().getTargetInfo().getTriple();
  // The imported AST had been generated for a different target.
  // Some parts of the triple in the loaded ASTContext can be unknown while the
  // very same parts in the target ASTContext are known. Thus we check for the
  // known parts only.
  if (!hasEqualKnownFields(TripleTo, TripleFrom)) {
    // TODO: Pass the SourceLocation of the CallExpression for more precise
    // diagnostics.
    ++NumTripleMismatch;
    return llvm::make_error<IndexError>(index_error_code::triple_mismatch,
                                        Unit->getMainFileName(), TripleTo.str(),
                                        TripleFrom.str());
  }

  const auto &LangTo = Context.getLangOpts();
  const auto &LangFrom = Unit->getASTContext().getLangOpts();

  // FIXME: Currenty we do not support CTU across C++ and C and across
  // different dialects of C++.
  if (LangTo.CPlusPlus != LangFrom.CPlusPlus) {
    ++NumLangMismatch;
    return llvm::make_error<IndexError>(index_error_code::lang_mismatch);
  }

  // If CPP dialects are different then return with error.
  //
  // Consider this STL code:
  //   template<typename _Alloc>
  //     struct __alloc_traits
  //   #if __cplusplus >= 201103L
  //     : std::allocator_traits<_Alloc>
  //   #endif
  //     { // ...
  //     };
  // This class template would create ODR errors during merging the two units,
  // since in one translation unit the class template has a base class, however
  // in the other unit it has none.
  if (LangTo.CPlusPlus11 != LangFrom.CPlusPlus11 ||
      LangTo.CPlusPlus14 != LangFrom.CPlusPlus14 ||
      LangTo.CPlusPlus17 != LangFrom.CPlusPlus17 ||
      LangTo.CPlusPlus2a != LangFrom.CPlusPlus2a) {
    ++NumLangDialectMismatch;
    return llvm::make_error<IndexError>(
        index_error_code::lang_dialect_mismatch);
  }

  TranslationUnitDecl *TU = Unit->getASTContext().getTranslationUnitDecl();
  if (const FunctionDecl *ResultDecl =
          findFunctionInDeclContext(TU, LookupFnName))
    return importDefinition(ResultDecl, Unit);
  return llvm::make_error<IndexError>(index_error_code::failed_import);
}

void CrossTranslationUnitContext::emitCrossTUDiagnostics(const IndexError &IE) {
  switch (IE.getCode()) {
  case index_error_code::missing_index_file:
    Context.getDiagnostics().Report(diag::err_ctu_error_opening)
        << IE.getFileName();
    break;
  case index_error_code::invalid_index_format:
    Context.getDiagnostics().Report(diag::err_extdefmap_parsing)
        << IE.getFileName() << IE.getLineNum();
    break;
  case index_error_code::multiple_definitions:
    Context.getDiagnostics().Report(diag::err_multiple_def_index)
        << IE.getLineNum();
    break;
  case index_error_code::triple_mismatch:
    Context.getDiagnostics().Report(diag::warn_ctu_incompat_triple)
        << IE.getFileName() << IE.getTripleToName() << IE.getTripleFromName();
    break;
  default:
    break;
  }
}

/// Load the AST from a source-file, which is supposed to be located inside the
/// compilation database \p OnDemandParsingCommands. The compilation database
/// can contain the path of the file under the key "file" as an absolute path,
/// or as a relative path. When emitting diagnostics, plist files may contain
/// references to a location in a TU, that is different from the main TU. In
/// such cases, the file path emitted by the DiagnosticEngine is based on how
/// the exact invocation is assembled inside the ClangTool, which performs the
/// building of the ASTs. In order to ensure absolute paths inside the
/// diagnostics, we use the ArgumentsAdjuster API of ClangTool to make sure that
/// the invocation inside ClangTool is always made with an absolute path. \p
/// ASTSourcePath is assumed to be the lookup-name of the file, which comes from
/// the Index. The Index is built by the \p clang-extdef-mapping tool, which is
/// supposed to generate absolute paths.
///
/// We must have absolute paths inside the plist, because otherwise we would
/// not be able to parse the bug, because we could not find the files with
/// relative paths. The directory of one entry in the compilation db may be
/// different from the directory where the plist is interpreted.
///
/// Note that as the ClangTool is instantiated with a lookup-vector, which
/// contains a single entry; the supposedly absolute path of the source file.
/// So, the ArgumentAdjuster will only be used on the single corresponding
/// invocation. This garantees that even if two files match in name, but
/// differ in location, only the correct one's invocation will be handled. This
/// is due to the fact that the lookup is done correctly inside the
/// OnDemandParsingDatabase, so it works for already absolute paths given under
/// the "file" entry of the compilation database, but also if a relative path is
/// given. In such a case, the lookup uses the "directory" entry as well to
/// identify the correct file.
llvm::Expected<std::unique_ptr<ASTUnit>>
CrossTranslationUnitContext::loadASTOnDemand(StringRef ASTSourcePath) const {

  using namespace tooling;

  SmallVector<std::string, 1> Files;
  Files.push_back(ASTSourcePath);
  ClangTool Tool(*CompileCommands, Files, CI.getPCHContainerOperations());

  /// Lambda filter designed to find the source file argument inside an
  /// invocation used to build the ASTs, and replace it with its absolute path
  /// equivalent.
  auto SourcePathNormalizer = [ASTSourcePath](const CommandLineArguments &Args,
                                              StringRef FileName) {
    /// Match the argument to the absolute path by checking whether it is a
    /// postfix.
    auto IsPostfixOfLookup = [ASTSourcePath](const std::string &Arg) {
      return ASTSourcePath.rfind(Arg) != llvm::StringRef::npos;
    };

    /// Commandline arguments are modified, and the API dictates the return of
    /// a new instance, so copy the original.
    CommandLineArguments Result{Args};

    /// Search for the source file argument. Start from the end as a heuristic,
    /// as most invocations tend to contain the source file argument in their
    /// latter half. Only the first match is replaced.
    auto SourceFilePath =
        std::find_if(Result.rbegin(), Result.rend(), IsPostfixOfLookup);

    /// If source file argument could not been found, return the original
    /// CommandlineArgumentsInstance.
    if (SourceFilePath == Result.rend())
      return Result;

    /// Overwrite the argument with the \p ASTSourcePath, as it is assumed to
    /// be the absolute path of the file.
    *SourceFilePath = ASTSourcePath.str();

    return Result;
  };

  Tool.appendArgumentsAdjuster(std::move(SourcePathNormalizer));

  std::vector<std::unique_ptr<ASTUnit>> ASTs;
  Tool.buildASTs(ASTs);

  /// There is an assumption that the compilation database does not contain
  /// multiple entries for the same source file.
  if (ASTs.size() > 1)
    return llvm::make_error<IndexError>(
        index_error_code::ambiguous_compile_commands_database);

  /// Ideally there is exactly one entry in the compilation database that
  /// matchse the source file.
  if (ASTs.size() != 1)
    return llvm::make_error<IndexError>(
        index_error_code::failed_to_get_external_ast);

  ASTs[0]->enableSourceFileDiagnostics();
  return std::move(ASTs[0]);
}

llvm::Expected<std::unique_ptr<ASTUnit>>
CrossTranslationUnitContext::loadASTFromDump(StringRef ASTFileName) const {
  // If no \p OnDemandParsingDatabase is given, try to load from AST dump
  // file, as on-demand parsing is disabled.
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  TextDiagnosticPrinter *DiagClient =
      new TextDiagnosticPrinter(llvm::errs(), &*DiagOpts);
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID(new DiagnosticIDs());
  IntrusiveRefCntPtr<DiagnosticsEngine> Diags(
      new DiagnosticsEngine(DiagID, &*DiagOpts, DiagClient));

  std::unique_ptr<ASTUnit> LoadedUnit(ASTUnit::LoadFromASTFile(
      ASTFileName, CI.getPCHContainerOperations()->getRawReader(),
      ASTUnit::LoadEverything, Diags, CI.getFileSystemOpts()));

  if (!LoadedUnit)
    return llvm::make_error<IndexError>(
        index_error_code::failed_to_get_external_ast);

  return std::move(LoadedUnit);
}

llvm::Error CrossTranslationUnitContext::lazyInitCompileCommands(
    StringRef CompileCommandsFile) {
  // Lazily initialize the compilation database.
  std::string LoadError;
  CompileCommands = tooling::JSONCompilationDatabase::loadFromFile(
      CompileCommandsFile, LoadError,
      tooling::JSONCommandLineSyntax::AutoDetect);
  return CompileCommands ? llvm::Error::success()
                         : llvm::make_error<IndexError>(
                               index_error_code::failed_to_get_external_ast);
}

llvm::Expected<ASTUnit *> CrossTranslationUnitContext::loadExternalAST(
    StringRef LookupName, StringRef CrossTUDir, StringRef IndexName,
    bool DisplayCTUProgress, unsigned CTULoadThreshold,
    llvm::Optional<StringRef> OnDemandParsingDatabase) {
  // FIXME: The current implementation only supports loading functions with
  //        a lookup name from a single translation unit. If multiple
  //        translation units contains functions with the same lookup name an
  //        error will be returned.

  if (NumASTLoaded >= CTULoadThreshold) {
    ++NumASTLoadThresholdReached;
    return llvm::make_error<IndexError>(
        index_error_code::load_threshold_reached);
  }

  ASTUnit *Unit = nullptr;
  auto FnUnitCacheEntry = FunctionASTUnitMap.find(LookupName);
  if (FnUnitCacheEntry == FunctionASTUnitMap.end()) {
    if (FunctionFileMap.empty()) {

      SmallString<256> IndexFile = CrossTUDir;
      if (llvm::sys::path::is_absolute(IndexName))
        IndexFile = IndexName;
      else
        llvm::sys::path::append(IndexFile, IndexName);

      llvm::Expected<llvm::StringMap<std::string>> IndexOrErr =
          parseCrossTUIndex(IndexFile, CrossTUDir, OnDemandParsingDatabase);

      if (!IndexOrErr)
        return IndexOrErr.takeError();

      FunctionFileMap = *IndexOrErr;
    }

    auto It = FunctionFileMap.find(LookupName);
    if (It == FunctionFileMap.end()) {
      ++NumNotInOtherTU;
      return llvm::make_error<IndexError>(index_error_code::missing_definition);
    }
    StringRef ASTSource = It->second;
    auto ASTCacheEntry = FileASTUnitMap.find(ASTSource);
    if (ASTCacheEntry == FileASTUnitMap.end()) {
      // Cache miss.

      if (OnDemandParsingDatabase) {
        llvm::Error InitError =
            lazyInitCompileCommands(*OnDemandParsingDatabase);
        if (InitError)
          return std::move(InitError);
      }

      llvm::Expected<std::unique_ptr<ASTUnit>> LoadedUnit =
          OnDemandParsingDatabase ? loadASTOnDemand(ASTSource)
                                  : loadASTFromDump(ASTSource);

      if (!LoadedUnit)
        return LoadedUnit.takeError();

      Unit = LoadedUnit->get();

      // Cache the resulting ASTUnit.
      FileASTUnitMap[ASTSource] = std::move(*LoadedUnit);

      ++NumASTLoaded;
      if (DisplayCTUProgress) {
        llvm::errs() << "CTU loaded AST file: " << ASTSource << "\n";
      }
    } else {
      Unit = ASTCacheEntry->second.get();
    }
    // Fill the cache for the lookup name as well.
    assert(Unit);
    FunctionASTUnitMap[LookupName] = Unit;
  } else {
    Unit = FnUnitCacheEntry->second;
  }

  // Only non-null pointers are cached, because the load operations should only
  // finish without an error, if the pointer they load is not null.
  assert(Unit);
  return Unit;
}

llvm::Expected<const FunctionDecl *>
CrossTranslationUnitContext::importDefinition(const FunctionDecl *FD,
                                              ASTUnit *Unit) {
  assert(FD->hasBody() && "Functions to be imported should have body.");

  assert(&FD->getASTContext() == &Unit->getASTContext() &&
         "Wrong ASTContext of function.");
  ASTImporter &Importer = getOrCreateASTImporter(Unit);
  auto ToDeclOrError = Importer.Import(FD);
  if (!ToDeclOrError) {
    handleAllErrors(ToDeclOrError.takeError(),
                    [&](const ImportError &IE) {
                      switch (IE.Error) {
                      case ImportError::NameConflict:
                        ++NumNameConflicts;
                         break;
                      case ImportError::UnsupportedConstruct:
                        ++NumUnsupportedNodeFound;
                        break;
                      case ImportError::Unknown:
                        llvm_unreachable("Unknown import error happened.");
                        break;
                      }
                    });
    return llvm::make_error<IndexError>(index_error_code::failed_import);
  }
  auto *ToDecl = cast<FunctionDecl>(*ToDeclOrError);
  assert(ToDecl->hasBody() && "Imported function should have body.");
  ++NumGetCTUSuccess;

  return ToDecl;
}

void CrossTranslationUnitContext::lazyInitImporterSharedSt(
    TranslationUnitDecl *ToTU) {
  if (!ImporterSharedSt)
    ImporterSharedSt = std::make_shared<ASTImporterSharedState>(*ToTU);
}

ASTImporter &
CrossTranslationUnitContext::getOrCreateASTImporter(ASTUnit *Unit) {
  ASTContext &From = Unit->getASTContext();

  auto I = ASTUnitImporterMap.find(From.getTranslationUnitDecl());
  if (I != ASTUnitImporterMap.end())
    return *I->second;

  lazyInitImporterSharedSt(Context.getTranslationUnitDecl());
  ASTImporter *NewImporter = new ASTImporter(
      Context, Context.getSourceManager().getFileManager(), From,
      From.getSourceManager().getFileManager(), false, ImporterSharedSt, Unit);
  ASTUnitImporterMap[From.getTranslationUnitDecl()].reset(NewImporter);
  return *NewImporter;
}

llvm::Optional<std::pair<SourceLocation, ASTUnit *>>
CrossTranslationUnitContext::GetImportedFromSourceLocation(
    const clang::SourceLocation &ToLoc) const {
  if (!ImporterSharedSt)
    return {};

  const SourceManager &SM = Context.getSourceManager();
  auto DecToLoc = SM.getDecomposedLoc(ToLoc);

  auto I = ImporterSharedSt->getImportedFileIDs().find(DecToLoc.first);
  if (I == ImporterSharedSt->getImportedFileIDs().end())
    return {};

  FileID FromID = I->second.first;
  clang::ASTUnit *Unit = I->second.second;
  SourceLocation FromLoc =
      Unit->getSourceManager().getComposedLoc(FromID, DecToLoc.second);

  return std::make_pair(FromLoc, Unit);
}

} // namespace cross_tu
} // namespace clang
