//===- unittest/AST/ASTImporterTest.cpp - AST node import test ------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Tests for the correct import of AST nodes from one AST context to another.
//
//===----------------------------------------------------------------------===//

#include "MatchVerifier.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"

#include "DeclMatcher.h"
#include "gmock/gmock.h"

namespace clang {
namespace ast_matchers {

typedef std::vector<std::string> ArgVector;
typedef std::vector<ArgVector> RunOptions;

static bool isCXX(Language Lang) {
  return Lang == Lang_CXX || Lang == Lang_CXX11;
}

static ArgVector getBasicRunOptionsForLanguage(Language Lang) {
  ArgVector BasicArgs;
  // Test with basic arguments.
  switch (Lang) {
  case Lang_C:
    BasicArgs = {"-x", "c", "-std=c99"};
    break;
  case Lang_C89:
    BasicArgs = {"-x", "c", "-std=c89"};
    break;
  case Lang_CXX:
    BasicArgs = {"-std=c++98"};
    break;
  case Lang_CXX11:
    BasicArgs = {"-std=c++11"};
    break;
  case Lang_OpenCL:
  case Lang_OBJCXX:
    llvm_unreachable("Not implemented yet!");
  }
  return BasicArgs;
}

static RunOptions getRunOptionsForLanguage(Language Lang) {
  ArgVector BasicArgs = getBasicRunOptionsForLanguage(Lang);

  // For C++, test with "-fdelayed-template-parsing" enabled to handle MSVC
  // default behaviour.
  if (isCXX(Lang)) {
    ArgVector ArgsForDelayedTemplateParse = BasicArgs;
    ArgsForDelayedTemplateParse.emplace_back("-fdelayed-template-parsing");
    return {BasicArgs, ArgsForDelayedTemplateParse};
  }

  return {BasicArgs};
}

// Creates a virtual file and assigns that to the context of given AST. If the
// file already exists then the file will not be created again as a duplicate.
static void createVirtualFileIfNeeded(ASTUnit *ToAST, StringRef FileName,
                                      const std::string &Code) {
  assert(ToAST);
  ASTContext &ToCtx = ToAST->getASTContext();
  auto *OFS = static_cast<vfs::OverlayFileSystem *>(
      ToCtx.getSourceManager().getFileManager().getVirtualFileSystem().get());
  auto *MFS =
      static_cast<vfs::InMemoryFileSystem *>(OFS->overlays_begin()->get());
  MFS->addFile(FileName, 0, llvm::MemoryBuffer::getMemBuffer(Code.c_str()));
}

template<typename NodeType, typename MatcherType>
testing::AssertionResult
testImport(const std::string &FromCode, const ArgVector &FromArgs,
           const std::string &ToCode, const ArgVector &ToArgs,
           MatchVerifier<NodeType> &Verifier, const MatcherType &AMatcher) {
  const char *const InputFileName = "input.cc";
  const char *const OutputFileName = "output.cc";

  std::unique_ptr<ASTUnit>
      FromAST = tooling::buildASTFromCodeWithArgs(
        FromCode, FromArgs, InputFileName),
      ToAST = tooling::buildASTFromCodeWithArgs(ToCode, ToArgs, OutputFileName);

  ASTContext &FromCtx = FromAST->getASTContext(),
      &ToCtx = ToAST->getASTContext();

  FromAST->beginSourceFile();
  ToAST->beginSourceFile();
  
  // Add input.cc to virtual file system so importer can 'find' it
  // while importing SourceLocations.
  createVirtualFileIfNeeded(ToAST.get(), InputFileName, FromCode);

  ASTImporter Importer(ToCtx, ToAST->getFileManager(),
                       FromCtx, FromAST->getFileManager(), false);

  IdentifierInfo *ImportedII = &FromCtx.Idents.get("declToImport");
  assert(ImportedII && "Declaration with 'declToImport' name"
                       "should be specified in test!");
  DeclarationName ImportDeclName(ImportedII);
  SmallVector<NamedDecl *, 4> FoundDecls;
  FromCtx.getTranslationUnitDecl()->localUncachedLookup(
        ImportDeclName, FoundDecls);

  if (FoundDecls.size() == 0)
    return testing::AssertionFailure() << "No declarations were found!";

  if (FoundDecls.size() > 1)
    return testing::AssertionFailure() << "Multiple declarations were found!";

  // Sanity check: the node being imported should match in the same way as
  // the result node.
  EXPECT_TRUE(Verifier.match(FoundDecls.front(), AMatcher));

  auto Imported = Importer.Import(FoundDecls.front());
  if (!Imported)
    return testing::AssertionFailure() << "Import failed, nullptr returned!";

  // This should dump source locations and assert if some source locations
  // were not imported.
  SmallString<1024> ImportChecker;
  llvm::raw_svector_ostream ToNothing(ImportChecker);
  ToCtx.getTranslationUnitDecl()->print(ToNothing);

  // This traverses the AST to catch certain bugs like poorly or not
  // implemented subtrees.
  Imported->dump(ToNothing);

  return Verifier.match(Imported, AMatcher);
}

template<typename NodeType, typename MatcherType>
void testImport(const std::string &FromCode, Language FromLang,
                const std::string &ToCode, Language ToLang,
                MatchVerifier<NodeType> &Verifier,
                const MatcherType &AMatcher) {
  auto RunOptsFrom = getRunOptionsForLanguage(FromLang);
  auto RunOptsTo = getRunOptionsForLanguage(ToLang);
  for (const auto &FromArgs : RunOptsFrom)
    for (const auto &ToArgs : RunOptsTo)
      EXPECT_TRUE(testImport(FromCode, FromArgs, ToCode, ToArgs,
                             Verifier, AMatcher));
}

template<typename NodeType, typename MatcherType>
void testImport(const std::string &FromCode,
                Language FromLang, const ArgVector &FromArgsExtra,
                const std::string &ToCode,
                Language ToLang, const ArgVector &ToArgsExtra,
                MatchVerifier<NodeType> &Verifier,
                const MatcherType &AMatcher) {
  auto RunOptsFrom = getRunOptionsForLanguage(FromLang);
  auto RunOptsTo = getRunOptionsForLanguage(ToLang);
  for (auto FromArgs : RunOptsFrom) {
    for (auto ToArgs : RunOptsTo) {
      FromArgs.insert(FromArgs.end(), FromArgsExtra.begin(), FromArgsExtra.end());
      ToArgs.insert(ToArgs.end(), ToArgsExtra.begin(), ToArgsExtra.end());
      EXPECT_TRUE(testImport(FromCode, FromArgs, ToCode, ToArgs,
                             Verifier, AMatcher));
    }
  }
}

const StringRef DeclToImportID = "declToImport";

// This class provides generic methods to write tests which can check internal
// attributes of AST nodes like getPreviousDecl(), isVirtual(), etc.  Also,
// this fixture makes it possible to import from several "From" contexts.
class ASTImporterTestBase : public ::testing::TestWithParam<ArgVector> {

  const char *const InputFileName = "input.cc";
  const char *const OutputFileName = "output.cc";

  // Buffer for the To context, must live in the test scope.
  std::string ToCode;

  struct TU {
    // Buffer for the context, must live in the test scope.
    std::string Code;
    std::string FileName;
    std::unique_ptr<ASTUnit> Unit;
    TranslationUnitDecl *TUDecl = nullptr;
    std::unique_ptr<ASTImporter> Importer;
    TU(StringRef Code, StringRef FileName, ArgVector Args)
        : Code(Code), FileName(FileName),
          Unit(tooling::buildASTFromCodeWithArgs(this->Code, Args,
                                                 this->FileName)),
          TUDecl(Unit->getASTContext().getTranslationUnitDecl()) {
      Unit->beginSourceFile();
    }

    void lazyInitImporter(ASTUnit *ToAST) {
      assert(ToAST);
      if (!Importer) {
        Importer.reset(new ASTImporter(
            ToAST->getASTContext(), ToAST->getFileManager(),
            Unit->getASTContext(), Unit->getFileManager(), false));
      }
    }

    Decl *import(ASTUnit *ToAST, Decl *FromDecl) {
      lazyInitImporter(ToAST);
      return Importer->Import(FromDecl);
    }

    QualType import(ASTUnit *ToAST, QualType FromType) {
      lazyInitImporter(ToAST);
      return Importer->Import(FromType);
    }
  };

  // We may have several From contexts and related translation units. In each
  // AST, the buffers for the source are handled via references and are set
  // during the creation of the AST. These references must point to a valid
  // buffer until the AST is alive. Thus, we must use a list in order to avoid
  // moving of the stored objects because that would mean breaking the
  // references in the AST. By using a vector a move could happen when the
  // vector is expanding, with the list we won't have these issues.
  std::list<TU> FromTUs;

  void lazyInitToAST(Language ToLang) {
    if (ToAST)
      return;
    ArgVector ToArgs = getArgVectorForLanguage(ToLang);
    // Build the AST from an empty file.
    ToAST = tooling::buildASTFromCodeWithArgs(/*Code=*/"", ToArgs, "empty.cc");
    ToAST->beginSourceFile();
  }

  TU *findFromTU(Decl *From) {
    // Create a virtual file in the To Ctx which corresponds to the file from
    // which we want to import the `From` Decl. Without this source locations
    // will be invalid in the ToCtx.
    auto It = std::find_if(FromTUs.begin(), FromTUs.end(), [From](const TU &E) {
      return E.TUDecl == From->getTranslationUnitDecl();
    });
    assert(It != FromTUs.end());
    assert(ToAST);
    createVirtualFileIfNeeded(ToAST.get(), It->FileName, It->Code);
    return &*It;
  }

public:
  // We may have several From context but only one To context.
  std::unique_ptr<ASTUnit> ToAST;

  // Returns the argument vector used for a specific language, this set
  // can be tweaked by the test parameters.
  ArgVector getArgVectorForLanguage(Language Lang) {
    ArgVector Args = getBasicRunOptionsForLanguage(Lang);
    ArgVector ExtraArgs = GetParam();
    for (const auto& Arg : ExtraArgs) {
      Args.push_back(Arg);
    }
    return Args;
  }

  // Creates an AST both for the From and To source code and imports the Decl
  // of the identifier into the To context.
  // Must not be called more than once within the same test.
  std::tuple<Decl *, Decl *>
  getImportedDecl(StringRef FromSrcCode, Language FromLang, StringRef ToSrcCode,
                  Language ToLang, StringRef Identifier = DeclToImportID) {
    ArgVector FromArgs = getArgVectorForLanguage(FromLang),
              ToArgs = getArgVectorForLanguage(ToLang);

    FromTUs.emplace_back(FromSrcCode, InputFileName, FromArgs);
    TU &FromTU = FromTUs.back();

    ToCode = ToSrcCode;
    assert(!ToAST);
    ToAST = tooling::buildASTFromCodeWithArgs(ToCode, ToArgs, OutputFileName);
    ToAST->beginSourceFile();

    ASTContext &FromCtx = FromTU.Unit->getASTContext();

    createVirtualFileIfNeeded(ToAST.get(), InputFileName, FromTU.Code);

    IdentifierInfo *ImportedII = &FromCtx.Idents.get(Identifier);
    assert(ImportedII && "Declaration with the given identifier "
                         "should be specified in test!");
    DeclarationName ImportDeclName(ImportedII);
    SmallVector<NamedDecl *, 4> FoundDecls;
    FromCtx.getTranslationUnitDecl()->localUncachedLookup(ImportDeclName,
                                                          FoundDecls);

    assert(FoundDecls.size() == 1);

    Decl *Imported = FromTU.import(ToAST.get(), FoundDecls.front());
    
    assert(Imported);
    return std::make_tuple(*FoundDecls.begin(), Imported);
  }

