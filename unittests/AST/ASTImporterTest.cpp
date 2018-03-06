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
#include "gtest/gtest.h"

namespace clang {
namespace ast_matchers {

template<typename NodeType, typename MatcherType>
testing::AssertionResult
testImport(const std::string &FromCode, Language FromLang,
           const std::string &ToCode, Language ToLang,
           MatchVerifier<NodeType> &Verifier,
           const MatcherType &AMatcher) {
  StringVector FromArgs, ToArgs;
  getLangArgs(FromLang, FromArgs);
  getLangArgs(ToLang, ToArgs);

  const char *const InputFileName = "input.cc";
  const char *const OutputFileName = "output.cc";

  std::unique_ptr<ASTUnit>
      FromAST = tooling::buildASTFromCodeWithArgs(
        FromCode, FromArgs, InputFileName),
      ToAST = tooling::buildASTFromCodeWithArgs(ToCode, ToArgs, OutputFileName);

  ASTContext &FromCtx = FromAST->getASTContext(),
      &ToCtx = ToAST->getASTContext();

  // Add input.cc to virtual file system so importer can 'find' it
  // while importing SourceLocations.
  vfs::OverlayFileSystem *OFS = static_cast<vfs::OverlayFileSystem *>(
        ToCtx.getSourceManager().getFileManager().getVirtualFileSystem().get());
  vfs::InMemoryFileSystem *MFS = static_cast<vfs::InMemoryFileSystem *>(
        OFS->overlays_begin()->get());
  MFS->addFile(InputFileName, 0, llvm::MemoryBuffer::getMemBuffer(FromCode));

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
  // were not imported
  SmallString<1024> ImportChecker;
  llvm::raw_svector_ostream ToNothing(ImportChecker);
  ToCtx.getTranslationUnitDecl()->print(ToNothing);

  // This traverses the AST to catch certain bugs like poorly or not
  // implemented subtrees.
  Imported->dump(ToNothing);

  return Verifier.match(Imported, AMatcher);
}

class Fixture : public ::testing::Test {

  const char *const InputFileName = "input.cc";
  const char *const OutputFileName = "output.cc";

  //Buffers for the contexts, must live in test scope
  std::string ToCode;

  struct TU {
    std::string Code;
    std::string FileName;
    std::unique_ptr<ASTUnit> Unit;
    TranslationUnitDecl *TUDecl = nullptr;
    TU(StringRef Code, StringRef FileName, StringVector Args)
        : Code(Code), FileName(FileName),
          Unit(tooling::buildASTFromCodeWithArgs(this->Code, Args,
                                                 this->FileName)),
          TUDecl(Unit->getASTContext().getTranslationUnitDecl()) {}
  };
  // We may have several From context and related translation units.
  std::list<TU> FromTUs;

  // Creates a virtual file and assigns that to the To context.  If the file
  // already exists then the file will not be created again as a duplicate.
  void createVirtualFile(StringRef FileName, const std::string &Code) {
    assert(ToAST);
    ASTContext &ToCtx = ToAST->getASTContext();
    vfs::OverlayFileSystem *OFS = static_cast<vfs::OverlayFileSystem *>(
        ToCtx.getSourceManager().getFileManager().getVirtualFileSystem().get());
    vfs::InMemoryFileSystem *MFS =
        static_cast<vfs::InMemoryFileSystem *>(OFS->overlays_begin()->get());
    MFS->addFile(FileName, 0, llvm::MemoryBuffer::getMemBuffer(Code.c_str()));
  }

public:
  // We may have several From context but only one To context.
  std::unique_ptr<ASTUnit> ToAST;

  // Creates an AST both for the From and To source code and imports the Decl
  // of the identifier into the To context.
  // Must not call more than once within the same test.
  std::tuple<Decl *, Decl *>
  getImportedDecl(StringRef FromSrcCode, Language FromLang,
                  StringRef ToSrcCode, Language ToLang,
                  const char *const Identifier = "declToImport") {
    StringVector FromArgs, ToArgs;
    getLangArgs(FromLang, FromArgs);
    getLangArgs(ToLang, ToArgs);

    FromTUs.emplace_back(FromSrcCode, InputFileName, FromArgs);
    TU &FromTu = FromTUs.back();

    ToCode = ToSrcCode;
    assert(!ToAST);
    ToAST = tooling::buildASTFromCodeWithArgs(ToCode, ToArgs, OutputFileName);

    ASTContext &FromCtx = FromTu.Unit->getASTContext(),
               &ToCtx = ToAST->getASTContext();

    createVirtualFile(InputFileName, FromTu.Code);

    ASTImporter Importer(ToCtx, ToAST->getFileManager(), FromCtx,
                         FromTu.Unit->getFileManager(), false);

    IdentifierInfo *ImportedII = &FromCtx.Idents.get(Identifier);
    assert(ImportedII && "Declaration with the given identifier "
                         "should be specified in test!");
    DeclarationName ImportDeclName(ImportedII);
    SmallVector<NamedDecl *, 4> FoundDecls;
    FromCtx.getTranslationUnitDecl()->localUncachedLookup(ImportDeclName,
                                                          FoundDecls);

    assert(FoundDecls.size() == 1);

    Decl *Imported = Importer.Import(*FoundDecls.begin());
    assert(Imported);
    return std::make_tuple(*FoundDecls.begin(), Imported);
  }

