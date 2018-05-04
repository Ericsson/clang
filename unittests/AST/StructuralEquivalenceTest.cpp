#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"

#include "Language.h"
#include "DeclMatcher.h"

#include "gtest/gtest.h"

namespace clang {
namespace ast_matchers {

struct StructuralEquivalenceTest : ::testing::Test {
  std::unique_ptr<ASTUnit> AST0, AST1;
  std::string Code0, Code1; // Buffers for SourceManager

  // Get a pair of Decl pointers to the synthetised declarations from the given
  // code snipets. By default we search for the unique Decl with name 'foo' in
  // both snippets.
  std::tuple<NamedDecl *, NamedDecl *>
  makeNamedDecls(const std::string &SrcCode0, const std::string &SrcCode1,
                 Language Lang, const char *const Identifier = "foo") {

    this->Code0 = SrcCode0;
    this->Code1 = SrcCode1;
    StringVector Args;
    getLangArgs(Lang, Args);

    const char *const InputFileName = "input.cc";

    AST0 = tooling::buildASTFromCodeWithArgs(Code0, Args, InputFileName);
    AST1 = tooling::buildASTFromCodeWithArgs(Code1, Args, InputFileName);

    ASTContext &Ctx0 = AST0->getASTContext(), &Ctx1 = AST1->getASTContext();

    auto getDecl = [](ASTContext &Ctx, const std::string &Name) -> NamedDecl * {
      IdentifierInfo *ImportedII = &Ctx.Idents.get(Name);
      assert(ImportedII && "Declaration with the identifier "
                           "should be specified in test!");
      DeclarationName ImportDeclName(ImportedII);
      SmallVector<NamedDecl *, 4> FoundDecls;
      Ctx.getTranslationUnitDecl()->localUncachedLookup(ImportDeclName,
                                                        FoundDecls);

      // We should find one Decl but one only
      assert(FoundDecls.size() > 0);
      assert(FoundDecls.size() < 2);

      return FoundDecls[0];
    };

    NamedDecl *d0 = getDecl(Ctx0, Identifier);
    NamedDecl *d1 = getDecl(Ctx1, Identifier);
    assert(d0);
    assert(d1);
    return std::make_tuple(d0, d1);
  }

