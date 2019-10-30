//===--- CrossTranslationUnit.h - -------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file provides an interface to load binary AST dumps on demand. This
//  feature can be utilized for tools that require cross translation unit
//  support.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_CROSSTU_CROSSTRANSLATIONUNIT_H
#define LLVM_CLANG_CROSSTU_CROSSTRANSLATIONUNIT_H

#include "clang/AST/ASTImporterSharedState.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Error.h"

namespace clang {
class CompilerInstance;
class ASTContext;
class ASTImporter;
class ASTUnit;
class DeclContext;
class FunctionDecl;
class NamedDecl;
class TranslationUnitDecl;

namespace tooling {
class JSONCompilationDatabase;
}

namespace cross_tu {

enum class index_error_code {
  unspecified = 1,
  missing_index_file,
  invalid_index_format,
  multiple_definitions,
  missing_definition,
  failed_import,
  failed_to_load_compilation_database,
  failed_to_get_external_ast,
  failed_to_generate_usr,
  triple_mismatch,
  lang_mismatch,
  lang_dialect_mismatch,
  load_threshold_reached,
  ambiguous_compile_commands_database
};

class IndexError : public llvm::ErrorInfo<IndexError> {
public:
  static char ID;
  IndexError(index_error_code C) : Code(C), LineNo(0) {}
  IndexError(index_error_code C, std::string FileName, int LineNo = 0)
      : Code(C), FileName(std::move(FileName)), LineNo(LineNo) {}
  IndexError(index_error_code C, std::string FileName, std::string TripleToName,
             std::string TripleFromName)
      : Code(C), FileName(std::move(FileName)),
        TripleToName(std::move(TripleToName)),
        TripleFromName(std::move(TripleFromName)) {}
  void log(raw_ostream &OS) const override;
  std::error_code convertToErrorCode() const override;
  index_error_code getCode() const { return Code; }
  int getLineNum() const { return LineNo; }
  std::string getFileName() const { return FileName; }
  std::string getTripleToName() const { return TripleToName; }
  std::string getTripleFromName() const { return TripleFromName; }

private:
  index_error_code Code;
  std::string FileName;
  int LineNo;
  std::string TripleToName;
  std::string TripleFromName;
};

/// This function parses an index file that determines which
///        translation unit contains which definition.
///
/// The index file format is the following:
/// each line consists of an USR and a filepath separated by a space.
///
/// \return Returns a map where the USR is the key and the filepath is the value
///         or an error.
llvm::Expected<llvm::StringMap<std::string>>
parseCrossTUIndex(StringRef IndexPath, StringRef CrossTUDir,
                  llvm::Optional<StringRef> OnDemandParsingDatabase);

std::string createCrossTUIndexString(const llvm::StringMap<std::string> &Index);

/// This class is used for tools that requires cross translation
///        unit capability.
///
/// This class can load definitions from external AST files.
/// The loaded definition will be merged back to the original AST using the
/// AST Importer.
/// In order to use this class, an index file is required that describes
/// the locations of the AST files for each definition.
///
/// Note that this class also implements caching.
class CrossTranslationUnitContext {
public:
  CrossTranslationUnitContext(CompilerInstance &CI);
  ~CrossTranslationUnitContext();

  /// This function loads a function definition from an external AST
  ///        file and merge it into the original AST.
  ///
  /// This method should only be used on functions that have no definitions in
  /// the current translation unit. A function definition with the same
  /// declaration will be looked up in the index file which should be in the
  /// \p CrossTUDir directory, called \p IndexName. In case the declaration is
  /// found in the index the corresponding AST file will be loaded and the
  /// definition of the function will be merged into the original AST using
  /// the AST Importer. \p CTULoadTreshold should serve as an upper limit to the
  /// number of TUs imported in order to reduce the memory footprint of CTU
  /// analysis.
  ///
  /// \return The declaration with the definition will be returned.
  /// If no suitable definition is found in the index file or multiple
  /// definitions found error will be returned.
  ///
  /// Note that the AST files should also be in the \p CrossTUDir.
  llvm::Expected<const FunctionDecl *>
  getCrossTUDefinition(const FunctionDecl *FD, StringRef CrossTUDir,
                       StringRef IndexName, bool DisplayCTUProgress,
                       unsigned CTULoadThreshold,
                       llvm::Optional<StringRef> OnDemandParsingDatabase);