  // Creates a TU decl for the given source code.
  // May be called several times in a given test.
  TranslationUnitDecl *getTuDecl(StringRef SrcCode, Language Lang,
                                 StringRef FileName = "input.cc") {
    assert(
        std::find_if(FromTUs.begin(), FromTUs.end(), [&FileName](const TU &e) {
          return e.FileName == FileName;
        }) == FromTUs.end());

    StringVector Args;
    getLangArgs(Lang, Args);
    FromTUs.emplace_back(SrcCode, FileName, Args);
    TU &Tu = FromTUs.back();

    return Tu.TUDecl;
  }

  // Import the given Decl into the ToCtx.
  // May be called several times in a given test.
  // The different instances of the param From may have different ASTContext.
  Decl *Import(Decl *From, Language ToLang) {
    if (!ToAST) {
      StringVector ToArgs;
      getLangArgs(ToLang, ToArgs);
      // Build the AST from an empty file.
      ToAST =
          tooling::buildASTFromCodeWithArgs(/*Code=*/"", ToArgs, "empty.cc");
    }

    // Create a virtual file in the To Ctx which corresponds to the file from
    // which we want to import the `From` Decl. Without this source locations
    // will be invalid in the ToCtx.
    auto It =
        std::find_if(FromTUs.begin(), FromTUs.end(), [&From](const TU &e) {
          return e.TUDecl == From->getTranslationUnitDecl();
        });
    assert(It != FromTUs.end());
    // This will not create the file more than once.
    createVirtualFile(It->FileName, It->Code);

    ASTContext &FromCtx = From->getASTContext(),
               &ToCtx = ToAST->getASTContext();
    ASTImporter Importer(ToCtx, ToAST->getFileManager(), FromCtx,
                         FromCtx.getSourceManager().getFileManager(), false);
    return Importer.Import(From);
  }

  ~Fixture() {
    if (!::testing::Test::HasFailure()) return;

    for (auto &Tu : FromTUs) {
      if (Tu.Unit) {
        llvm::errs() << "FromAST:\n";
        Tu.Unit->getASTContext().getTranslationUnitDecl()->dump();
        llvm::errs() << "\n";
      }
    }
    if (ToAST) {
      llvm::errs() << "ToAST:\n";
      ToAST->getASTContext().getTranslationUnitDecl()->dump();
    }
  }
};

TEST(ImportExpr, ImportStringLiteral) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("void declToImport() { \"foo\"; }",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 stringLiteral(
                                   hasType(
                                     asString("const char [4]")))))))));
  EXPECT_TRUE(testImport("void declToImport() { L\"foo\"; }",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 stringLiteral(
                                   hasType(
                                     asString("const wchar_t [4]")))))))));
  EXPECT_TRUE(testImport("void declToImport() { \"foo\" \"bar\"; }",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 stringLiteral(
                                   hasType(
                                     asString("const char [7]")))))))));
}

TEST(ImportExpr, ImportGNUNullExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("void declToImport() { __null; }",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 gnuNullExpr(
                                   hasType(isInteger()))))))));
}

TEST(ImportExpr, ImportCXXNullPtrLiteralExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("void declToImport() { nullptr; }",
                         Lang_CXX11, "", Lang_CXX11, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 cxxNullPtrLiteralExpr()))))));
}


TEST(ImportExpr, ImportFloatinglLiteralExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("void declToImport() { 1.0; }",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 floatLiteral(
                                   equals(1.0),
                                   hasType(asString("double")))))))));
  EXPECT_TRUE(testImport("void declToImport() { 1.0e-5f; }",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 floatLiteral(
                                   equals(1.0e-5f),
                                   hasType(asString("float")))))))));
}

TEST(ImportExpr, ImportCompoundLiteralExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "void declToImport() {"
          "  struct s { int x; long y; unsigned z; }; "
          "  (struct s){ 42, 0L, 1U }; }",
          Lang_CXX, "", Lang_CXX, Verifier,
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
                      )))))))));
}