  // Creates a TU decl for the given source code.
  // May be called several times in a given test (with different file name).
  TranslationUnitDecl *getTuDecl(StringRef SrcCode, Language Lang,
                                 StringRef FileName = "input.cc") {
    assert(
        std::find_if(FromTUs.begin(), FromTUs.end(), [FileName](const TU &E) {
          return E.FileName == FileName;
        }) == FromTUs.end());

    ArgVector Args = getArgVectorForLanguage(Lang);
    FromTUs.emplace_back(SrcCode, FileName, Args);
    TU &Tu = FromTUs.back();

    return Tu.TUDecl;
  }

  // Import the given Decl into the ToCtx.
  // May be called several times in a given test.
  // The different instances of the param From may have different ASTContext.
  Decl *Import(Decl *From, Language ToLang) {
    lazyInitToAST(ToLang);
    TU *FromTU = findFromTU(From);
    return FromTU->import(ToAST.get(), From);
  }

  QualType ImportType(QualType FromType, Decl *TUDecl, Language ToLang) {
    lazyInitToAST(ToLang);
    TU *FromTU = findFromTU(TUDecl);
    return FromTU->import(ToAST.get(), FromType);
  }

  ~ASTImporterTestBase() {
    if (!::testing::Test::HasFailure()) return;

    for (auto &Tu : FromTUs) {
      assert(Tu.Unit);
      llvm::errs() << "FromAST:\n";
      Tu.Unit->getASTContext().getTranslationUnitDecl()->dump();
      llvm::errs() << "\n";
    }
    if (ToAST) {
      llvm::errs() << "ToAST:\n";
      ToAST->getASTContext().getTranslationUnitDecl()->dump();
    }
  }
};

AST_MATCHER_P(RecordDecl, hasFieldOrder, std::vector<StringRef>, Order) {
  size_t Index = 0;
  for (FieldDecl *Field : Node.fields()) {
    if (Index == Order.size())
      return false;
    if (Field->getName() != Order[Index])
      return false;
    ++Index;
  }
  return Index == Order.size();
}

TEST(ImportExpr, ImportStringLiteral) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() { \"foo\"; }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     stringLiteral(
                       hasType(
                         asString("const char [4]"))))))));
  testImport("void declToImport() { L\"foo\"; }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     stringLiteral(
                       hasType(
                        asString("const wchar_t [4]"))))))));
  testImport("void declToImport() { \"foo\" \"bar\"; }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     stringLiteral(
                       hasType(
                         asString("const char [7]"))))))));
}

TEST(ImportExpr, ImportGNUNullExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() { __null; }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     gnuNullExpr(
                       hasType(isInteger())))))));
}

TEST(ImportExpr, ImportCXXNullPtrLiteralExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() { nullptr; }",
             Lang_CXX11, {"-Wno-unused-value"}, "", Lang_CXX11, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     cxxNullPtrLiteralExpr())))));
}


TEST(ImportExpr, ImportFloatingLiteralExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() { 1.0; }",
             Lang_C, {"-Wno-unused-value"}, "", Lang_C, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     floatLiteral(
                       equals(1.0),
                       hasType(asString("double"))))))));
  testImport("void declToImport() { 1.0e-5f; }",
             Lang_C, {"-Wno-unused-value"}, "", Lang_C, {}, Verifier,
              functionDecl(
                hasBody(
                  compoundStmt(
                    has(
                      floatLiteral(
                        equals(1.0e-5f),
                        hasType(asString("float"))))))));
}

TEST(ImportExpr, ImportCompoundLiteralExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() {"
             "  struct s { int x; long y; unsigned z; }; "
             "  (struct s){ 42, 0L, 1U }; }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     compoundLiteralExpr(
                       hasType(asString("struct s")),
                       has(initListExpr(
                         hasType(asString("struct s")),
                         has(integerLiteral(
                               equals(42), hasType(asString("int")))),
                         has(integerLiteral(
                               equals(0), hasType(asString("long")))),
                         has(integerLiteral(
                               equals(1),
                               hasType(asString("unsigned int"))))
                         ))))))));
}

TEST(ImportExpr, ImportCXXThisExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("class declToImport { void f() { this; } };",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             cxxRecordDecl(
               hasMethod(
                 hasBody(
                   compoundStmt(
                     has(
                       cxxThisExpr(
                         hasType(
                           asString("class declToImport *")))))))));
}

TEST(ImportExpr, ImportAtomicExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() { int *ptr; __atomic_load_n(ptr, 1); }",
             Lang_C, "", Lang_C, Verifier,
             functionDecl(hasBody(compoundStmt(has(atomicExpr(
                 has(ignoringParenImpCasts(
                     declRefExpr(hasDeclaration(varDecl(hasName("ptr"))),
                                 hasType(asString("int *"))))),
                 has(integerLiteral(equals(1), hasType(asString("int"))))))))));
}

TEST(ImportExpr, ImportLabelDeclAndAddrLabelExpr) {
  MatchVerifier<Decl> Verifier;
  testImport(
      "void declToImport() { loop: goto loop; &&loop; }",
      Lang_C, {"-Wno-unused-value"}, "", Lang_C, {},
      Verifier,
      functionDecl(hasBody(compoundStmt(
          has(labelStmt(hasDeclaration(labelDecl(hasName("loop"))))),
          has(addrLabelExpr(hasDeclaration(labelDecl(hasName("loop")))))))));
}

AST_MATCHER_P(TemplateDecl, hasTemplateDecl,
              internal::Matcher<NamedDecl>, InnerMatcher) {
  const NamedDecl *Template = Node.getTemplatedDecl();
  return Template && InnerMatcher.matches(*Template, Finder, Builder);
}

TEST(ImportExpr, ImportParenListExpr) {
  MatchVerifier<Decl> Verifier;
  testImport(
      "template<typename T> class dummy { void f() { dummy X(*this); } };"
      "typedef dummy<int> declToImport;"
      "template class dummy<int>;",
      Lang_CXX, "", Lang_CXX, Verifier,
      typedefDecl(hasType(templateSpecializationType(
          hasDeclaration(classTemplateSpecializationDecl(hasSpecializedTemplate(
              classTemplateDecl(hasTemplateDecl(cxxRecordDecl(hasMethod(allOf(
                  hasName("f"),
                  hasBody(compoundStmt(has(declStmt(hasSingleDecl(
                      varDecl(hasInitializer(parenListExpr(has(unaryOperator(
                          hasOperatorName("*"),
                          hasUnaryOperand(cxxThisExpr())))))))))))))))))))))));
}

TEST(ImportExpr, ImportSwitch) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() { int b; switch (b) { case 1: break; } }",
             Lang_C, "", Lang_C, Verifier,
             functionDecl(hasBody(compoundStmt(
                 has(switchStmt(has(compoundStmt(has(caseStmt())))))))));
}

TEST(ImportExpr, ImportStmtExpr) {
  MatchVerifier<Decl> Verifier;
  // NOTE: has() ignores implicit casts, using hasDescendant() to match it
  testImport(
    "void declToImport() { int b; int a = b ?: 1; int C = ({int X=4; X;}); }",
    Lang_C, "", Lang_C, Verifier,
    functionDecl(
      hasBody(
        compoundStmt(
          has(
            declStmt(
              hasSingleDecl(
                varDecl(
                  hasName("C"),
                  hasType(asString("int")),
                  hasInitializer(
                    stmtExpr(
                      hasAnySubstatement(
                        declStmt(
                          hasSingleDecl(
                            varDecl(
                              hasName("X"),
                              hasType(asString("int")),
                              hasInitializer(
                                integerLiteral(equals(4))))))),
                      hasDescendant(
                        implicitCastExpr()
                        )))))))))));
}

TEST(ImportExpr, ImportConditionalOperator) {
  MatchVerifier<Decl> Verifier;
  testImport(
    "void declToImport() { true ? 1 : -5; }",
    Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
    functionDecl(
      hasBody(
        compoundStmt(
          has(
            conditionalOperator(
              hasCondition(cxxBoolLiteral(equals(true))),
              hasTrueExpression(integerLiteral(equals(1))),
              hasFalseExpression(
                unaryOperator(hasUnaryOperand(integerLiteral(equals(5))))
                )))))));
}

TEST(ImportExpr, ImportBinaryConditionalOperator) {
  MatchVerifier<Decl> Verifier;
  testImport(
    "void declToImport() { 1 ?: -5; }",
    Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
    functionDecl(
      hasBody(
        compoundStmt(
          has(
            binaryConditionalOperator(
              hasCondition(
                implicitCastExpr(
                  hasSourceExpression(
                    opaqueValueExpr(
                      hasSourceExpression(integerLiteral(equals(1))))),
                  hasType(booleanType()))),
              hasTrueExpression(
                opaqueValueExpr(hasSourceExpression(
                                  integerLiteral(equals(1))))),
              hasFalseExpression(
                unaryOperator(hasOperatorName("-"),
                              hasUnaryOperand(integerLiteral(equals(5)))))
                ))))));
}

TEST(ImportExpr, ImportDesignatedInitExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() {"
             "  struct point { double x; double y; };"
             "  struct point ptarray[10] = "
                    "{ [2].y = 1.0, [2].x = 2.0, [0].x = 1.0 }; }",
             Lang_C, "", Lang_C, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     declStmt(
                       hasSingleDecl(
                         varDecl(
                           hasInitializer(
                             initListExpr(
                               hasSyntacticForm(
                                 initListExpr(
                                   has(
                                     designatedInitExpr(
                                       designatorCountIs(2),
                                       has(floatLiteral(
                                             equals(1.0))),
                                       has(integerLiteral(
                                             equals(2))))),
                                   has(
                                     designatedInitExpr(
                                       designatorCountIs(2),
                                       has(floatLiteral(
                                             equals(2.0))),
                                       has(integerLiteral(
                                             equals(2))))),
                                   has(
                                     designatedInitExpr(
                                       designatorCountIs(2),
                                       has(floatLiteral(
                                             equals(1.0))),
                                       has(integerLiteral(
                                             equals(0)))))
                                   ))))))))))));
}


TEST(ImportExpr, ImportPredefinedExpr) {
  MatchVerifier<Decl> Verifier;
  // __func__ expands as StringLiteral("declToImport")
  testImport("void declToImport() { __func__; }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     predefinedExpr(
                       hasType(
                         asString("const char [13]")),
                       has(
                         stringLiteral(
                           hasType(
                             asString("const char [13]"))))))))));
}

TEST(ImportExpr, ImportInitListExpr) {
  MatchVerifier<Decl> Verifier;
  testImport(
    "void declToImport() {"
    "  struct point { double x; double y; };"
    "  point ptarray[10] = { [2].y = 1.0, [2].x = 2.0,"
    "                        [0].x = 1.0 }; }",
    Lang_CXX, "", Lang_CXX, Verifier,
    functionDecl(
      hasBody(
        compoundStmt(
          has(
            declStmt(
              hasSingleDecl(
                varDecl(
                  hasInitializer(
                    initListExpr(
                      has(
                        cxxConstructExpr(
                          requiresZeroInitialization())),
                      has(
                        initListExpr(
                          hasType(asString("struct point")),
                          has(floatLiteral(equals(1.0))),
                          has(implicitValueInitExpr(
                                hasType(asString("double")))))),
                      has(
                        initListExpr(
                          hasType(asString("struct point")),
                          has(floatLiteral(equals(2.0))),
                          has(floatLiteral(equals(1.0)))))
                        ))))))))));
}