  /// This function loads a function definition from an external AST
  ///        file.
  ///
  /// A function definition with the same declaration will be looked up in the
  /// index file which should be in the \p CrossTUDir directory, called
  /// \p IndexName. In case the declaration is found in the index the
  /// corresponding AST file will be loaded. If the number of TUs imported
  /// reaches \p CTULoadTreshold, no loading is performed.
  ///
  /// \return Returns a pointer to the ASTUnit that contains the definition of
  /// the looked up function or an Error.
  /// The returned pointer is never a nullptr.
  ///
  /// Note that the AST files should also be in the \p CrossTUDir.
  llvm::Expected<ASTUnit *>
  loadExternalAST(StringRef LookupName, StringRef CrossTUDir,
                  StringRef IndexName, bool DisplayCTUProgress,
                  unsigned CTULoadThreshold,
                  llvm::Optional<StringRef> OnDemandParsingDatabase);

  /// This function merges a definition from a separate AST Unit into
  ///        the current one which was created by the compiler instance that
  ///        was passed to the constructor.
  ///
  /// \return Returns the resulting definition or an error.
  llvm::Expected<const FunctionDecl *> importDefinition(const FunctionDecl *FD,
                                                        ASTUnit *Unit);

  /// Get a name to identify a function.
  static std::string getLookupName(const NamedDecl *ND);

  /// Emit diagnostics for the user for potential configuration errors.
  void emitCrossTUDiagnostics(const IndexError &IE);

  /// Determine the original source location in the original TU for an
  /// imported source location.
  /// \p ToLoc Source location in the imported-to AST.
  /// \return Source location in the imported-from AST and the corresponding
  /// ASTUnit.
  /// If any error happens (ToLoc is a non-imported source location) empty is
  /// returned.
  llvm::Optional<std::pair<SourceLocation /*FromLoc*/, ASTUnit *>>
  GetImportedFromSourceLocation(const clang::SourceLocation &ToLoc) const;

private:
  llvm::Expected<std::unique_ptr<ASTUnit>>
  loadASTFromDump(StringRef ASTSourcePath) const;
  llvm::Expected<std::unique_ptr<ASTUnit>>
  loadASTOnDemand(StringRef ASTSourcePath) const;
  llvm::Error lazyInitCompileCommands(StringRef CompileCommandsFile);
  void lazyInitImporterSharedSt(TranslationUnitDecl *ToTU);
  ASTImporter &getOrCreateASTImporter(ASTUnit *Unit);
  const FunctionDecl *findFunctionInDeclContext(const DeclContext *DC,
                                                StringRef LookupFnName);

  /// Map from the full name of the AST file to the belonging ASTUnit structure.
  llvm::StringMap<std::unique_ptr<clang::ASTUnit>> FileASTUnitMap;
  /// Map from a function lookup name (USR) to the ASTUnit for that function.
  llvm::StringMap<clang::ASTUnit *> FunctionASTUnitMap;
  /// Map from function lookup name to the AST file name.
  // This is the content of the "index file".
  llvm::StringMap<std::string> FunctionFileMap;
  llvm::DenseMap<TranslationUnitDecl *, std::unique_ptr<ASTImporter>>
      ASTUnitImporterMap;
  CompilerInstance &CI;
  ASTContext &Context;
  std::shared_ptr<ASTImporterSharedState> ImporterSharedSt;
  unsigned NumASTLoaded{0u};
  /// In case of on-demand parsing, the compilation database is parsed and
  /// stored.
  std::unique_ptr<tooling::JSONCompilationDatabase> CompileCommands;
};

} // namespace cross_tu
} // namespace clang

#endif // LLVM_CLANG_CROSSTU_CROSSTRANSLATIONUNIT_H