TEST(ImportExpr, ImportCXXThisExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport("class declToImport { void f() { this; } };",
                   Lang_CXX, "", Lang_CXX, Verifier,
                   cxxRecordDecl(
                     hasMethod(
                       hasBody(
                         compoundStmt(
                           has(
                             cxxThisExpr(
                               hasType(
                                 asString("class declToImport *"))))))))));
}

TEST(ImportExpr, ImportAtomicExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport(
      "void declToImport() { int *ptr; __atomic_load_n(ptr, 1); }", Lang_CXX,
      "", Lang_CXX, Verifier,
      functionDecl(hasBody(compoundStmt(has(atomicExpr(
          has(ignoringParenImpCasts(
              declRefExpr(hasDeclaration(varDecl(hasName("ptr"))),
                          hasType(asString("int *"))))),
          has(integerLiteral(equals(1), hasType(asString("int")))))))))));
}

TEST(ImportExpr, ImportLabelDeclAndAddrLabelExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "void declToImport() { loop: goto loop; &&loop; }",
          Lang_CXX, "", Lang_CXX, Verifier,
          functionDecl(
            hasBody(
              compoundStmt(
                has(labelStmt(hasDeclaration(labelDecl(hasName("loop"))))),
                has(addrLabelExpr(hasDeclaration(labelDecl(hasName("loop")))))
                )))));
}

AST_MATCHER_P(TemplateDecl, hasTemplateDecl,
              internal::Matcher<NamedDecl>, InnerMatcher) {
  const NamedDecl *Template = Node.getTemplatedDecl();
  return Template && InnerMatcher.matches(*Template, Finder, Builder);
}

// FIXME enable, handle SIGSEGV in clang::Decl::getKind(this=0x0)
TEST(ImportExpr, DISABLED_ImportParenListExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "template<typename T> class dummy { void f() { dummy X(*this); } };"
          "typedef dummy<int> declToImport;"
          "template class dummy<int>;",
          Lang_CXX, "", Lang_CXX, Verifier,
          typedefDecl(
            hasType(
              templateSpecializationType(
                hasDeclaration(
                  classTemplateDecl(
                    hasTemplateDecl(
                      cxxRecordDecl(
                        hasMethod(
                        allOf(
                          hasName("f"),
                          hasBody(
                            compoundStmt(
                              has(
                                declStmt(
                                  hasSingleDecl(
                                    varDecl(
                                      hasInitializer(
                                        parenListExpr(
                                          has(
                                            unaryOperator(
                                              hasOperatorName("*"),
                                              hasUnaryOperand(cxxThisExpr())
                                              )))))))))))))))))))));
}

TEST(ImportExpr, ImportStmtExpr) {
  MatchVerifier<Decl> Verifier;
  // NOTE: has() ignores implicit casts, using hasDescendant() to match it
  EXPECT_TRUE(
        testImport(
          "void declToImport() { int b; int a = b ?: 1; int C = ({int X=4; X;}); }",
          Lang_CXX, "", Lang_CXX, Verifier,
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
                              ))))))))))));
}

TEST(ImportExpr, ImportConditionalOperator) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "void declToImport() { true ? 1 : -5; }",
          Lang_CXX, "", Lang_CXX, Verifier,
          functionDecl(
            hasBody(
              compoundStmt(
                has(
                  conditionalOperator(
                    hasCondition(cxxBoolLiteral(equals(true))),
                    hasTrueExpression(integerLiteral(equals(1))),
                    hasFalseExpression(
                      unaryOperator(hasUnaryOperand(integerLiteral(equals(5))))
                      ))))))));
}

TEST(ImportExpr, ImportBinaryConditionalOperator) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "void declToImport() { 1 ?: -5; }",
          Lang_CXX, "", Lang_CXX, Verifier,
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
                      )))))));
}

TEST(ImportExpr, ImportDesignatedInitExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("void declToImport() {"
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
                                               )))))))))))));
}


TEST(ImportExpr, ImportPredefinedExpr) {
  MatchVerifier<Decl> Verifier;
  // __func__ expands as StringLiteral("declToImport")
  EXPECT_TRUE(testImport("void declToImport() { __func__; }",
                         Lang_CXX, "", Lang_CXX, Verifier,
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
                                         asString("const char [13]")))))))))));
}

TEST(ImportExpr, ImportInitListExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
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
                              )))))))))));
}


const internal::VariadicDynCastAllOfMatcher<Expr, VAArgExpr> vaArgExpr;

TEST(ImportExpr, ImportVAArgExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "void declToImport(__builtin_va_list list, ...) {"
          "  (void)__builtin_va_arg(list, int); }",
          Lang_CXX, "", Lang_CXX, Verifier,
          functionDecl(
            hasBody(
              compoundStmt(
                has(
                  cStyleCastExpr(
                    hasSourceExpression(
                      vaArgExpr()))))))));
}