  bool testStructuralMatch(NamedDecl *d0, NamedDecl *d1) {
    llvm::DenseSet<std::pair<Decl *, Decl *>> NonEquivalentDecls;
    StructuralEquivalenceContext Ctx(d0->getASTContext(), d1->getASTContext(),
                                     NonEquivalentDecls, false, false);
    return Ctx.IsEquivalent(d0, d1);
  }
};

using std::get;

TEST_F(StructuralEquivalenceTest, Int) {
  auto t = makeNamedDecls("int foo;", "int foo;", Lang_CXX);
  EXPECT_TRUE(testStructuralMatch(get<0>(t), get<1>(t)));
}

TEST_F(StructuralEquivalenceTest, IntVsSignedInt) {
  auto t = makeNamedDecls("int foo;", "signed int foo;", Lang_CXX);
  EXPECT_TRUE(testStructuralMatch(get<0>(t), get<1>(t)));
}

TEST_F(StructuralEquivalenceTest, Char) {
  auto t = makeNamedDecls("char foo;", "char foo;", Lang_CXX);
  EXPECT_TRUE(testStructuralMatch(get<0>(t), get<1>(t)));
}

TEST_F(StructuralEquivalenceTest, CharVsSignedChar) {
  auto t = makeNamedDecls("char foo;", "signed char foo;", Lang_CXX);
  // TODO this should be false!
  // FIXME in clang::StructuralEquivalenceContext::Finish
  EXPECT_TRUE(testStructuralMatch(get<0>(t), get<1>(t)));
}

TEST_F(StructuralEquivalenceTest, ForwardRecordDecl) {
  auto t = makeNamedDecls("struct foo;", "struct foo;", Lang_CXX);
  EXPECT_TRUE(testStructuralMatch(get<0>(t), get<1>(t)));
}

TEST_F(StructuralEquivalenceTest, IntVsSignedIntInStruct) {
  auto t = makeNamedDecls("struct foo { int x; };",
                          "struct foo { signed int x; };", Lang_CXX);
  EXPECT_TRUE(testStructuralMatch(get<0>(t), get<1>(t)));
}

TEST_F(StructuralEquivalenceTest, CharVsSignedCharInStruct) {
  auto t = makeNamedDecls("struct foo { char x; };",
                          "struct foo { signed char x; };", Lang_CXX);
  EXPECT_FALSE(testStructuralMatch(get<0>(t), get<1>(t)));
}

TEST_F(StructuralEquivalenceTest, IntVsSignedIntTemplateSpec) {
  auto t = makeNamedDecls(
      R"(template <class T> struct foo; template<> struct foo<int>{};)",
      R"(template <class T> struct foo; template<> struct foo<signed int>{};)",
      Lang_CXX);
  ClassTemplateSpecializationDecl *Spec0 =
      *cast<ClassTemplateDecl>(get<0>(t))->spec_begin();
  ClassTemplateSpecializationDecl *Spec1 =
      *cast<ClassTemplateDecl>(get<1>(t))->spec_begin();
  ASSERT_TRUE(Spec0 != nullptr);
  ASSERT_TRUE(Spec1 != nullptr);
  EXPECT_TRUE(testStructuralMatch(Spec0, Spec1));
}

TEST_F(StructuralEquivalenceTest, CharVsSignedCharTemplateSpec) {
  auto t = makeNamedDecls(
      R"(template <class T> struct foo; template<> struct foo<char>{};)",
      R"(template <class T> struct foo; template<> struct foo<signed char>{};)",
      Lang_CXX);
  ClassTemplateSpecializationDecl *Spec0 =
      *cast<ClassTemplateDecl>(get<0>(t))->spec_begin();
  ClassTemplateSpecializationDecl *Spec1 =
      *cast<ClassTemplateDecl>(get<1>(t))->spec_begin();
  ASSERT_TRUE(Spec0 != nullptr);
  ASSERT_TRUE(Spec1 != nullptr);
  EXPECT_FALSE(testStructuralMatch(Spec0, Spec1));
}

TEST_F(StructuralEquivalenceTest, CharVsSignedCharTemplateSpecWithInheritance) {
  auto t = makeNamedDecls(
      R"(
struct true_type{};
template <class T> struct foo;
template<> struct foo<char> : true_type {};
      )",
      R"(
struct true_type{};
template <class T> struct foo;
template<> struct foo<signed char> : true_type {};
      )",
      Lang_CXX);
  ClassTemplateSpecializationDecl *Spec0 =
      *cast<ClassTemplateDecl>(get<0>(t))->spec_begin();
  ClassTemplateSpecializationDecl *Spec1 =
      *cast<ClassTemplateDecl>(get<1>(t))->spec_begin();
  ASSERT_TRUE(Spec0 != nullptr);
  ASSERT_TRUE(Spec1 != nullptr);
  EXPECT_FALSE(testStructuralMatch(Spec0, Spec1));
}


TEST_F(StructuralEquivalenceTest, WrongOrderInNamespace) {
  auto Code0 =
      R"(
namespace NS {
template <class T> class Base {
    int a;
};
class Derived : Base<Derived> {
};
}
void foo(NS::Derived &);
      )";
  auto t = makeNamedDecls( Code0, Code0, Lang_CXX);

  ASSERT_TRUE(get<0>(t) != nullptr);
  ASSERT_TRUE(get<1>(t) != nullptr);

  NamespaceDecl *NS =
      LastDeclMatcher<NamespaceDecl>().match(get<1>(t), namespaceDecl());
  ClassTemplateDecl *TD = LastDeclMatcher<ClassTemplateDecl>().match(
      get<1>(t), classTemplateDecl(hasName("Base")));

  // Reorder the decls, move the TD to the last place in the DC.
  NS->removeDecl(TD);
  NS->addDeclInternal(TD);

  // TODO this should be FALSE!
  // FIXME in clang::StructuralEquivalenceContext
  EXPECT_TRUE(testStructuralMatch(get<0>(t), get<1>(t)));
}

TEST_F(StructuralEquivalenceTest, WrongOrderOfFieldsInClass) {
  auto Code0 = "class X { int a; int b; };";
  auto t = makeNamedDecls( Code0, Code0, Lang_CXX, "X");

  ASSERT_TRUE(get<0>(t) != nullptr);
  ASSERT_TRUE(get<1>(t) != nullptr);

  CXXRecordDecl *RD = FirstDeclMatcher<CXXRecordDecl>().match(
      get<1>(t), cxxRecordDecl(hasName("X")));
  FieldDecl *FD =
      FirstDeclMatcher<FieldDecl>().match(get<1>(t), fieldDecl(hasName("a")));

  // Reorder the FieldDecls
  RD->removeDecl(FD);
  RD->addDeclInternal(FD);

  EXPECT_FALSE(testStructuralMatch(get<0>(t), get<1>(t)));
}

} // end namespace ast_matchers
} // end namespace clang