const internal::VariadicDynCastAllOfMatcher<Expr, VAArgExpr> vaArgExpr;

TEST(ImportExpr, ImportVAArgExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport(__builtin_va_list list, ...) {"
             "  (void)__builtin_va_arg(list, int); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     cStyleCastExpr(
                       hasSourceExpression(
                         vaArgExpr())))))));
}

TEST(ImportExpr, CXXTemporaryObjectExpr) {
  MatchVerifier<Decl> Verifier;
  testImport(
      "struct C {};"
      "void declToImport() { C c = C(); }",
      Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
      functionDecl(hasBody(compoundStmt(
          hasDescendant(cxxTemporaryObjectExpr())))));
}

TEST(ImportType, ImportAtomicType) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() { typedef _Atomic(int) a_int; }",
             Lang_CXX11, "", Lang_CXX11, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     declStmt(
                       has(
                         typedefDecl(
                           has(atomicType())))))))));
}

TEST(ImportDecl, ImportFunctionTemplateDecl) {
  MatchVerifier<Decl> Verifier;
  testImport(
          "template <typename T> void declToImport() { };",
          Lang_CXX, "", Lang_CXX, Verifier,
          functionTemplateDecl());
  testImport(
      "template<typename Y> int a() { return 1; }"
      "template<typename Y, typename D> int a(){ return 2; }"
      "void declToImport() { a<void>(); }",
      Lang_CXX, "", Lang_CXX, Verifier,
      functionDecl(has(compoundStmt(has(callExpr(has(ignoringParenImpCasts(
          declRefExpr(to(functionDecl(hasBody(compoundStmt(
              has(returnStmt(has(integerLiteral(equals(1))))))))))))))))));
  testImport(
      "template<typename Y> int a() { return 1; }"
      "template<typename Y, typename D> int a() { return 2; }"
      "void declToImport() { a<void,void>(); }",
      Lang_CXX, "", Lang_CXX, Verifier,
      functionDecl(has(compoundStmt(has(callExpr(has(ignoringParenImpCasts(
          declRefExpr(to(functionDecl(hasBody(compoundStmt(
              has(returnStmt(has(integerLiteral(equals(2))))))))))))))))));
}

const internal::VariadicDynCastAllOfMatcher<Expr, CXXDependentScopeMemberExpr>
    cxxDependentScopeMemberExpr;

TEST(ImportExpr, ImportCXXDependentScopeMemberExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("template <typename T> struct C { T t; };"
             "template <typename T> void declToImport() {"
             "  C<T> d;"
             "  d.t;"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionTemplateDecl(has(functionDecl(
                 has(compoundStmt(has(cxxDependentScopeMemberExpr())))))));
  testImport("template <typename T> struct C { T t; };"
             "template <typename T> void declToImport() {"
             "  C<T> d;"
             "  (&d)->t;"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionTemplateDecl(has(functionDecl(
                 has(compoundStmt(has(cxxDependentScopeMemberExpr())))))));
}

TEST(ImportType, ImportTypeAliasTemplate) {
  MatchVerifier<Decl> Verifier;
  testImport("template <int K>"
             "struct dummy { static const int i = K; };"
             "template <int K> using dummy2 = dummy<K>;"
             "int declToImport() { return dummy2<3>::i; }",
             Lang_CXX11, "", Lang_CXX11, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     returnStmt(
                       has(
                         implicitCastExpr(
                           has(
                             declRefExpr())))))))));
}

const internal::VariadicDynCastAllOfMatcher<Decl, VarTemplateSpecializationDecl>
    varTemplateSpecializationDecl;

TEST(ImportDecl, ImportVarTemplate) {
  // FIXME: should use a Lang_CXX14?
  MatchVerifier<Decl> Verifier;
  testImport(
      "template <typename T>"
      "T pi = T(3.1415926535897932385L);"
      "void declToImport() { pi<int>; }",
      Lang_CXX11, {"-Wno-unused-value"}, "", Lang_CXX11, {}, Verifier,
      functionDecl(
          hasBody(has(declRefExpr(to(varTemplateSpecializationDecl())))),
          unless(hasAncestor(translationUnitDecl(has(varDecl(
              hasName("pi"), unless(varTemplateSpecializationDecl()))))))));
}

TEST(ImportType, ImportPackExpansion) {
  MatchVerifier<Decl> Verifier;
  testImport("template <typename... Args>"
             "struct dummy {"
             "  dummy(Args... args) {}"
             "  static const int i = 4;"
             "};"
             "int declToImport() { return dummy<int>::i; }",
             Lang_CXX11, "", Lang_CXX11, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     returnStmt(
                       has(
                         implicitCastExpr(
                           has(
                             declRefExpr())))))))));
}

const internal::VariadicDynCastAllOfMatcher<Type,
                                            DependentTemplateSpecializationType>
    dependentTemplateSpecializationType;

TEST(ImportType, ImportDependentTemplateSpecialization) {
  MatchVerifier<Decl> Verifier;
  testImport(
      "template<typename T>"
      "struct A;"
      "template<typename T>"
      "struct declToImport {"
      "  typename A<T>::template B<T> a;"
      "};",
      Lang_CXX, "", Lang_CXX, Verifier,
      classTemplateDecl(has(cxxRecordDecl(
          has(fieldDecl(hasType(dependentTemplateSpecializationType())))))));
}

const internal::VariadicDynCastAllOfMatcher<Stmt, SizeOfPackExpr>
    sizeOfPackExpr;

TEST(ImportExpr, ImportSizeOfPackExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("template <typename... Ts>"
             "void declToImport() {"
             "  const int i = sizeof...(Ts);"
             "};"
             "void instantiate() { declToImport<void>(); }",
             Lang_CXX11, "", Lang_CXX11, Verifier,
             functionTemplateDecl(has(functionDecl(hasBody(
                 compoundStmt(has(declStmt(has(varDecl(hasInitializer(
                     implicitCastExpr(has(sizeOfPackExpr())))))))))))));
  testImport(
      "template <typename... Ts>"
      "using X = int[sizeof...(Ts)];"
      "template <typename... Us>"
      "struct Y {"
      "  X<Us..., int, double, int, Us...> f;"
      "};"
      "Y<float, int> declToImport;",
      Lang_CXX11, "", Lang_CXX11, Verifier,
      varDecl(hasType(classTemplateSpecializationDecl(has(fieldDecl(hasType(
          hasUnqualifiedDesugaredType(constantArrayType(hasSize(7))))))))));
}

/// \brief Matches __builtin_types_compatible_p:
/// GNU extension to check equivalent types
/// Given
/// \code
///   __builtin_types_compatible_p(int, int)
/// \endcode
//  will generate TypeTraitExpr <...> 'int'
const internal::VariadicDynCastAllOfMatcher<Stmt, TypeTraitExpr> typeTraitExpr;

TEST(ImportExpr, ImportTypeTraitExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() { "
             "  __builtin_types_compatible_p(int, int);"
             "}",
             Lang_C, {"-Wno-unused-value"}, "", Lang_C, {}, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     typeTraitExpr(hasType(asString("int"))))))));
}

const internal::VariadicDynCastAllOfMatcher<Stmt, CXXTypeidExpr> cxxTypeidExpr;

TEST(ImportExpr, ImportCXXTypeidExpr) {
  MatchVerifier<Decl> Verifier;
  testImport(
      "namespace std { class type_info {}; }"
      "void declToImport() {"
      "  int x;"
      "  auto a = typeid(int); auto b = typeid(x);"
      "}",
      Lang_CXX11, {"-Wno-unused-value"}, "", Lang_CXX11, {}, Verifier,
      functionDecl(
          hasDescendant(varDecl(
              hasName("a"), hasInitializer(hasDescendant(cxxTypeidExpr())))),
          hasDescendant(varDecl(
              hasName("b"), hasInitializer(hasDescendant(cxxTypeidExpr()))))));
}

TEST(ImportExpr, ImportTypeTraitExprValDep) {
  MatchVerifier<Decl> Verifier;
  testImport("template<typename T> struct declToImport {"
             "  void m() { __is_pod(T); }"
             "};"
             "void f() { declToImport<int>().m(); }",
             Lang_CXX11, {"-Wno-unused-value"}, "", Lang_CXX11, {}, Verifier,
             classTemplateDecl(
               has(
                 cxxRecordDecl(
                   has(
                     functionDecl(
                       hasBody(
                         compoundStmt(
                           has(
                             typeTraitExpr(
                               hasType(booleanType())
                               ))))))))));
}

const internal::VariadicDynCastAllOfMatcher<Expr, CXXPseudoDestructorExpr>
    cxxPseudoDestructorExpr;

TEST(ImportExpr, ImportCXXPseudoDestructorExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("typedef int T;"
             "void declToImport(int *p) {"
             "  T t;"
             "  p->T::~T();"
             "}",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionDecl(has(compoundStmt(has(
                 callExpr(has(cxxPseudoDestructorExpr())))))));
}

TEST(ImportDecl, ImportUsingDecl) {
  MatchVerifier<Decl> Verifier;
  testImport("namespace foo { int bar; }"
             "int declToImport(){ using foo::bar; }",
             Lang_CXX, {"-Wno-return-type"}, "", Lang_CXX, {}, Verifier,
             functionDecl(
               has(
                 compoundStmt(
                   has(
                     declStmt(
                       has(
                         usingDecl())))))));
}

TEST(ImportDecl, ImportRecordDeclInFunc) {
  MatchVerifier<Decl> Verifier;
  testImport("int declToImport() { "
             "  struct data_t {int a;int b;};"
             "  struct data_t d;"
             "  return 0;"
             "}",
             Lang_C, "", Lang_C, Verifier,
             functionDecl(
               hasBody(
                 compoundStmt(
                   has(
                     declStmt(
                       hasSingleDecl(
                         varDecl(hasName("d")))))))));
}

TEST_P(ASTImporterTestBase, ImportRecordTypeInFunc) {
  Decl *FromTU = getTuDecl(
      "int declToImport() { "
      "  struct data_t {int a;int b;};"
      "  struct data_t d;"
      "  return 0;"
      "}",
      Lang_C,
      "input.c");
  auto FromVar = FirstDeclMatcher<VarDecl>().match(FromTU, varDecl(hasName("d")));
  ASSERT_TRUE(FromVar);
  auto ToType = ImportType(FromVar->getType().getCanonicalType(), FromVar, Lang_C);
  ASSERT_FALSE(ToType.isNull());
}

TEST_P(ASTImporterTestBase, ImportRecordDeclInFuncParams) {
  // This construct is not supported by ASTImporter.
  Decl *FromTU = getTuDecl(
      "int declToImport(struct data_t{int a;int b;} *d){ return 0; }",
      Lang_C,
      "input.c");
  auto From = FirstDeclMatcher<FunctionDecl>().match(FromTU, functionDecl());
  ASSERT_TRUE(From);
  auto To = Import(From, Lang_C);
  EXPECT_EQ(To, nullptr);
}

/// \brief Matches shadow declarations introduced into a scope by a
///        (resolved) using declaration.
///
/// Given
/// \code
///   namespace n { int f; }
///   namespace declToImport { using n::f; }
/// \endcode
/// usingShadowDecl()
///   matches \code f \endcode
const internal::VariadicDynCastAllOfMatcher<Decl,
                                            UsingShadowDecl> usingShadowDecl;