TEST(ImportExpr, CXXTemporaryObjectExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport(
      "struct C {};"
      "void declToImport() { C c = C(); }",
      Lang_CXX, "", Lang_CXX, Verifier,
      functionDecl(hasBody(compoundStmt(
          has(declStmt(has(varDecl(has(exprWithCleanups(has(cxxConstructExpr(
              has(materializeTemporaryExpr(has(implicitCastExpr(
                  has(cxxTemporaryObjectExpr()))))))))))))))))));
}

TEST(ImportType, ImportAtomicType) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("void declToImport() { typedef _Atomic(int) a_int; }",
                         Lang_CXX11, "", Lang_CXX11, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 declStmt(
                                   has(
                                     typedefDecl(
                                       has(atomicType()))))))))));
}

const internal::VariadicDynCastAllOfMatcher<Expr, CXXDependentScopeMemberExpr>
    cxxDependentScopeMemberExpr;

TEST(ImportExpr, ImportCXXDependentScopeMemberExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template <typename T> struct C { T t; };"
             "template <typename T> void declToImport() {"
             "  C<T> d;"
             "  d.t;"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(has(functionDecl(
                 has(compoundStmt(has(cxxDependentScopeMemberExpr()))))))));
  EXPECT_TRUE(testImport("template <typename T> struct C { T t; };"
             "template <typename T> void declToImport() {"
             "  C<T> d;"
             "  (&d)->t;"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(has(functionDecl(
                 has(compoundStmt(has(cxxDependentScopeMemberExpr()))))))));
}

TEST(ImportType, ImportTypeAliasTemplate) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template <int K>"
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
                                         declRefExpr()))))))))));
}

const internal::VariadicDynCastAllOfMatcher<Decl, VarTemplateSpecializationDecl>
    varTemplateSpecializationDecl;

TEST(ImportDecl, ImportVarTemplate) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport(
      "template <typename T>"
      "T pi = T(3.1415926535897932385L);"
      "void declToImport() { pi<int>; }",
      Lang_CXX11, "", Lang_CXX11, Verifier,
      functionDecl(
          hasBody(has(declRefExpr(to(varTemplateSpecializationDecl())))),
          unless(hasAncestor(translationUnitDecl(has(varDecl(
              hasName("pi"), unless(varTemplateSpecializationDecl())))))))));
}

TEST(ImportType, ImportPackExpansion) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template <typename... Args>"
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
                                         declRefExpr()))))))))));
}

const internal::VariadicDynCastAllOfMatcher<Type,
                                            DependentTemplateSpecializationType>
    dependentTemplateSpecializationType;

TEST(ImportType, ImportDependentTemplateSpecialization) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport(
      "template<typename T>"
      "struct A;"
      "template<typename T>"
      "struct declToImport {"
      "  typename A<T>::template B<T> a;"
      "};",
      Lang_CXX, "", Lang_CXX, Verifier,
      classTemplateDecl(has(cxxRecordDecl(
          has(fieldDecl(hasType(dependentTemplateSpecializationType()))))))));
}

const internal::VariadicDynCastAllOfMatcher<Stmt, SizeOfPackExpr>
    sizeOfPackExpr;

TEST(ImportExpr, ImportSizeOfPackExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
      testImport("template <typename... Ts>"
                 "void declToImport() {"
                 "  const int i = sizeof...(Ts);"
                 "};",
                 Lang_CXX11, "", Lang_CXX11, Verifier,
                 functionTemplateDecl(has(functionDecl(hasBody(
                     compoundStmt(has(declStmt(has(varDecl(hasInitializer(
                         implicitCastExpr(has(sizeOfPackExpr()))))))))))))));
  EXPECT_TRUE(testImport(
      "template <typename... Ts>"
      "using X = int[sizeof...(Ts)];"
      "template <typename... Us>"
      "struct Y {"
      "  X<Us..., int, double, int, Us...> f;"
      "};"
      "Y<float, int> declToImport;",
      Lang_CXX11, "", Lang_CXX11, Verifier,
      varDecl(hasType(classTemplateSpecializationDecl(has(fieldDecl(hasType(
          hasUnqualifiedDesugaredType(constantArrayType(hasSize(7)))))))))));
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
  EXPECT_TRUE(testImport("void declToImport() { "
                         "  __builtin_types_compatible_p(int, int);"
                         "}",
                         Lang_C, "", Lang_C, Verifier,
                         functionDecl(
                           hasBody(
                             compoundStmt(
                               has(
                                 typeTraitExpr(hasType(asString("int")))))))));
}

TEST(ImportExpr, ImportCXXTypeidExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport(
      "namespace std { class type_info {}; }"
      "void declToImport() {"
      "  int x;"
      "  auto a = typeid(int); auto b = typeid(x);"
      "}",
      Lang_CXX11, "", Lang_CXX11, Verifier,
      functionDecl(
          hasDescendant(varDecl(
              hasName("a"), hasInitializer(hasDescendant(cxxTypeidExpr())))),
          hasDescendant(varDecl(
              hasName("b"), hasInitializer(hasDescendant(cxxTypeidExpr())))))));
}

TEST(ImportExpr, ImportTypeTraitExprValDep) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template<typename T> struct declToImport {"
                         "  void m() { __is_pod(T); };"
                         "};",
                         Lang_CXX11, "", Lang_CXX11, Verifier,
                         classTemplateDecl(
                           has(
                             cxxRecordDecl(
                               has(
                                 functionDecl(
                                   hasBody(
                                     compoundStmt(
                                       has(
                                         typeTraitExpr(
                                           hasType(asString("_Bool"))
                                           )))))))))));
}

const internal::VariadicDynCastAllOfMatcher<Expr, CXXPseudoDestructorExpr>
    cxxPseudoDestructorExpr;

TEST(ImportExpr, ImportCXXPseudoDestructorExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("typedef int T;"
             "void declToImport(int *p) {"
             "  T t;"
             "  p->T::~T();"
             "}",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionDecl(has(compoundStmt(has(
                 callExpr(has(cxxPseudoDestructorExpr()))))))));
}

TEST(ImportDecl, ImportUsingDecl) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("namespace foo { int bar; }"
             "void declToImport() { using foo::bar; }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionDecl(
               has(
                 compoundStmt(
                   has(
                     declStmt(
                       has(
                         usingDecl()))))))));
}

TEST(ImportDecl, ImportRecordDeclInFuncParams) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "int declToImport(struct data_t{int a;int b;} *d){ return 0; }",
          Lang_CXX, "", Lang_CXX, Verifier,
          functionDecl()));
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
  EXPECT_TRUE(testImport("namespace foo { int bar; }"
             "namespace declToImport { using foo::bar; }",
             Lang_CXX, "", Lang_CXX, Verifier,
             namespaceDecl(has(usingShadowDecl()))));
}

TEST(ImportDecl, ImportFunctionTemplateDecl) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "template <typename T> void declToImport() { };",
          Lang_CXX, "", Lang_CXX, Verifier,
          functionTemplateDecl()));
  EXPECT_TRUE(testImport(
      "template<typename Y> int a() { return 1; }"
      "template<typename Y, typename D> int a(){ return 2; }"
      "void declToImport() { a<void>(); }",
      Lang_CXX, "", Lang_CXX, Verifier,
      functionDecl(has(compoundStmt(has(callExpr(has(ignoringParenImpCasts(
          declRefExpr(to(functionDecl(hasBody(compoundStmt(
              has(returnStmt(has(integerLiteral(equals(1)))))))))))))))))));
  EXPECT_TRUE(testImport(
      "template<typename Y> int a() { return 1; }"
      "template<typename Y, typename D> int a() { return 2; }"
      "void declToImport() { a<void,void>(); }",
      Lang_CXX, "", Lang_CXX, Verifier,
      functionDecl(has(compoundStmt(has(callExpr(has(ignoringParenImpCasts(
          declRefExpr(to(functionDecl(hasBody(compoundStmt(
              has(returnStmt(has(integerLiteral(equals(2)))))))))))))))))));
}

TEST(ImportDecl, DISABLED_ImportTemplateDefaultArgument) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
        testImport(
          "template <typename T=int> void declToImport(T &t) { };",
          Lang_CXX11, "", Lang_CXX11, Verifier,
          functionTemplateDecl(has(templateArgument()))));
}