TEST(ImportDecl, ImportUsingShadowDecl) {
  MatchVerifier<Decl> Verifier;
  testImport("namespace foo { int bar; }"
             "namespace declToImport { using foo::bar; }",
             Lang_CXX, "", Lang_CXX, Verifier,
             namespaceDecl(has(usingShadowDecl())));
}

TEST(ImportDecl, DISABLED_ImportTemplateDefaultArgument) {
  MatchVerifier<Decl> Verifier;
        testImport(
          "template <typename T=int> void declToImport(T &t) { };",
          Lang_CXX11, "", Lang_CXX11, Verifier,
          functionTemplateDecl(has(templateArgument())));
}

TEST_P(ASTImporterTestBase, ImportOfTemplatedDeclOfClassTemplateDecl) {
  Decl *FromTU = getTuDecl("template<class X> struct S{};", Lang_CXX);
  auto From =
      FirstDeclMatcher<ClassTemplateDecl>().match(FromTU, classTemplateDecl());
  ASSERT_TRUE(From);
  auto To = cast<ClassTemplateDecl>(Import(From, Lang_CXX));
  ASSERT_TRUE(To);
  Decl *ToTemplated = To->getTemplatedDecl();
  Decl *ToTemplated1 = Import(From->getTemplatedDecl(), Lang_CXX);
  EXPECT_TRUE(ToTemplated1);
  EXPECT_EQ(ToTemplated1, ToTemplated);
}

TEST_P(ASTImporterTestBase, ImportOfTemplatedDeclOfFunctionTemplateDecl) {
  Decl *FromTU = getTuDecl("template<class X> void f(){}", Lang_CXX);
  auto From = FirstDeclMatcher<FunctionTemplateDecl>().match(
      FromTU, functionTemplateDecl());
  ASSERT_TRUE(From);
  auto To = cast<FunctionTemplateDecl>(Import(From, Lang_CXX));
  ASSERT_TRUE(To);
  Decl *ToTemplated = To->getTemplatedDecl();
  ToTemplated->dump();
  Decl *ToTemplated1 = Import(From->getTemplatedDecl(), Lang_CXX);
  EXPECT_TRUE(ToTemplated1);
  ToTemplated1->dump();
  EXPECT_EQ(ToTemplated1, ToTemplated);
}

TEST_P(ASTImporterTestBase,
       ImportOfTemplatedDeclShouldImportTheClassTemplateDecl) {
  Decl *FromTU = getTuDecl("template<class X> struct S{};", Lang_CXX);
  auto FromFT =
      FirstDeclMatcher<ClassTemplateDecl>().match(FromTU, classTemplateDecl());
  ASSERT_TRUE(FromFT);

  auto ToTemplated =
      cast<CXXRecordDecl>(Import(FromFT->getTemplatedDecl(), Lang_CXX));
  EXPECT_TRUE(ToTemplated);
  auto ToTU = ToTemplated->getTranslationUnitDecl();
  auto ToFT =
      FirstDeclMatcher<ClassTemplateDecl>().match(ToTU, classTemplateDecl());
  EXPECT_TRUE(ToFT);
}

TEST_P(ASTImporterTestBase,
       ImportOfTemplatedDeclShouldImportTheFunctionTemplateDecl) {
  Decl *FromTU = getTuDecl("template<class X> void f(){}", Lang_CXX);
  auto FromFT = FirstDeclMatcher<FunctionTemplateDecl>().match(
      FromTU, functionTemplateDecl());
  ASSERT_TRUE(FromFT);

  auto ToTemplated =
      cast<FunctionDecl>(Import(FromFT->getTemplatedDecl(), Lang_CXX));
  EXPECT_TRUE(ToTemplated);
  auto ToTU = ToTemplated->getTranslationUnitDecl();
  auto ToFT = FirstDeclMatcher<FunctionTemplateDecl>().match(
      ToTU, functionTemplateDecl());
  EXPECT_TRUE(ToFT);
}

TEST_P(ASTImporterTestBase, ImportCorrectTemplatedDecl) {
  auto Code =
        R"(
        namespace x {
          template<class X> struct S1{};
          template<class X> struct S2{};
          template<class X> struct S3{};
        }
        )";
  Decl *FromTU = getTuDecl(Code, Lang_CXX);
  auto FromNs =
      FirstDeclMatcher<NamespaceDecl>().match(FromTU, namespaceDecl());
  auto ToNs = cast<NamespaceDecl>(Import(FromNs, Lang_CXX));
  ASSERT_TRUE(ToNs);
  auto From =
      FirstDeclMatcher<ClassTemplateDecl>().match(FromTU,
                                                  classTemplateDecl(
                                                      hasName("S2")));
  auto To =
      FirstDeclMatcher<ClassTemplateDecl>().match(ToNs,
                                                  classTemplateDecl(
                                                      hasName("S2")));
  ASSERT_TRUE(From);
  ASSERT_TRUE(To);
  auto ToTemplated = To->getTemplatedDecl();
  auto ToTemplated1 =
      cast<CXXRecordDecl>(Import(From->getTemplatedDecl(), Lang_CXX));
  EXPECT_TRUE(ToTemplated1);
  ASSERT_EQ(ToTemplated1, ToTemplated);
}

TEST(ImportExpr, ImportClassTemplatePartialSpecialization) {
  MatchVerifier<Decl> Verifier;
  auto Code =  R"s(
struct declToImport {
  template <typename T0>
  struct X;
   template <typename T0>
  struct X<T0*> {};
};
                   )s";

  testImport(Code, Lang_CXX, "", Lang_CXX, Verifier, recordDecl());
}

TEST(ImportExpr, ImportClassTemplatePartialSpecializationComplex) {
  MatchVerifier<Decl> Verifier;
  auto Code = R"s(
// excerpt from <functional>

namespace declToImport {

template <typename _MemberPointer>
class _Mem_fn;

template <typename _Tp, typename _Class>
_Mem_fn<_Tp _Class::*> mem_fn(_Tp _Class::*);

template <typename _Res, typename _Class>
class _Mem_fn<_Res _Class::*> {
    template <typename _Signature>
    struct result;

    template <typename _CVMem, typename _Tp>
    struct result<_CVMem(_Tp)> {};

    template <typename _CVMem, typename _Tp>
    struct result<_CVMem(_Tp&)> {};
};

} // namespace
                  )s";

  testImport(Code, Lang_CXX, "", Lang_CXX, Verifier,
                         namespaceDecl());
}

TEST(ImportExpr, ImportTypedefOfUnnamedStruct) {
  MatchVerifier<Decl> Verifier;
  auto Code = "typedef struct {} declToImport;";
  testImport(Code, Lang_CXX, "", Lang_CXX, Verifier, typedefDecl());
}

TEST(ImportExpr, ImportTypedefOfUnnamedStructWithCharArray) {
  MatchVerifier<Decl> Verifier;
  auto Code = R"s(
      struct declToImport
      {
        typedef struct { char arr[2]; } two;
      };
          )s";
  testImport(Code, Lang_CXX, "", Lang_CXX, Verifier, recordDecl());
}

TEST(ImportExpr, ImportVarOfUnnamedStruct) {
  MatchVerifier<Decl> Verifier;
  testImport("struct {} declToImport;", Lang_CXX, "", Lang_CXX,
                         Verifier, varDecl());
}

TEST_P(ASTImporterTestBase, ImportFunctionWithBackReferringParameter) {
  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
    R"(
template<typename _T>
struct X {};

void declToImport(int y,X<int> &x){}

template<>
struct X<int> {
  void g(){
    X<int> x;
    declToImport(0,x);
  }
};
    )",Lang_CXX, "", Lang_CXX);

  MatchVerifier<Decl> Verifier;
  auto Matcher = functionDecl(hasName("declToImport"),
                              parameterCountIs(2),
                              hasParameter(0, hasName("y")),
                              hasParameter(1, hasName("x")),
                              hasParameter(1, hasType(asString("X<int> &"))));
  ASSERT_TRUE(Verifier.match(From, Matcher));
  EXPECT_TRUE(Verifier.match(To, Matcher));
}

const internal::VariadicDynCastAllOfMatcher<Expr, UnresolvedMemberExpr>
    unresolvedMemberExpr;
TEST(ImportExpr, ImportUnresolvedMemberExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("struct S { template <typename T> void mem(); };"
             "template <typename U> void declToImport() {"
             "S s;"
             "s.mem<U>();"
             "}"
             "void instantiate() { declToImport<void>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(has(functionDecl(has(compoundStmt(
                 has(callExpr(has(unresolvedMemberExpr())))))))));
}

const internal::VariadicDynCastAllOfMatcher<Expr, DependentScopeDeclRefExpr>
    dependentScopeDeclRefExpr;
TEST(ImportExpr, ImportDependentScopeDeclRefExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("template <typename T> struct S { static const int foo = 0; };"
             "template <typename T> void declToImport() {"
             "S<T>::foo;"
             "}"
             "void instantiate() { declToImport<void>(); }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionTemplateDecl(has(functionDecl(has(compoundStmt(
                 has(dependentScopeDeclRefExpr())))))));

  testImport("template <typename T> struct S {"
             "  template <typename S> class foo {};"
             "};"
             "template <typename T> void declToImport() {"
             "S<T>::template foo;"
             "}"
             "void instantiate() { declToImport<void>(); }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionTemplateDecl(has(functionDecl(has(compoundStmt(
                 has(dependentScopeDeclRefExpr())))))));

  testImport("template <typename T> struct S {"
             "  template <typename S = void> class foo {};"
             "};"
             "template <typename T> void declToImport() {"
             "S<T>::template foo<>;"
             "}"
             "void instantiate() { declToImport<void>(); }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionTemplateDecl(has(functionDecl(has(compoundStmt(
                 has(dependentScopeDeclRefExpr())))))));

  testImport("template <typename T> struct S {"
             "  template <typename S> class foo {};"
             "};"
             "template <typename T> void declToImport() {"
             "S<T>::template foo<T>;"
             "}"
             "void instantiate() { declToImport<void>(); }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
              functionTemplateDecl(has(functionDecl(has(compoundStmt(
                  has(dependentScopeDeclRefExpr())))))));
}

const internal::VariadicDynCastAllOfMatcher<Type, DependentNameType>
    dependentNameType;
TEST(ImportExpr, DependentNameType) {
  MatchVerifier<Decl> Verifier;
  testImport("template <typename T> struct declToImport {"
             "typedef typename T::type dependent_name;"
             "};",
             Lang_CXX11, "", Lang_CXX11, Verifier,
             classTemplateDecl(has(cxxRecordDecl(
                 has(typedefDecl(has(dependentNameType())))))));
}

TEST(ImportExpr, DependentSizedArrayType) {
  MatchVerifier<Decl> Verifier;
  testImport("template<typename T, int Size> class declToImport {"
             "  T data[Size];"
             "};",
             Lang_CXX, "", Lang_CXX, Verifier,
             classTemplateDecl(has(cxxRecordDecl(has(fieldDecl(
                 hasType(dependentSizedArrayType())))))));
}

TEST(ImportExpr, CXXOperatorCallExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("class declToImport {"
             "  void f() { *this = declToImport(); }"
             "};",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             cxxRecordDecl(has(cxxMethodDecl(hasBody(compoundStmt(
                 has(exprWithCleanups(
                     has(cxxOperatorCallExpr())))))))));
}