TEST_F(Fixture, ImportOfTemplatedDeclOfClassTemplateDecl) {
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

// Currently disabled, because lookup fails to find the existing templated decl.
TEST_F(Fixture, DISABLED_ImportOfTemplatedDeclOfFunctionTemplateDecl) {
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

TEST_F(Fixture, ImportOfTemplatedDeclShouldImportTheClassTemplateDecl) {
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

TEST_F(Fixture, ImportOfTemplatedDeclShouldImportTheFunctionTemplateDecl) {
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

TEST_F(Fixture, ImportCorrectTemplatedDecl) {
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

  EXPECT_TRUE(testImport(Code, Lang_CXX, "", Lang_CXX, Verifier, recordDecl()));
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

  EXPECT_TRUE(testImport(Code, Lang_CXX, "", Lang_CXX, Verifier,
                         namespaceDecl()));
}

TEST(ImportExpr, ImportTypedefOfUnnamedStruct) {
  MatchVerifier<Decl> Verifier;
  auto Code = "typedef struct {} declToImport;";
  EXPECT_TRUE(
      testImport(Code, Lang_CXX, "", Lang_CXX, Verifier, typedefDecl()));
}

TEST(ImportExpr, ImportTypedefOfUnnamedStructWithCharArray) {
  MatchVerifier<Decl> Verifier;
  auto Code = R"s(
      struct declToImport
      {
        typedef struct { char arr[2]; } two;
      };
          )s";
  EXPECT_TRUE(testImport(Code, Lang_CXX, "", Lang_CXX, Verifier, recordDecl()));
}

TEST(ImportExpr, ImportVarOfUnnamedStruct) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("struct {} declToImport;", Lang_CXX, "", Lang_CXX,
                         Verifier, varDecl()));
}

TEST_F(Fixture, ImportFunctionWithBackReferringParameter) {
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
  EXPECT_TRUE(testImport("struct S { template <typename T> void mem(); };"
                         "template <typename U> void declToImport() {"
                         "S s;"
                         "s.mem<U>();"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionTemplateDecl(has(functionDecl(has(compoundStmt(
                             has(callExpr(has(unresolvedMemberExpr()))))))))));
}

const internal::VariadicDynCastAllOfMatcher<Expr, DependentScopeDeclRefExpr>
    dependentScopeDeclRefExpr;
TEST(ImportExpr, ImportDependentScopeDeclRefExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template <typename T> struct S;"
                         "template <typename T> void declToImport() {"
                         "S<T>::foo;"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionTemplateDecl(has(functionDecl(has(compoundStmt(
                             has(dependentScopeDeclRefExpr()))))))));

  EXPECT_TRUE(testImport("template <typename T> struct S;"
                         "template <typename T> void declToImport() {"
                         "S<T>::template foo;"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionTemplateDecl(has(functionDecl(has(compoundStmt(
                             has(dependentScopeDeclRefExpr()))))))));

  EXPECT_TRUE(testImport("template <typename T> struct S;"
                         "template <typename T> void declToImport() {"
                         "S<T>::template foo<>;"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionTemplateDecl(has(functionDecl(has(compoundStmt(
                             has(dependentScopeDeclRefExpr()))))))));

  EXPECT_TRUE(testImport("template <typename T> struct S;"
                         "template <typename T> void declToImport() {"
                         "S<T>::template foo<T>;"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionTemplateDecl(has(functionDecl(has(compoundStmt(
                             has(dependentScopeDeclRefExpr()))))))));
}
const internal::VariadicDynCastAllOfMatcher<Type, DependentNameType>
    dependentNameType;
TEST(ImportExpr, DependentNameType) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template <typename T> struct declToImport {"
                         "typedef typename T::type dependent_name;"
                         "};",
                         Lang_CXX11, "", Lang_CXX11, Verifier,
                         classTemplateDecl(has(cxxRecordDecl(
                             has(typedefDecl(has(dependentNameType()))))))));
}

TEST(ImportExpr, DependentSizedArrayType) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template<typename T, int Size> class declToImport {"
                         "  T data[Size];"
                         "};",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         classTemplateDecl(has(cxxRecordDecl(has(fieldDecl(
                             hasType(dependentSizedArrayType()))))))));
}

TEST(ImportExpr, CXXOperatorCallExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("class declToImport {"
                         "  void f() { *this = declToImport(); }"
                         "};",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         cxxRecordDecl(has(cxxMethodDecl(hasBody(compoundStmt(
                             has(exprWithCleanups(
                                 has(cxxOperatorCallExpr()))))))))));
}

TEST(ImportExpr, CXXNamedCastExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("void declToImport() {"
                         "  const_cast<char*>(\"hello\");"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(hasBody(compoundStmt(has(
                             cxxConstCastExpr()))))));
  EXPECT_TRUE(testImport("void declToImport() {"
                         "  double d;"
                         "  reinterpret_cast<int*>(&d);"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(hasBody(compoundStmt(has(
                             cxxReinterpretCastExpr()))))));
  EXPECT_TRUE(testImport("struct A {virtual ~A() {} };"
                         "struct B : A {};"
                         "void declToImport() {"
                         "  dynamic_cast<B*>(new A);"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(hasBody(compoundStmt(has(
                             cxxDynamicCastExpr()))))));
  EXPECT_TRUE(testImport("struct A {virtual ~A() {} };"
                         "struct B : A {};"
                         "void declToImport() {"
                         "  static_cast<B*>(new A);"
                         "}",
                         Lang_CXX, "", Lang_CXX, Verifier,
                         functionDecl(hasBody(compoundStmt(has(
                             cxxStaticCastExpr()))))));
}