TEST(ImportExpr, CXXNamedCastExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("void declToImport() {"
             "  const_cast<char*>(\"hello\");"
             "}",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(hasBody(compoundStmt(has(
                 cxxConstCastExpr())))));
  testImport("void declToImport() {"
             "  double d;"
             "  reinterpret_cast<int*>(&d);"
             "}",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(hasBody(compoundStmt(has(
                 cxxReinterpretCastExpr())))));
  testImport("struct A {virtual ~A() {} };"
             "struct B : A {};"
             "void declToImport() {"
             "  dynamic_cast<B*>(new A);"
             "}",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionDecl(hasBody(compoundStmt(has(
                 cxxDynamicCastExpr())))));
  testImport("struct A {virtual ~A() {} };"
             "struct B : A {};"
              "void declToImport() {"
              "  static_cast<B*>(new A);"
              "}",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
              functionDecl(hasBody(compoundStmt(has(
                  cxxStaticCastExpr())))));
}

TEST(ImportExpr, ImportUnresolvedLookupExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("template<typename T> int foo();"
             "template <typename T> void declToImport() {"
             "  ::foo<T>;"
             "  ::template foo<T>;"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, {"-Wno-unused-value"}, "", Lang_CXX, {}, Verifier,
             functionTemplateDecl(has(functionDecl(
                 has(compoundStmt(has(unresolvedLookupExpr())))))));
}

TEST(ImportExpr, ImportCXXUnresolvedConstructExpr) {
  MatchVerifier<Decl> Verifier;
  testImport("template <typename T> struct C { T t; };"
             "template <typename T> void declToImport() {"
             "  C<T> d;"
             "  d.t = T();"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(has(functionDecl(has(compoundStmt(has(
                 binaryOperator(has(cxxUnresolvedConstructExpr())))))))));
  testImport("template <typename T> struct C { T t; };"
             "template <typename T> void declToImport() {"
             "  C<T> d;"
             "  (&d)->t = T();"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(has(functionDecl(has(compoundStmt(has(
                 binaryOperator(has(cxxUnresolvedConstructExpr())))))))));
}

/// Check that function "declToImport()" (which is the templated function
/// for corresponding FunctionTemplateDecl) is not added into DeclContext.
/// Same for class template declarations.
TEST(ImportDecl, DISABLED_ImportTemplatedDeclForTemplate) {
  MatchVerifier<Decl> Verifier;
  testImport("template <typename T> void declToImport() { T a = 1; }"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(hasAncestor(translationUnitDecl(
                 unless(has(functionDecl(hasName("declToImport"))))))));
  testImport("template <typename T> struct declToImport { T t; };"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             classTemplateDecl(hasAncestor(translationUnitDecl(
                 unless(has(cxxRecordDecl(hasName("declToImport"))))))));
}

TEST_P(ASTImporterTestBase, TUshouldNotContainTemplatedDeclOfFunctionTemplates) {

  Decl *From, *To;
  std::tie(From, To) =
      getImportedDecl("template <typename T> void declToImport() { T a = 1; }"
                      "void instantiate() { declToImport<int>(); }",
                      Lang_CXX, "", Lang_CXX);

  auto Check = [](Decl *D) -> bool {
    auto TU = D->getTranslationUnitDecl();
    for (auto Child : TU->decls()) {
      if (FunctionDecl *FD = dyn_cast<FunctionDecl>(Child)) {
        if (FD->getNameAsString() == "declToImport") {
          GTEST_NONFATAL_FAILURE_(
              "TU should not contain any FunctionDecl with name declToImport");
          TU->dump();
          return false;
        }
      }
    }
    return true;
  };

  assert(Check(From));
  Check(To);
}

TEST_P(ASTImporterTestBase, TUshouldNotContainTemplatedDeclOfClassTemplates) {

  Decl *From, *To;
  std::tie(From, To) =
      getImportedDecl("template <typename T> struct declToImport { T t; };"
                      "void instantiate() { declToImport<int>(); }",
                      Lang_CXX, "", Lang_CXX);

  auto Check = [](Decl *D) -> bool {
    auto TU = D->getTranslationUnitDecl();
    for (auto Child : TU->decls()) {
      if (CXXRecordDecl *RD = dyn_cast<CXXRecordDecl>(Child)) {
        if (RD->getNameAsString() == "declToImport") {
          GTEST_NONFATAL_FAILURE_(
              "TU should not contain any CXXRecordDecl with name declToImport");
          TU->dump();
          return false;
        }
      }
    }
    return true;
  };

  assert(Check(From));
  Check(To);
}

TEST_P(ASTImporterTestBase, TUshouldNotContainTemplatedDeclOfTypeAlias) {

  Decl *From, *To;
  std::tie(From, To) =
      getImportedDecl(
          "template <typename T> struct X {};"
          "template <typename T> using declToImport = X<T>;"
          "void instantiate() { declToImport<int> a; }",
                      Lang_CXX11, "", Lang_CXX11);

  auto Check = [](Decl *D) -> bool {
    auto TU = D->getTranslationUnitDecl();
    for (auto Child : TU->decls()) {
      if (TypeAliasDecl *AD = dyn_cast<TypeAliasDecl>(Child)) {
        if (AD->getNameAsString() == "declToImport") {
          GTEST_NONFATAL_FAILURE_(
              "TU should not contain any TypeAliasDecl with name declToImport");
          TU->dump();
          return false;
        }
      }
    }
    return true;
  };

  assert(Check(From));
  Check(To);
}

TEST_P(
    ASTImporterTestBase,
    TUshouldNotContainClassTemplateSpecializationOfImplicitInstantiation) {

  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
      R"(
        template<class T>
        class Base {};
        class declToImport : public Base<declToImport> {};
    )",
      Lang_CXX, "", Lang_CXX);

  // Check that the ClassTemplateSpecializationDecl is NOT the child of the TU
  auto Pattern =
      translationUnitDecl(unless(has(classTemplateSpecializationDecl())));
  ASSERT_TRUE(
      MatchVerifier<Decl>{}.match(From->getTranslationUnitDecl(), Pattern));
  EXPECT_TRUE(
      MatchVerifier<Decl>{}.match(To->getTranslationUnitDecl(), Pattern));

  // Check that the ClassTemplateSpecializationDecl is the child of the
  // ClassTemplateDecl
  Pattern = translationUnitDecl(has(classTemplateDecl(
      hasName("Base"), has(classTemplateSpecializationDecl()))));
  ASSERT_TRUE(
      MatchVerifier<Decl>{}.match(From->getTranslationUnitDecl(), Pattern));
  EXPECT_TRUE(
      MatchVerifier<Decl>{}.match(To->getTranslationUnitDecl(), Pattern));
}

TEST_P(
    ASTImporterTestBase,
    TUshouldContainClassTemplateSpecializationOfExplicitInstantiation) {

  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
      R"(
        namespace NS {
          template<class T>
          class X {};
          template class X<int>;
        }
    )",
      Lang_CXX, "", Lang_CXX, "NS");

  // Check that the ClassTemplateSpecializationDecl is NOT the child of the
  // ClassTemplateDecl
  auto Pattern = namespaceDecl(has(classTemplateDecl(
      hasName("X"), unless(has(classTemplateSpecializationDecl())))));
  ASSERT_TRUE(MatchVerifier<Decl>{}.match(From, Pattern));
  EXPECT_TRUE(MatchVerifier<Decl>{}.match(To, Pattern));

  // Check that the ClassTemplateSpecializationDecl is the child of the
  // NamespaceDecl
  Pattern = namespaceDecl(has(classTemplateSpecializationDecl(hasName("X"))));
  ASSERT_TRUE(MatchVerifier<Decl>{}.match(From, Pattern));
  EXPECT_TRUE(MatchVerifier<Decl>{}.match(To, Pattern));
}


TEST_P(ASTImporterTestBase, CXXRecordDeclFieldsShouldBeInCorrectOrder) {


  Decl *From, *To;
  std::tie(From, To) =
      getImportedDecl(
          "struct declToImport { int a; int b; };",
                      Lang_CXX11, "", Lang_CXX11);

  MatchVerifier<Decl> Verifier;
  ASSERT_TRUE(Verifier.match(From, cxxRecordDecl(has(fieldDecl()))));
  ASSERT_TRUE(Verifier.match(To, cxxRecordDecl(has(fieldDecl()))));

  auto Check = [](Decl *D) -> bool {
    std::array<const char*, 2> FieldNamesInOrder{{"a", "b"}};
    int i = 0;
    for (auto Child : cast<DeclContext>(D)->decls()) {
      if (FieldDecl *FD = dyn_cast<FieldDecl>(Child)) {
        if (FD->getNameAsString() != FieldNamesInOrder[i++]) {
          GTEST_NONFATAL_FAILURE_(
              "Fields are in wrong order");
          cast<DeclContext>(D)->dumpDeclContext();
          D->dump();
          return false;
        }
      }
    }
    return true;
  };

  assert(Check(From));
  Check(To);
}

TEST_P(ASTImporterTestBase,
    CXXRecordDeclFieldsShouldBeInCorrectOrderEvenWhenWeImportFirstTheLastDecl) {

  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
      // The original recursive algorithm of ASTImporter first imports 'c' then
      // 'b' and lastly 'a'.  Therefore we must restore the order somehow.
      R"s(
      struct declToImport {
          int a = c + b;
          int b = 1;
          int c = 2;
      };
      )s",
      Lang_CXX11, "", Lang_CXX11);

  MatchVerifier<Decl> Verifier;
  ASSERT_TRUE(Verifier.match(From, cxxRecordDecl(has(fieldDecl()))));
  ASSERT_TRUE(Verifier.match(To, cxxRecordDecl(has(fieldDecl()))));

  auto Check = [](Decl *D) -> bool {
    std::array<const char*, 3> FieldNamesInOrder{{"a", "b", "c"}};
    int i = 0;
    for (auto Child : cast<DeclContext>(D)->decls()) {
      if (FieldDecl *FD = dyn_cast<FieldDecl>(Child)) {
        if (FD->getNameAsString() != FieldNamesInOrder[i++]) {
          GTEST_NONFATAL_FAILURE_(
              "Fields are in wrong order");
          cast<DeclContext>(D)->dumpDeclContext();
          D->dump();
          return false;
        }
      }
    }
    return true;
  };

  assert(Check(From));
  Check(To);
}

TEST_P(ASTImporterTestBase, ShouldImportImplicitCXXRecordDecl) {
  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
      R"(
    struct declToImport {};
    )",
      Lang_CXX, "", Lang_CXX);

  MatchVerifier<Decl> Verifier;
  // matches the implicit decl
  auto Matcher = cxxRecordDecl(has(cxxRecordDecl()));
  ASSERT_TRUE(Verifier.match(From, Matcher));
  EXPECT_TRUE(Verifier.match(To, Matcher));
}

TEST_P(ASTImporterTestBase, ShouldImportImplicitCXXRecordDeclOfClassTemplate) {
  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
      R"(
    template <typename U>
    struct declToImport {
    };
    )",
      Lang_CXX, "", Lang_CXX);

  MatchVerifier<Decl> Verifier;
  // matches the implicit decl
  auto Matcher = classTemplateDecl(has(cxxRecordDecl(has(cxxRecordDecl()))));
  ASSERT_TRUE(Verifier.match(From, Matcher));
  EXPECT_TRUE(Verifier.match(To, Matcher));
}