TEST(ImportExpr, ImportUnresolvedLookupExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template<typename T> int foo();"
             "template <typename T> void declToImport() {"
             "  ::foo<T>;"
             "  ::template foo<T>;"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(has(functionDecl(
                 has(compoundStmt(has(unresolvedLookupExpr()))))))));
}

TEST(ImportExpr, ImportCXXUnresolvedConstructExpr) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template <typename T> struct C { T t; };"
             "template <typename T> void declToImport() {"
             "  C<T> d;"
             "  d.t = T();"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(has(functionDecl(has(compoundStmt(has(
                 binaryOperator(has(cxxUnresolvedConstructExpr()))))))))));
  EXPECT_TRUE(testImport("template <typename T> struct C { T t; };"
             "template <typename T> void declToImport() {"
             "  C<T> d;"
             "  (&d)->t = T();"
             "}"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(has(functionDecl(has(compoundStmt(has(
                 binaryOperator(has(cxxUnresolvedConstructExpr()))))))))));
}

/// Check that function "declToImport()" (which is the templated function
/// for corresponding FunctionTemplateDecl) is not added into DeclContext.
/// Same for class template declarations.
TEST(ImportDecl, ImportTemplatedDeclForTemplate) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(testImport("template <typename T> void declToImport() { T a = 1; }"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             functionTemplateDecl(hasAncestor(translationUnitDecl(
                 unless(has(functionDecl(hasName("declToImport")))))))));
  EXPECT_TRUE(testImport("template <typename T> struct declToImport { T t; };"
             "void instantiate() { declToImport<int>(); }",
             Lang_CXX, "", Lang_CXX, Verifier,
             classTemplateDecl(hasAncestor(translationUnitDecl(
                 unless(has(cxxRecordDecl(hasName("declToImport")))))))));
}