TEST_P(
    ASTImporterTestBase,
    ShouldImportImplicitCXXRecordDeclOfClassTemplateSpecializationDecl) {
  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
      R"(
        template<class T>
        class Base {};
        class declToImport : public Base<declToImport> {};
    )",
      Lang_CXX, "", Lang_CXX);

  auto hasImplicitClass = has(cxxRecordDecl());
  auto Pattern = translationUnitDecl(has(classTemplateDecl(
      hasName("Base"), has(classTemplateSpecializationDecl(hasImplicitClass)))));
  ASSERT_TRUE(
      MatchVerifier<Decl>{}.match(From->getTranslationUnitDecl(), Pattern));
  EXPECT_TRUE(
      MatchVerifier<Decl>{}.match(To->getTranslationUnitDecl(), Pattern));
}

TEST_P(ASTImporterTestBase, IDNSOrdinary) {
  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
    R"(
    void declToImport() {}
    )",Lang_CXX, "", Lang_CXX);

  MatchVerifier<Decl> Verifier;
  auto Matcher = functionDecl();
  ASSERT_TRUE(Verifier.match(From, Matcher));
  EXPECT_TRUE(Verifier.match(To, Matcher));
  EXPECT_EQ(From->getIdentifierNamespace(), To->getIdentifierNamespace());
}

TEST_P(ASTImporterTestBase, IDNSOfNonmemberOperator) {
  Decl *FromTU = getTuDecl(
    R"(
    struct X {};
    void operator<<(int, X);
    )",Lang_CXX);
  Decl* From = LastDeclMatcher<Decl>{}.match(FromTU, functionDecl());
  const Decl* To = Import(From, Lang_CXX);
  EXPECT_EQ(From->getIdentifierNamespace(), To->getIdentifierNamespace());
}

TEST_P(
    ASTImporterTestBase,
    ShouldImportMembersOfClassTemplateSpecializationDecl) {
  Decl *From, *To;
  std::tie(From, To) = getImportedDecl(
      R"(
        template<class T>
        class Base { int a; };
        class declToImport : Base<declToImport> {};
    )",
      Lang_CXX, "", Lang_CXX);

  auto Pattern = translationUnitDecl(has(classTemplateDecl(
      hasName("Base"),
      has(classTemplateSpecializationDecl(has(fieldDecl(hasName("a"))))))));
  ASSERT_TRUE(
      MatchVerifier<Decl>{}.match(From->getTranslationUnitDecl(), Pattern));
  EXPECT_TRUE(
      MatchVerifier<Decl>{}.match(To->getTranslationUnitDecl(), Pattern));
}

struct ImportFunctions : ASTImporterTestBase {};

TEST_P(ImportFunctions,
       PrototypeShouldBeImportedAsAPrototypeWhenThereIsNoDefinition) {
  Decl *FromTU = getTuDecl("void f();", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto FromD =
      FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  auto ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. without a body
  EXPECT_TRUE(!ToFD->doesThisDeclarationHaveABody());
}

TEST_P(ImportFunctions, PrototypeAfterPrototype) {
  Decl *FromTU = getTuDecl("void f(); void f();", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To0);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(!To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions, ImportOfPrototypeShouldBringInTheWholeChain) {
  Decl *FromTU = getTuDecl("void f(); void f() {}", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto FromD = // Prototype
      FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To0);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions, ImportOfDefinitionShouldBringInTheWholeChain) {
  Decl *FromTU = getTuDecl("void f(); void f() {}", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto FromD = // Definition
      LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To1);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions,
       DefinitionShouldBeImportedAsADefinition) {
  Decl *FromTU = getTuDecl("void f() {}", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto FromD =
      FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  auto ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. with a body
  EXPECT_TRUE(ToFD->doesThisDeclarationHaveABody());
}

TEST_P(ImportFunctions, ImportPrototypeOfRecursiveFunction) {
  Decl *FromTU = getTuDecl("void f(); void f() { f(); }", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto From =
      FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern); // Proto

  Decl *ImportedD = Import(From, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To0);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions, ImportDefinitionOfRecursiveFunction) {
  Decl *FromTU = getTuDecl("void f(); void f() { f(); }", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto From =
      LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern); // Def

  Decl *ImportedD = Import(From, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To1);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions,
       ImportPrototypes) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *ImportedD;
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input0.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

    ImportedD = Import(FromD, Lang_CXX);
  }
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input1.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To0);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(!To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions, ImportDefinitions) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *ImportedD;
  {
    Decl *FromTU = getTuDecl("void f(){}", Lang_CXX, "input0.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    ImportedD = Import(FromD, Lang_CXX);
  }
  {
    Decl *FromTU = getTuDecl("void f(){};", Lang_CXX, "input1.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 1u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To0);
  EXPECT_TRUE(To0->doesThisDeclarationHaveABody());
}

TEST_P(ImportFunctions,
       ImportDefinitionThenPrototype) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *ImportedD;
  {
    Decl *FromTU = getTuDecl("void f(){}", Lang_CXX, "input0.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    ImportedD = Import(FromD, Lang_CXX);
  }
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input1.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To0);
  EXPECT_TRUE(To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(!To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions,
       ImportPrototypeThenDefinition) {
  auto Pattern = functionDecl(hasName("f"));

  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input0.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }
  {
    Decl *FromTU = getTuDecl("void f(){}", Lang_CXX, "input1.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }
  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions, ImportPrototypeThenPrototype) {
  auto Pattern = functionDecl(hasName("f"));

  FunctionDecl *ImportedD = nullptr;
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input0.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    ImportedD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  }

  FunctionDecl *ImportedD1 = nullptr;
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input1.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    ImportedD1 = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();

  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == To0);
  EXPECT_TRUE(ImportedD1 == To1);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(!To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions,
       ImportPrototypeThenProtoAndDefinition) {
  auto Pattern = functionDecl(hasName("f"));

  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input0.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }
  {
    Decl *FromTU = getTuDecl("void f(); void f(){}", Lang_CXX, "input1.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();

  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 3u);
  FunctionDecl* ProtoD = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(!ProtoD->doesThisDeclarationHaveABody());

  FunctionDecl* DefinitionD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(DefinitionD->doesThisDeclarationHaveABody());

  EXPECT_TRUE(DefinitionD->getPreviousDecl());
  EXPECT_TRUE(!DefinitionD->getPreviousDecl()->doesThisDeclarationHaveABody());
  EXPECT_EQ(DefinitionD->getPreviousDecl()->getPreviousDecl(), ProtoD);
}

TEST_P(ImportFunctions, InClassProtoAndOutOfClassDef_ImportingProto) {
  auto Code =
      R"(
        struct B { void f(); };
        void B::f() {}
        )";
  auto Pattern = cxxMethodDecl(hasName("f"));
  Decl *FromTU = getTuDecl(Code, Lang_CXX);
  CXXMethodDecl *Proto =
      FirstDeclMatcher<CXXMethodDecl>().match(FromTU, Pattern);

  CXXMethodDecl *To = cast<CXXMethodDecl>(Import(Proto, Lang_CXX));

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  EXPECT_EQ(DeclCounter<CXXMethodDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<CXXMethodDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<CXXMethodDecl>().match(ToTU, Pattern);
  EXPECT_EQ(To, To0);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions, InClassProtoAndOutOfClassDef_ImportingDef) {
  auto Code =
      R"(
        struct B { void f(); };
        void B::f() {}
        )";
  auto Pattern = cxxMethodDecl(hasName("f"));
  Decl *FromTU = getTuDecl(Code, Lang_CXX);
  CXXMethodDecl *Def = LastDeclMatcher<CXXMethodDecl>().match(FromTU, Pattern);

  CXXMethodDecl *To = cast<CXXMethodDecl>(Import(Def, Lang_CXX));

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  EXPECT_EQ(DeclCounter<CXXMethodDecl>().match(ToTU, Pattern), 2u);
  auto To0 = FirstDeclMatcher<CXXMethodDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<CXXMethodDecl>().match(ToTU, Pattern);
  EXPECT_EQ(To, To1);
  EXPECT_TRUE(!To0->doesThisDeclarationHaveABody());
  EXPECT_TRUE(To1->doesThisDeclarationHaveABody());
  EXPECT_EQ(To1->getPreviousDecl(), To0);
}

TEST_P(ImportFunctions,
       OverriddenMethodsShouldBeImported) {
  auto Code =
        R"(
        struct B { virtual void f(); };
        void B::f() {}
        struct D : B { void f(); };
        )";
  auto Pattern = cxxMethodDecl(hasName("f"), hasParent(cxxRecordDecl(hasName("D"))));
  Decl *FromTU = getTuDecl(Code, Lang_CXX);
  CXXMethodDecl *Proto = FirstDeclMatcher<CXXMethodDecl>().match(FromTU, Pattern);
  ASSERT_EQ(Proto->size_overridden_methods(), 1u);

  CXXMethodDecl* To = cast<CXXMethodDecl>(Import(Proto, Lang_CXX));

  EXPECT_EQ(To->size_overridden_methods(), 1u);
}

TEST_P(ImportFunctions,
       VirtualFlagShouldBePreservedWhenImportingPrototype) {
  auto Code =
        R"(
        struct B { virtual void f(); };
        void B::f() {}
        )";
  auto Pattern = cxxMethodDecl(hasName("f"), hasParent(cxxRecordDecl(hasName("B"))));
  Decl *FromTU = getTuDecl(Code, Lang_CXX);
  CXXMethodDecl *Proto = FirstDeclMatcher<CXXMethodDecl>().match(FromTU, Pattern);
  CXXMethodDecl *Def = LastDeclMatcher<CXXMethodDecl>().match(FromTU, Pattern);
  ASSERT_TRUE(Proto->isVirtual());
  ASSERT_TRUE(Def->isVirtual());

  CXXMethodDecl* To = cast<CXXMethodDecl>(Import(Proto, Lang_CXX));

  EXPECT_TRUE(To->isVirtual());
}

TEST_P(ImportFunctions,
       VirtualFlagShouldBePreservedWhenImportingDefinition) {
  auto Code =
        R"(
        struct B { virtual void f(); };
        void B::f() {}
        )";
  auto Pattern = cxxMethodDecl(hasName("f"), hasParent(cxxRecordDecl(hasName("B"))));
  Decl *FromTU = getTuDecl(Code, Lang_CXX);
  CXXMethodDecl *Proto = FirstDeclMatcher<CXXMethodDecl>().match(FromTU, Pattern);
  CXXMethodDecl *Def = LastDeclMatcher<CXXMethodDecl>().match(FromTU, Pattern);
  ASSERT_TRUE(Proto->isVirtual());
  ASSERT_TRUE(Def->isVirtual());

  CXXMethodDecl* To = cast<CXXMethodDecl>(Import(Def, Lang_CXX));

  EXPECT_TRUE(To->isVirtual());
}


struct ImportFriendFunctions : ImportFunctions {};