TEST_F(Fixture, TUshouldNotContainTemplatedDeclOfFunctionTemplates) {

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

TEST_F(Fixture, TUshouldNotContainTemplatedDeclOfClassTemplates) {

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

TEST_F(Fixture, TUshouldNotContainTemplatedDeclOfTypeAlias) {

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

TEST_F(
    Fixture,
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

TEST_F(
    Fixture,
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


TEST_F(Fixture, CXXRecordDeclFieldsShouldBeInCorrectOrder) {


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

TEST_F(Fixture,
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

TEST_F(Fixture, ShouldImportImplicitCXXRecordDecl) {
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

TEST_F(Fixture, ShouldImportImplicitCXXRecordDeclOfClassTemplate) {
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

TEST_F(
    Fixture,
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

TEST_F(Fixture, IDNSOrdinary) {
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

TEST_F(Fixture, IDNSOfNonmemberOperator) {
  Decl *FromTU = getTuDecl(
    R"(
    struct X {};
    void operator<<(int, X);
    )",Lang_CXX);
  Decl* From = LastDeclMatcher<Decl>{}.match(FromTU, functionDecl());
  const Decl* To = Import(From, Lang_CXX);
  EXPECT_EQ(From->getIdentifierNamespace(), To->getIdentifierNamespace());
}

TEST_F(
    Fixture,
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

struct ImportFunctions : Fixture {};

TEST_F(ImportFunctions,
       PrototypeShouldBeImportedAsAPrototypeWhenThereIsNoDefinition) {
  Decl *FromTU = getTuDecl("void f();", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  FunctionDecl *FromD =
      FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  FunctionDecl *ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. without a body
  EXPECT_TRUE(!ToFD->doesThisDeclarationHaveABody());
}

TEST_F(ImportFunctions,
       PrototypeShouldBeImportedAsDefintionWhenThereIsADefinition) {
  Decl *FromTU = getTuDecl("void f(); void f() {}", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  FunctionDecl *FromD = // Prototype
      FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  FunctionDecl *ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. with a body
  EXPECT_TRUE(ToFD->doesThisDeclarationHaveABody());
}

TEST_F(ImportFunctions,
       DefinitionShouldBeImportedAsDefintionWhenThereIsAPrototype) {
  Decl *FromTU = getTuDecl("void f(); void f() {}", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  FunctionDecl *FromD = // Definition
      LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  FunctionDecl *ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. with a body
  EXPECT_TRUE(ToFD->doesThisDeclarationHaveABody());
}

TEST_F(ImportFunctions,
       DefinitionShouldBeImportedAsADefinition) {
  Decl *FromTU = getTuDecl("void f() {}", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  FunctionDecl *FromD =
      FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(FromD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  FunctionDecl *ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. with a body
  EXPECT_TRUE(ToFD->doesThisDeclarationHaveABody());
}

TEST_F(ImportFunctions, ImportPrototypeOfRecursiveFunction) {
  Decl *FromTU = getTuDecl("void f(); void f() { f(); }", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  FunctionDecl *PrototypeFD =
      FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(PrototypeFD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  FunctionDecl *ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. with a body
  EXPECT_TRUE(ToFD->doesThisDeclarationHaveABody());
}

TEST_F(ImportFunctions, ImportDefinitionOfRecursiveFunction) {
  Decl *FromTU = getTuDecl("void f(); void f() { f(); }", Lang_CXX);
  auto Pattern = functionDecl(hasName("f"));
  FunctionDecl *DefinitionFD =
      LastDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

  Decl *ImportedD = Import(DefinitionFD, Lang_CXX);
  Decl *ToTU = ImportedD->getTranslationUnitDecl();

  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  FunctionDecl *ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. with a body
  EXPECT_TRUE(ToFD->doesThisDeclarationHaveABody());
}

TEST_F(ImportFunctions,
       ImportPrototypes) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *ImportedD;
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input0.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

    ImportedD = Import(FromD, Lang_CXX);
  }
  Decl *ImportedD1;
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input1.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    ImportedD1 = Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  FunctionDecl *ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_EQ(ImportedD, ImportedD1);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. without a body
  EXPECT_TRUE(!ToFD->doesThisDeclarationHaveABody());
}

TEST_F(ImportFunctions,
       ImportDefinitionThenPrototype) {
  auto Pattern = functionDecl(hasName("f"));

  Decl *ImportedD;
  {
    Decl *FromTU = getTuDecl("void f(){}", Lang_CXX, "input0.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

    ImportedD = Import(FromD, Lang_CXX);
  }
  Decl *ImportedD1;
  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input1.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    ImportedD1 = Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  // There must be only one imported FunctionDecl ...
  EXPECT_TRUE(FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern) ==
              LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern));
  FunctionDecl *ToFD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_EQ(ImportedD, ImportedD1);
  EXPECT_TRUE(ImportedD == ToFD);
  // .. with a body
  EXPECT_TRUE(ToFD->doesThisDeclarationHaveABody());
}

TEST_F(ImportFunctions,
       ImportPrototypeThenDefinition) {
  auto Pattern = functionDecl(hasName("f"));

  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input0.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

    Import(FromD, Lang_CXX);
  }
  {
    Decl *FromTU = getTuDecl("void f(){}", Lang_CXX, "input1.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  FunctionDecl* ProtoD = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(!ProtoD->doesThisDeclarationHaveABody());
  FunctionDecl* DefinitionD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(DefinitionD->doesThisDeclarationHaveABody());
  EXPECT_EQ(DefinitionD->getPreviousDecl(), ProtoD);
}

TEST_F(ImportFunctions,
       ImportPrototypeThenProtoAndDefinition) {
  auto Pattern = functionDecl(hasName("f"));

  {
    Decl *FromTU = getTuDecl("void f();", Lang_CXX, "input0.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);

    Import(FromD, Lang_CXX);
  }
  {
    Decl *FromTU = getTuDecl("void f(); void f(){}", Lang_CXX, "input1.cc");
    FunctionDecl *FromD =
        FirstDeclMatcher<FunctionDecl>().match(FromTU, Pattern);
    Import(FromD, Lang_CXX);
  }

  Decl *ToTU = ToAST->getASTContext().getTranslationUnitDecl();
  ASSERT_EQ(DeclCounter<FunctionDecl>().match(ToTU, Pattern), 2u);
  FunctionDecl* ProtoD = FirstDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(!ProtoD->doesThisDeclarationHaveABody());
  FunctionDecl* DefinitionD = LastDeclMatcher<FunctionDecl>().match(ToTU, Pattern);
  EXPECT_TRUE(DefinitionD->doesThisDeclarationHaveABody());
  EXPECT_EQ(DefinitionD->getPreviousDecl(), ProtoD);
}

TEST_F(ImportFunctions,
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

TEST_F(ImportFunctions,
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

TEST_F(Fixture, OmitVAListTag) {
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

TEST_F(Fixture, ProperPrevDeclForClassTemplateDecls) {
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

TEST_F(Fixture, TypeForDeclShouldBeSet) {
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

TEST_F(Fixture, DeclsFromFriendsShouldBeInRedeclChains2) {
  Decl *From, *To;
  std::tie(From, To) =
      getImportedDecl("class declToImport {};", Lang_CXX,
                      "class Y { friend class declToImport; };", Lang_CXX);
  auto *Imported = cast<CXXRecordDecl>(To);

  EXPECT_TRUE(Imported->getPreviousDecl());
}

} // end namespace ast_matchers
} // end namespace clang