TEST_P(ImportFriendFunctions, ImportFriendList) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU = getTuDecl("struct X { friend void f(); };"
                           "void f();",
                           Lang_CXX,
                           "input0.cc");
  auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  {
    auto *Class = FirstDeclMatcher<CXXRecordDecl>().match(FromTU, cxxRecordDecl());
    auto *Friend = FirstDeclMatcher<FriendDecl>().match(FromTU, friendDecl());
    auto Friends = Class->friends();
    unsigned int FrN = 0;
    for (auto Fr : Friends) {
      ASSERT_EQ(Fr, Friend);
      ++FrN;
    }
    ASSERT_EQ(FrN, 1u);
  }
  Import(FromD, Lang_CXX);
  auto *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  auto *Class = FirstDeclMatcher<CXXRecordDecl>().match(ToTU, cxxRecordDecl());
  auto *Friend = FirstDeclMatcher<FriendDecl>().match(ToTU, friendDecl());
  auto Friends = Class->friends();
  unsigned int FrN = 0;
  for (auto Fr : Friends) {
    EXPECT_EQ(Fr, Friend);
    ++FrN;
  }
  ASSERT_EQ(FrN, 1u);
}

TEST_P(ImportFriendFunctions, ImportFriendFunctionRedeclChainProto) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU = getTuDecl("struct X { friend void f(); };"
                           "void f();",
                           Lang_CXX,
                           "input0.cc");
  auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  auto *ImportedD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  EXPECT_TRUE(!ImportedD->doesThisDeclarationHaveABody());
  auto ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(!ToFD->doesThisDeclarationHaveABody());
  EXPECT_EQ(ToFD->getPreviousDecl(), ImportedD);
}

TEST_P(ImportFriendFunctions,
       ImportFriendFunctionRedeclChainProto_OutOfClassProtoFirst) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU = getTuDecl("void f();"
                           "struct X { friend void f(); };",
                           Lang_CXX, "input0.cc");
  auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  auto *ImportedD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  EXPECT_TRUE(!ImportedD->doesThisDeclarationHaveABody());
  auto ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(!ToFD->doesThisDeclarationHaveABody());
  EXPECT_EQ(ToFD->getPreviousDecl(), ImportedD);
}

TEST_P(ImportFriendFunctions, ImportFriendFunctionRedeclChainDef) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU = getTuDecl("struct X { friend void f(){} };"
                           "void f();",
                           Lang_CXX,
                           "input0.cc");
  auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  auto *ImportedD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  EXPECT_TRUE(ImportedD->doesThisDeclarationHaveABody());
  auto ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(!ToFD->doesThisDeclarationHaveABody());
  EXPECT_EQ(ToFD->getPreviousDecl(), ImportedD);
}

TEST_P(ImportFriendFunctions,
       ImportFriendFunctionRedeclChainDef_OutOfClassDef) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU = getTuDecl("struct X { friend void f(); };"
                           "void f(){}",
                           Lang_CXX, "input0.cc");
  auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  auto *ImportedD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  EXPECT_TRUE(!ImportedD->doesThisDeclarationHaveABody());
  auto ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ToFD->doesThisDeclarationHaveABody());
  EXPECT_EQ(ToFD->getPreviousDecl(), ImportedD);
}

TEST_P(ImportFriendFunctions, ImportFriendFunctionRedeclChainDefWithClass) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU = getTuDecl(
      R"(
        class X;
        void f(X *x){}
        class X{
        friend void f(X *x);
        };
      )",
      Lang_CXX, "input0.cc");
  auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  auto *ImportedD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  EXPECT_TRUE(ImportedD->doesThisDeclarationHaveABody());
  auto *InClassFD = cast<FunctionDecl>(FirstDeclMatcher<FriendDecl>()
                                              .match(ToTU, friendDecl())
                                              ->getFriendDecl());
  EXPECT_TRUE(!InClassFD->doesThisDeclarationHaveABody());
  EXPECT_EQ(InClassFD->getPreviousDecl(), ImportedD);
  // The parameters must refer the same type
  EXPECT_EQ((*InClassFD->param_begin())->getOriginalType(),
            (*ImportedD->param_begin())->getOriginalType());
}

TEST_P(ImportFriendFunctions,
       ImportFriendFunctionRedeclChainDefWithClass_ImportTheProto) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU = getTuDecl(
      R"(
        class X;
        void f(X *x){}
        class X{
        friend void f(X *x);
        };
      )",
      Lang_CXX, "input0.cc");
  auto FromD = LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  auto *ImportedD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  EXPECT_TRUE(!ImportedD->doesThisDeclarationHaveABody());
  auto *OutOfClassFD = FirstDeclMatcher<FunctionDecl>().match(
      ToTU, functionDecl(unless(hasParent(friendDecl()))));

  EXPECT_TRUE(OutOfClassFD->doesThisDeclarationHaveABody());
  EXPECT_EQ(ImportedD->getPreviousDecl(), OutOfClassFD);
  // The parameters must refer the same type
  EXPECT_EQ((*OutOfClassFD->param_begin())->getOriginalType(),
            (*ImportedD->param_begin())->getOriginalType());
}

TEST_P(ImportFriendFunctions, ImportFriendFunctionFromMultipleTU) {
  auto Pattern = functionDecl(hasName("f"));

  FunctionDecl *ImportedD;
  {
    Decl *FromTU =
        getTuDecl("struct X { friend void f(){} };", Lang_CXX, "input0.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    ImportedD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  }
  FunctionDecl *ImportedD1;
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input1.cc");
    auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    ImportedD1 = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  EXPECT_TRUE(ImportedD->doesThisDeclarationHaveABody());
  EXPECT_TRUE(!ImportedD1->doesThisDeclarationHaveABody());
  EXPECT_EQ(ImportedD1->getPreviousDecl(), ImportedD);
}

TEST_P(ImportFriendFunctions, Lookup) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU =
      getTuDecl("struct X { friend void f(); };", Lang_CXX, "input0.cc");
  auto FromD = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  ASSERT_TRUE(FromD->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  ASSERT_TRUE(!FromD->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  auto FromName = FromD->getDeclName();
  {
    CXXRecordDecl *Class =
        FirstDeclMatcher<CXXRecordDecl>().match(FromTU, cxxRecordDecl());
    auto lookup_res = Class->noload_lookup(FromName);
    ASSERT_EQ(lookup_res.size(), 0u);
    lookup_res = cast<TranslationUnitDecl>(FromTU)->noload_lookup(FromName);
    ASSERT_EQ(lookup_res.size(), 1u);
  }

  auto ToD = cast<FunctionDecl>(Import(FromD, Lang_CXX));
  auto ToName = ToD->getDeclName();

  {
    Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
    CXXRecordDecl *Class =
        FirstDeclMatcher<CXXRecordDecl>().match(ToTU, cxxRecordDecl());
    auto lookup_res = Class->noload_lookup(ToName);
    EXPECT_EQ(lookup_res.size(), 0u);
    lookup_res = cast<TranslationUnitDecl>(ToTU)->noload_lookup(ToName);
    EXPECT_EQ(lookup_res.size(), 1u);
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 1u);
  auto To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(To0->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  EXPECT_TRUE(!To0->isInIdentifierNamespace(Decl::IDNS_Ordinary));
}

TEST_P(ImportFriendFunctions, DISABLED_LookupWithProto) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU =
      getTuDecl("struct X { friend void f(); };"
                // This proto decl makes f available to normal
                // lookup, otherwise it is hidden.
                // Normal C++ lookup (implemented in
                // `clang::Sema::CppLookupName()` and in `LookupDirect()`)
                // returns the found `NamedDecl` only if the set IDNS is matched
                "void f();",
                Lang_CXX, "input0.cc");
  auto From0 = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  auto From1 = LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  ASSERT_TRUE(From0->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  ASSERT_TRUE(!From0->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  ASSERT_TRUE(!From1->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  ASSERT_TRUE(From1->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  auto FromName = From0->getDeclName();
  {
    CXXRecordDecl *Class =
        FirstDeclMatcher<CXXRecordDecl>().match(FromTU, cxxRecordDecl());
    auto lookup_res = Class->noload_lookup(FromName);
    ASSERT_EQ(lookup_res.size(), 0u);
    lookup_res = cast<TranslationUnitDecl>(FromTU)->noload_lookup(FromName);
    ASSERT_EQ(lookup_res.size(), 1u);
  }

  auto To0 = cast<FunctionDecl>(Import(From0, Lang_CXX));
  auto ToName = To0->getDeclName();

  {
    auto ToTU = ToAST->getASTContext().getTranslationUnitDecl();
    CXXRecordDecl *Class =
        FirstDeclMatcher<CXXRecordDecl>().match(ToTU, cxxRecordDecl());
    auto lookup_res = Class->noload_lookup(ToName);
    EXPECT_EQ(lookup_res.size(), 0u);
    lookup_res = ToTU->noload_lookup(ToName);
    EXPECT_EQ(lookup_res.size(), 1u);
  }

  auto ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(To0->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  EXPECT_TRUE(!To0->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  EXPECT_TRUE(!To1->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  EXPECT_TRUE(To1->isInIdentifierNamespace(Decl::IDNS_Ordinary));
}

TEST_P(ImportFriendFunctions, LookupWithProtoFirst) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU =
      getTuDecl("void f();"
                "struct X { friend void f(); };",
                Lang_CXX, "input0.cc");
  auto From0 = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  auto From1 = LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  ASSERT_TRUE(!From0->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  ASSERT_TRUE(From0->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  ASSERT_TRUE(From1->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  ASSERT_TRUE(From1->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  auto FromName = From0->getDeclName();
  {
    CXXRecordDecl *Class =
        FirstDeclMatcher<CXXRecordDecl>().match(FromTU, cxxRecordDecl());
    auto lookup_res = Class->noload_lookup(FromName);
    ASSERT_EQ(lookup_res.size(), 0u);
    lookup_res = cast<TranslationUnitDecl>(FromTU)->noload_lookup(FromName);
    ASSERT_EQ(lookup_res.size(), 1u);
  }

  auto To0 = cast<FunctionDecl>(Import(From0, Lang_CXX));
  auto ToName = To0->getDeclName();
  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();

  {
    CXXRecordDecl *Class =
        FirstDeclMatcher<CXXRecordDecl>().match(ToTU, cxxRecordDecl());
    auto lookup_res = Class->noload_lookup(ToName);
    EXPECT_EQ(lookup_res.size(), 0u);
    lookup_res = cast<TranslationUnitDecl>(ToTU)->noload_lookup(ToName);
    EXPECT_EQ(lookup_res.size(), 1u);
  }

  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  To0 = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  auto To1 = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(!To0->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  EXPECT_TRUE(To0->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  EXPECT_TRUE(To1->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  EXPECT_TRUE(To1->isInIdentifierNamespace(Decl::IDNS_Ordinary));
}

TEST_P(ImportFriendFunctions, ImportFriendChangesLookup) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *FromTU0 = getTuDecl("void f();", Lang_CXX, "input0.cc");
  FunctionDecl *FromD0 =
      FirstDeclMatcher<FunctionDecl>().match(FromTU0, Pattern);
  Decl *FromTU1 =
      getTuDecl("class X { friend void f(); };", Lang_CXX, "input1.cc");
  FunctionDecl *FromD1 =
      FirstDeclMatcher<FunctionDecl>().match(FromTU1, Pattern);
  auto FromName0 = FromD0->getDeclName();
  auto FromName1 = FromD1->getDeclName();

  ASSERT_TRUE(FromD0->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  ASSERT_TRUE(!FromD0->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  ASSERT_TRUE(!FromD1->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  ASSERT_TRUE(FromD1->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  auto lookup_res = cast<TranslationUnitDecl>(FromTU0)->noload_lookup(FromName0);
  ASSERT_EQ(lookup_res.size(), 1u);
  lookup_res = cast<TranslationUnitDecl>(FromTU1)->noload_lookup(FromName1);
  ASSERT_EQ(lookup_res.size(), 1u);

  FunctionDecl *ToD0 = cast<FunctionDecl>(Import(FromD0, Lang_CXX));
  auto ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  auto ToName = ToD0->getDeclName();
  EXPECT_TRUE(ToD0->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  EXPECT_TRUE(!ToD0->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  lookup_res = cast<TranslationUnitDecl>(ToTU)->noload_lookup(ToName);
  EXPECT_EQ(lookup_res.size(), 1u);
  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 1u);
  
  FunctionDecl *ToD1 = cast<FunctionDecl>(Import(FromD1, Lang_CXX));
  lookup_res = cast<TranslationUnitDecl>(ToTU)->noload_lookup(ToName);
  EXPECT_EQ(lookup_res.size(), 1u);
  EXPECT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);

  EXPECT_TRUE(ToD0->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  EXPECT_TRUE(!ToD0->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
  
  EXPECT_TRUE(ToD1->isInIdentifierNamespace(Decl::IDNS_Ordinary));
  EXPECT_TRUE(ToD1->isInIdentifierNamespace(Decl::IDNS_OrdinaryFriend));
}

TEST_P(ASTImporterTestBase, OmitVAListTag) {
  Decl *From, *To;
  std::tie(From, To) =
      getImportedDecl("void declToImport(int n, ...) {"
                      "  __builtin_va_list __args;"
                      "  __builtin_va_start(__args, n);"
                      "}",
                      Lang_C, "", Lang_C);
  auto Pattern = translationUnitDecl(has(recordDecl(hasName("__va_list_tag"))));
  EXPECT_FALSE(
      MatchVerifier<Decl>{}.match(To->getTranslationUnitDecl(), Pattern));
}

TEST_P(ASTImporterTestBase, ProperPrevDeclForClassTemplateDecls) {
  auto Pattern = classTemplateSpecializationDecl(hasName("X"));

  ClassTemplateSpecializationDecl *Imported1;
  {
    Decl *FromTU = getTuDecl("template<class T> class X;"
                             "struct Y { friend class X<int>; };",
                             Lang_CXX, "input0.cc");
    auto *FromD = FirstDeclMatcher<ClassTemplateSpecializationDecl>().match(
        FromTU, Pattern);

    Imported1 = cast<ClassTemplateSpecializationDecl>(Import(FromD, Lang_CXX));
  }
  ClassTemplateSpecializationDecl *Imported2;
  {
    Decl *FromTU = getTuDecl("template<class T> class X;"
                             "template<> class X<int>{};"
                             "struct Z { friend class X<int>; };",
                             Lang_CXX, "input1.cc");
    auto *FromD = FirstDeclMatcher<ClassTemplateSpecializationDecl>().match(
        FromTU, Pattern);

    Imported2 = cast<ClassTemplateSpecializationDecl>(Import(FromD, Lang_CXX));
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  // FIXME: Check if this should actually be 2.
  EXPECT_EQ(DeclCounter<ClassTemplateSpecializationDecl>().match(ToTU, Pattern),
            3u);
  ASSERT_TRUE(Imported2->getPreviousDecl());
  EXPECT_EQ(Imported2->getPreviousDecl(), Imported1);
}

TEST_P(ASTImporterTestBase, TypeForDeclShouldBeSet) {
  auto Pattern = cxxRecordDecl(hasName("X"));

  CXXRecordDecl *Imported1;
  {
    Decl *FromTU = getTuDecl("class X;", Lang_CXX, "input0.cc");
    auto *FromD = FirstDeclMatcher<CXXRecordDecl>().match(FromTU, Pattern);

    Imported1 = cast<CXXRecordDecl>(Import(FromD, Lang_CXX));
  }
  CXXRecordDecl *Imported2;
  {
    Decl *FromTU = getTuDecl("class X {};", Lang_CXX, "input1.cc");
    auto *FromD = FirstDeclMatcher<CXXRecordDecl>().match(FromTU, Pattern);

    Imported2 = cast<CXXRecordDecl>(Import(FromD, Lang_CXX));
  }

  EXPECT_TRUE(Imported2->getPreviousDecl());
  EXPECT_EQ(Imported1->getTypeForDecl(), Imported2->getTypeForDecl());
}

TEST_P(ASTImporterTestBase, DeclsFromFriendsShouldBeInRedeclChains2) {
  Decl *From, *To;
  std::tie(From, To) =
      getImportedDecl("class declToImport {};", Lang_CXX,
                      "class Y { friend class declToImport; };", Lang_CXX);
  auto *Imported = cast<CXXRecordDecl>(To);

  EXPECT_TRUE(Imported->getPreviousDecl());
}

struct CanonicalRedeclChain : ASTImporterTestBase {};

TEST_P(CanonicalRedeclChain, ShouldBeConsequentWithMatchers) {
  Decl *FromTU = getTuDecl("void f();", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto D0 = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  auto Redecls = getCanonicalForwardRedeclChain(D0);
  ASSERT_EQ(Redecls.size(), 1u);
  EXPECT_EQ(D0, Redecls[0]);
}

TEST_P(CanonicalRedeclChain, ShouldBeConsequentWithMatchers2) {
  Decl *FromTU = getTuDecl("void f(); void f(); void f();", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto D0 = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  auto D2 = LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  FunctionDecl *D1 = D2->getPreviousDecl();

  auto Redecls = getCanonicalForwardRedeclChain(D0);
  ASSERT_EQ(Redecls.size(), 3u);
  EXPECT_EQ(D0, Redecls[0]);
  EXPECT_EQ(D1, Redecls[1]);
  EXPECT_EQ(D2, Redecls[2]);
}

TEST_P(CanonicalRedeclChain, ShouldBeSameForAllDeclInTheChain) {
  Decl *FromTU = getTuDecl("void f(); void f(); void f();", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  auto D0 = FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  auto D2 = LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
  FunctionDecl *D1 = D2->getPreviousDecl();

  auto RedeclsD0 = getCanonicalForwardRedeclChain(D0);
  auto RedeclsD1 = getCanonicalForwardRedeclChain(D1);
  auto RedeclsD2 = getCanonicalForwardRedeclChain(D2);

  EXPECT_THAT(RedeclsD0, ::testing::ContainerEq(RedeclsD1));
  EXPECT_THAT(RedeclsD1, ::testing::ContainerEq(RedeclsD2));
}

TEST_P(ASTImporterTestBase, ImportDoesUpdateUsedFlag) {
  auto Pattern = varDecl(hasName("x"));
  VarDecl *Imported1;
  {
    Decl *FromTU = getTuDecl("extern int x;", Lang_CXX, "input0.cc");
    auto *FromD = FirstDeclMatcher<VarDecl>().match(FromTU, Pattern);
    Imported1 = cast<VarDecl>(Import(FromD, Lang_CXX));
  }
  VarDecl *Imported2;
  {
    Decl *FromTU = getTuDecl("int x;", Lang_CXX, "input1.cc");
    auto *FromD = FirstDeclMatcher<VarDecl>().match(FromTU, Pattern);
    Imported2 = cast<VarDecl>(Import(FromD, Lang_CXX));
  }
  EXPECT_EQ(Imported1->getCanonicalDecl(), Imported2->getCanonicalDecl());
  EXPECT_FALSE(Imported2->isUsed(false));
  {
    Decl *FromTU = getTuDecl(
        "extern int x; int f() { return x; }", Lang_CXX, "input2.cc");
    auto *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, functionDecl());
    Import(FromD, Lang_CXX);
  }
  EXPECT_TRUE(Imported2->isUsed(false));
}

TEST_P(ASTImporterTestBase, ReimportWithUsedFlag) {
  auto Pattern = varDecl(hasName("x"));
  
  Decl *FromTU = getTuDecl("int x;", Lang_CXX, "input0.cc");
  auto *FromD = FirstDeclMatcher<VarDecl>().match(FromTU, Pattern);
  
  auto Imported1 = cast<VarDecl>(Import(FromD, Lang_CXX));
  
  ASSERT_FALSE(Imported1->isUsed(false));

  FromD->setIsUsed();
  auto Imported2 = cast<VarDecl>(Import(FromD, Lang_CXX));
  
  EXPECT_EQ(Imported1, Imported2);
  EXPECT_TRUE(Imported2->isUsed(false));
}

// Note, this test case is automatically reduced from Xerces code.
TEST_P(ASTImporterTestBase, UsingShadowDeclShouldImportTheDeclOnlyOnce) {
  auto Pattern = cxxRecordDecl(hasName("B"));

  {
    Decl *FromTU =
        getTuDecl(
            R"(
namespace xercesc_3_2 {
class MemoryManager;
class A {
public:
  static MemoryManager *fgMemoryManager;
};
class XMLString {
public:
  static int *transcode(const char *const, MemoryManager *const);
};
class B {
  B(char *p1) : fMsg(XMLString::transcode(p1, A::fgMemoryManager)) {}
  int *fMsg;
};
}
            )"
            , Lang_CXX, "input0.cc");
    CXXRecordDecl *FromD =
        FirstDeclMatcher<CXXRecordDecl>().match(FromTU, Pattern);

    Import(FromD, Lang_CXX);
  }

  {
    Decl *FromTU = getTuDecl(
            R"(
int strtol(char **);
using ::strtol;
namespace xercesc_3_2 {
class MemoryManager;
class XMLString {
  static int *transcode(const char *, MemoryManager *);
};
int *XMLString::transcode(const char *const, MemoryManager *const) { return 0; }
char *a;
long b = strtol(&a);
}
            )"
        , Lang_CXX, "input1.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, functionDecl(hasName("transcode")));
    Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  EXPECT_EQ(DeclCounter<UsingShadowDecl>().match(ToTU, usingShadowDecl()), 1u);
}

TEST_P(ASTImporterTestBase, ImportDefinitionOfClassTemplateAfterFwdDecl) {
  {
    Decl *FromTU = getTuDecl(
        R"(
            template <typename T>
            struct B;
            )",
        Lang_CXX, "input0.cc");
    auto *FromD = FirstDeclMatcher<ClassTemplateDecl>().match(
        FromTU, classTemplateDecl(hasName("B")));

    Import(FromD, Lang_CXX);
  }

  {
    Decl *FromTU = getTuDecl(
        R"(
            template <typename T>
            struct B {
              void f();
            };
            )",
        Lang_CXX, "input1.cc");
    FunctionDecl *FromD = FirstDeclMatcher<FunctionDecl>().match(
        FromTU, functionDecl(hasName("f")));
    Import(FromD, Lang_CXX);
    auto *FromCTD = FirstDeclMatcher<ClassTemplateDecl>().match(
        FromTU, classTemplateDecl(hasName("B")));
    auto *ToCTD = cast<ClassTemplateDecl>(Import(FromCTD, Lang_CXX));
    EXPECT_TRUE(ToCTD->isThisDeclarationADefinition());
  }
}

INSTANTIATE_TEST_CASE_P(
    ParameterizedTests, ASTImporterTestBase,
    ::testing::Values(ArgVector(), ArgVector{"-fdelayed-template-parsing"}),);

INSTANTIATE_TEST_CASE_P(
    ParameterizedTests, ImportFunctions,
    ::testing::Values(ArgVector(), ArgVector{"-fdelayed-template-parsing"}),);

INSTANTIATE_TEST_CASE_P(
    ParameterizedTests, CanonicalRedeclChain,
    ::testing::Values(ArgVector()),);

} // end namespace ast_matchers
} // end namespace clang
