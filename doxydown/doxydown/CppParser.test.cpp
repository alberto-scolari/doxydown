/**
 * Unit tests for CppParser.
 *
 * Each test writes a small C++ snippet (mostly taken from demo/parsing.hpp and
 * demo/presentation.hpp) to a temporary file, parses it and inspects the
 * entities stored in the EntityRegistry. The expected names, declarations and
 * documentation strings match what is rendered in docs/generated.
 */
#include <doxydown/CppParser.hpp>
#include <doxydown/StyleOption.hpp>
#include <doxydown/entities/EntityRegistry.hpp>

#include <tests/utils.hpp>

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

/**
 * Registry and parser shared by all tests: building a CppParser compiles the
 * tree-sitter queries, which is by far the most expensive step, so it is done
 * once; each test starts from a cleared registry.
 */
struct SharedParser {
  EntityRegistry registry{"Title", "Root docs."};
  CppParser parser{registry, StyleOption::MKDOCS};

  static SharedParser& get() {
    static SharedParser instance;
    return instance;
  }
};

/**
 * Common fixture: provides an empty registry (with the "parsing" and
 * "presentation" groups of doxydown.yaml), a parser and a scratch directory.
 */
class CppParserTestBase : public ::testing::Test {
protected:
  using Defines = std::unordered_map<std::string, std::string>;

  EntityRegistry& registry = SharedParser::get().registry;
  CppParser* parser = &SharedParser::get().parser;
  static std::atomic<unsigned> counter;
  fs::path dir;

  void SetUp() override {
    registry.clear();
    registry.add_group("parsing", "Parsing", "Parsing group.");
    registry.add_group("presentation", "Presentation", "Presentation group.");
    // static std::atomic<unsigned> counter{0};
    std::random_device rd;
    dir = fs::temp_directory_path() /
      ("doxydown_cppparser_test_" + std::to_string(rd()) + "_" + std::to_string(counter++));
    fs::create_directories(dir);
  }

  void TearDown() override {
    registry.clear();
    std::error_code ec;
    fs::remove_all(dir, ec);
  }

  fs::path write(std::string_view code, std::string_view name = "test.hpp") {
    fs::path p = dir / name;
    std::ofstream out(p, std::ios::binary);
    out << code;
    return p;
  }

  FwdRef<FileEntity> parse(std::string_view code, const Defines& defines = {},
      bool coverage = false, std::string_view name = "test.hpp") {
    auto file = parser->parse(write(code, name), defines, coverage);
    if (not file) {
      throw std::runtime_error("parse failed");
    }
    return *file;
  }

  FwdRef<FileEntity> parse_demo(std::string_view rel) {
    auto file = parser->parse(fs::path(DOXYDOWN_SOURCE_DIR) / rel, {}, false);
    if (not file) {
      throw std::runtime_error("parse failed");
    }
    return *file;
  }

  RefVariant root() { return registry.get_root_ref(); }

  RefVariant group(std::string_view name) {
    auto g = registry.get_group(name);
    if (not g) {
      throw std::runtime_error("no such group");
    }
    return *g;
  }

  auto types(const RefVariant& p) { return registry.get_type_children_for(p) | to_vector; }
  auto functions(const RefVariant& p) { return registry.get_function_children_for(p) | to_vector; }
  auto variables(const RefVariant& p) { return registry.get_variable_children_for(p) | to_vector; }
  auto enums(const RefVariant& p) { return registry.get_enum_children_for(p) | to_vector; }
  auto namespaces(const RefVariant& p) { return registry.get_namespace_children_for(p) | to_vector; }
  auto typedefs(const RefVariant& p) { return registry.get_typedef_children_for(p) | to_vector; }
  auto concepts(const RefVariant& p) { return registry.get_concept_children_for(p) | to_vector; }
  auto operators(const RefVariant& p) { return registry.get_operator_children_for(p) | to_vector; }
  auto macros(const RefVariant& p) { return registry.get_macro_children_for(p) | to_vector; }
  // TODO(lambdas): re-enable together with the lambda tests below
  // auto lambdas(const RefVariant& p) { return collect(registry.get_lambda_children_for(p)); }

  /** The single variable at root level, failing if there is not exactly one. */
  FwdRef<VariableEntity> only_root_variable() {
    auto vars = variables(root());
    if (vars.size() != 1) {
      throw std::runtime_error("expected exactly one root variable, got " + std::to_string(vars.size()));
    }
    return vars.front();
  }

  /** Total number of entities directly under root, of any kind. */
  std::size_t count_root_children() {
    return types(root()).size() + functions(root()).size() + variables(root()).size()
      + namespaces(root()).size() + typedefs(root()).size() + concepts(root()).size()
      + operators(root()).size() + macros(root()).size();
      // TODO(lambdas): + lambdas(root()).size();
  }
};

std::atomic<unsigned> CppParserTestBase::counter{0};


class CppParserTest : public CppParserTestBase {};

} // namespace

// ---------------------------------------------------------------------------
// File handling
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, ParseReturnsFileWithNormalizedPath) {
  fs::path p = write("int a;\n");
  fs::path unnormalized = p.parent_path() / "." / p.filename();
  auto file = parser->parse(unnormalized, {}, false);
  ASSERT_TRUE(file.has_value());
  EXPECT_EQ((*file)->get_path(), p.lexically_normal());
}

TEST_F(CppParserTest, ParseStoresFileContent) {
  constexpr std::string_view code = "int a;\nint b;\n";
  auto file = parse(code);
  EXPECT_EQ(file->get_content(), code);
}

TEST_F(CppParserTest, ParseStoresFilename) {
  auto file = parse("int a;\n", {}, false, "my_header.hpp");
  EXPECT_EQ(file->get_filename(), fs::path("my_header.hpp"));
}

TEST_F(CppParserTest, NumLinesIsLastRowOfParseTree) {
  auto file = parse("int a;\nint b;\nint c;\n");
  EXPECT_EQ(file->get_num_lines(), 3u);
}

TEST_F(CppParserTest, EmptyFileProducesNoEntities) {
  auto file = parse("");
  EXPECT_EQ(file->get_content(), "");
  EXPECT_EQ(count_root_children(), 0u);
}

TEST_F(CppParserTest, FileWithOnlyCommentsProducesNoEntities) {
  parse("/** Dangling documentation. */\n// plain comment\n");
  EXPECT_EQ(count_root_children(), 0u);
}

TEST_F(CppParserTest, ParsingSameFileTwiceThrows) {
  fs::path p = write("int a;\n");
  ASSERT_TRUE(parser->parse(p, {}, false).has_value());
  EXPECT_THROW(parser->parse(p, {}, false), std::runtime_error);
}

TEST_F(CppParserTest, ParserCanParseSeveralFiles) {
  parser->parse(write("/** A. */\nint a;\n", "a.hpp"), {}, false);
  parser->parse(write("/** B. */\nint b;\n", "b.hpp"), {}, false);
  auto vars = variables(root());
  ASSERT_EQ(vars.size(), 2u);
  EXPECT_EQ(vars[0]->get_name(), "a");
  EXPECT_EQ(vars[1]->get_name(), "b");
}

TEST_F(CppParserTest, EntityRemembersItsFile) {
  auto file = parse("/** A. */\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_path(), file->get_path());
}

TEST_F(CppParserTest, PipeCommentsAreNotDocumentation) {
  // `|` is not a Doxygen marker: these are ordinary comments
  parse("int plain;\n/*| Not docs. */\nint a;\n//| Not docs.\nint b;\n"
    "int c; /*|< Not docs. */\nint d; //|< Not docs.\n");
  auto vars = variables(root());
  ASSERT_EQ(vars.size(), 5u);
  for (const auto& v : vars) {
    SCOPED_TRACE(std::string(v->get_name()));
    EXPECT_EQ(v->get_docs(), vars[0]->get_docs());
  }
}

TEST_F(CppParserTest, ParseErrorStillParsesValidEntities) {
  parse("/** A. */\nint a;\n@@@ garbage $$$\n/** B. */\nint b;\n");
  auto vars = variables(root());
  ASSERT_GE(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_name(), "a");
  EXPECT_EQ(vars[0]->get_docs(), "A.\n");
}

// ---------------------------------------------------------------------------
// Variables (demo/parsing.hpp, "Variables")
// ---------------------------------------------------------------------------

namespace {

struct DeclCase {
  const char* label;
  const char* code;
  const char* name;
  const char* decl;
};

// void PrintTo(const DeclCase& c, std::ostream* os) { *os << c.label; }

std::string case_name(const ::testing::TestParamInfo<DeclCase>& info) {
  return info.param.label;
}

const DeclCase VARIABLE_CASES[] = {
  {"Plain", "int w;", "w", "int w"},
  {"Initializer", "int x = 0;", "x", "int x"},
  {"ParenthesizedInitializer", "int y(0);", "y", "int y"},
  {"BraceInitializer", "int z{0};", "z", "int z"},
  {"Array", "int arr1[10];", "arr1", "int arr1[10]"},
  {"Reference", "int& ref1;", "ref1", "int& ref1"},
  {"Pointer", "int* ptr1;", "ptr1", "int* ptr1"},
  {"ArrayWithInitializer", "int arr2[10] = {0};", "arr2", "int arr2[10]"},
  {"ReferenceWithInitializer", "int& ref2 = x;", "ref2", "int& ref2"},
  {"PointerWithInitializer", "int* ptr2 = &x;", "ptr2", "int* ptr2"},
  {"Unicode", "int λ;", "λ", "int λ"},
  {"FunctionPointer", "int (*fp)(int, int);", "fp", "int (*fp)(int, int)"},
};

class CppParserVariableTest : public CppParserTestBase,
  public ::testing::WithParamInterface<DeclCase> {};

} // namespace

TEST_P(CppParserVariableTest, IsParsedAsVariable) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(variables(root()).size(), 1u);
  EXPECT_EQ(functions(root()).size(), 0u);
}

TEST_P(CppParserVariableTest, HasName) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(only_root_variable()->get_name(), GetParam().name);
}

TEST_P(CppParserVariableTest, DeclarationExcludesInitializerAndSemicolon) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(only_root_variable()->get_decl(), GetParam().decl);
}

TEST_P(CppParserVariableTest, IsNotTemplate) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_FALSE(only_root_variable()->is_template());
  EXPECT_EQ(only_root_variable()->get_template_decl(), "");
}

TEST_P(CppParserVariableTest, HasDocs) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Doc.\n");
  EXPECT_TRUE(only_root_variable()->is_visible());
}

TEST_P(CppParserVariableTest, IsSingleLine) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(only_root_variable()->get_start_line(), 1u);
  EXPECT_EQ(only_root_variable()->get_end_line(), 1u);
}

TEST_P(CppParserVariableTest, IsParsedAsMemberVariable) {
  if (std::string_view(GetParam().label) == "ParenthesizedInitializer") {
    GTEST_SKIP() << "`int y(0);` is not a valid member declaration";
  }
  std::string member_code{GetParam().code};
  parse(std::string("/** Class. */\nclass C {\npublic:\n  /** Doc. */\n  ") + member_code + "\n};\n");
  auto cls = types(root());
  ASSERT_EQ(cls.size(), 1u);
  auto members = variables(cls[0]);
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(members[0]->get_name(), GetParam().name);
  EXPECT_EQ(members[0]->get_decl(), GetParam().decl);
  EXPECT_EQ(members[0]->get_docs(), "Doc.\n");
  EXPECT_TRUE(variables(root()).empty());
}

INSTANTIATE_TEST_SUITE_P(Demo, CppParserVariableTest,
  ::testing::ValuesIn(VARIABLE_CASES), case_name);

// ---------------------------------------------------------------------------
// Functions (demo/parsing.hpp, "Functions")
// ---------------------------------------------------------------------------

namespace {

struct TemplateDeclCase {
  const char* label;
  const char* code;
  const char* name;
  const char* decl;
  const char* template_decl;
  uint32_t end_line; // line relative to the entity start (its `template<...>` header, if any)
};

// void PrintTo(const TemplateDeclCase& c, std::ostream* os) { *os << c.label; }

std::string tcase_name(const ::testing::TestParamInfo<TemplateDeclCase>& info) {
  return info.param.label;
}

constexpr const char* SFINAE = "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>";

const TemplateDeclCase FUNCTION_CASES[] = {
  {"Declaration", "int f(int x, int y);", "f", "int f(int x, int y)", "", 0},
  {"ReturnsReference", "int& f(int x, int y);", "f", "int& f(int x, int y)", "", 0},
  {"ReturnsPointer", "int* f(int x, int y);", "f", "int* f(int x, int y)", "", 0},
  {"ReturnsFunctionPointer", "int (*f(int x, int y))(int, int);", "f",
    "int (*f(int x, int y))(int, int)", "", 0},
  {"Template", "template<class T>\nT f(T x, T y);", "f",
    "template<class T> T f(T x, T y)", "template<class T>", 1},
  {"TemplateWithSfinae",
    "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>\nT f(T x, T y);", "f",
    "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0> T f(T x, T y)", SFINAE, 1},
  {"InlineDefinition", "inline int f(int x, int y) {\n  return 0;\n}", "f",
    "inline int f(int x, int y)", "", 2},
  {"InlineTemplateDefinition", "template<class T>\nT f(T x, T y) {\n  return T();\n}", "f",
    "template<class T> T f(T x, T y)", "template<class T>", 3},
  {"InlineTemplateWithSfinaeDefinition",
    "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>\nT f(T x, T y) {\n  return T();\n}", "f",
    "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0> T f(T x, T y)", SFINAE, 3},
};

class CppParserFunctionTest : public CppParserTestBase,
  public ::testing::WithParamInterface<TemplateDeclCase> {
protected:
  FwdRef<FunctionEntity> parse_only_function() {
    parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
    auto fns = functions(root());
    if (fns.size() != 1) {
      throw std::runtime_error("expected exactly one function, got " + std::to_string(fns.size()));
    }
    return fns.front();
  }
};

} // namespace

TEST_P(CppParserFunctionTest, IsParsedAsFunctionOnly) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(functions(root()).size(), 1u);
  EXPECT_EQ(variables(root()).size(), 0u);
  EXPECT_EQ(operators(root()).size(), 0u);
}

TEST_P(CppParserFunctionTest, HasName) {
  EXPECT_EQ(parse_only_function()->get_name(), GetParam().name);
}

TEST_P(CppParserFunctionTest, DeclarationExcludesBodyAndSemicolon) {
  EXPECT_EQ(parse_only_function()->get_decl(), GetParam().decl);
}

TEST_P(CppParserFunctionTest, TemplateDeclaration) {
  auto fn = parse_only_function();
  EXPECT_EQ(fn->get_template_decl(), GetParam().template_decl);
  EXPECT_EQ(fn->is_template(), std::string_view(GetParam().template_decl) != "");
}

TEST_P(CppParserFunctionTest, HasDocs) {
  EXPECT_EQ(parse_only_function()->get_docs(), "Doc.\n");
}

TEST_P(CppParserFunctionTest, LineRange) {
  auto fn = parse_only_function();
  EXPECT_EQ(fn->get_start_line(), 1u);
  EXPECT_EQ(fn->get_end_line(), 1u + GetParam().end_line);
}

TEST_P(CppParserFunctionTest, IsParsedAsMemberFunction) {
  parse(std::string("/** Class. */\nclass C {\npublic:\n  /** Doc. */\n  ") + GetParam().code + "\n};\n");
  auto cls = types(root());
  ASSERT_EQ(cls.size(), 1u);
  auto members = functions(cls[0]);
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(members[0]->get_name(), GetParam().name);
  EXPECT_EQ(members[0]->get_template_decl(), GetParam().template_decl);
  EXPECT_EQ(members[0]->get_docs(), "Doc.\n");
  EXPECT_TRUE(functions(root()).empty());
}

INSTANTIATE_TEST_SUITE_P(Demo, CppParserFunctionTest,
  ::testing::ValuesIn(FUNCTION_CASES), tcase_name);

TEST_F(CppParserTest, ConstructorIsParsedAsMemberFunction) {
  parse("/** C. */\nclass C {\npublic:\n  /** Ctor. */\n  C();\n};\n");
  auto fns = functions(types(root())[0]);
  ASSERT_EQ(fns.size(), 1u);
  EXPECT_EQ(fns[0]->get_name(), "C");
  EXPECT_EQ(fns[0]->get_decl(), "C()");
  EXPECT_EQ(fns[0]->get_docs(), "Ctor.\n");
}

TEST_F(CppParserTest, DestructorIsParsedAsMemberFunction) {
  parse("/** C. */\nclass C {\npublic:\n  /** Dtor. */\n  ~C();\n};\n");
  auto fns = functions(types(root())[0]);
  ASSERT_EQ(fns.size(), 1u);
  EXPECT_EQ(fns[0]->get_name(), "~C");
  EXPECT_EQ(fns[0]->get_decl(), "~C()");
  EXPECT_EQ(fns[0]->get_docs(), "Dtor.\n");
}

TEST_F(CppParserTest, OverloadsAreKeptInSourceOrder) {
  parse("/** One. */\nint f(int x);\n/** Two. */\nint f(int x, int y);\n");
  auto fns = functions(root());
  ASSERT_EQ(fns.size(), 2u);
  EXPECT_EQ(fns[0]->get_docs(), "One.\n");
  EXPECT_EQ(fns[1]->get_docs(), "Two.\n");
}

TEST_F(CppParserTest, MultiLineFunctionDeclarationKeepsNewlines) {
  parse("/** Doc. */\nint f(int x,\n      int y);\n");
  auto fns = functions(root());
  ASSERT_EQ(fns.size(), 1u);
  EXPECT_EQ(fns[0]->get_decl(), "int f(int x,\n      int y)");
  EXPECT_EQ(fns[0]->get_start_line(), 1u);
  EXPECT_EQ(fns[0]->get_end_line(), 2u);
}

// ---------------------------------------------------------------------------
// Operators (demo/parsing.hpp, "Operators")
// ---------------------------------------------------------------------------

namespace {

const TemplateDeclCase OPERATOR_CASES[] = {
  {"Declaration", "int operator+(int x, int y);", "operator+", "int operator+(int x, int y)", "", 0},
  {"Template", "template<class T>\nT operator+(T x, T y);", "operator+",
    "template<class T> T operator+(T x, T y)", "template<class T>", 1},
  {"TemplateWithSfinae",
    "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>\nT operator+(T x, T y);", "operator+",
    "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0> T operator+(T x, T y)", SFINAE, 1},
  {"InlineDefinition", "inline int operator+(int x, int y) {\n  return 0;\n}", "operator+",
    "inline int operator+(int x, int y)", "", 2},
  {"InlineTemplateDefinition", "template<class T>\nT operator+(T x, T y) {\n  return T();\n}", "operator+",
    "template<class T> T operator+(T x, T y)", "template<class T>", 3},
  {"InlineTemplateWithSfinaeDefinition",
    "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0>\nT operator+(T x, T y) {\n  return T();\n}",
    "operator+",
    "template<class T, std::enable_if_t<std::is_integral_v<T>, int> = 0> T operator+(T x, T y)", SFINAE, 3},
};

const TemplateDeclCase MEMBER_OPERATOR_CASES[] = {
  {"Assignment", "ClassWithMembers& operator=(const ClassWithMembers& o);", "operator=",
    "ClassWithMembers& operator=(const ClassWithMembers& o)", "", 0},
  {"ConversionDeclaration", "operator int() const;", "int", "operator int() const", "", 0},
  {"ReferenceConversionDeclaration", "operator int&() const;", "int", "operator int&() const", "", 0},
  {"PointerConversionDeclaration", "operator int*() const;", "int", "operator int*() const", "", 0},
  {"ConversionDefinition", "operator double() const {\n    return 0.0;\n  }", "double",
    "operator double() const", "", 2},
};

class CppParserOperatorTest : public CppParserTestBase,
  public ::testing::WithParamInterface<TemplateDeclCase> {
protected:
  FwdRef<OperatorEntity> parse_only_operator() {
    parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
    auto ops = operators(root());
    if (ops.size() != 1) {
      throw std::runtime_error("expected exactly one operator, got " + std::to_string(ops.size()));
    }
    return ops.front();
  }
};

class CppParserMemberOperatorTest : public CppParserTestBase,
  public ::testing::WithParamInterface<TemplateDeclCase> {
protected:
  FwdRef<OperatorEntity> parse_only_member_operator() {
    parse(std::string("/** Class. */\nclass ClassWithMembers {\npublic:\n  /** Doc. */\n  ")
      + GetParam().code + "\n};\n");
    auto cls = types(root());
    if (cls.size() != 1) {
      throw std::runtime_error("expected exactly one class");
    }
    auto ops = operators(cls[0]);
    if (ops.size() != 1) {
      throw std::runtime_error("expected exactly one operator, got " + std::to_string(ops.size()));
    }
    return ops.front();
  }
};

} // namespace

TEST_P(CppParserOperatorTest, IsParsedAsOperatorOnly) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(operators(root()).size(), 1u);
  EXPECT_EQ(functions(root()).size(), 0u);
}

TEST_P(CppParserOperatorTest, HasName) {
  EXPECT_EQ(parse_only_operator()->get_name(), GetParam().name);
}

TEST_P(CppParserOperatorTest, Declaration) {
  EXPECT_EQ(parse_only_operator()->get_decl(), GetParam().decl);
}

TEST_P(CppParserOperatorTest, TemplateDeclaration) {
  EXPECT_EQ(parse_only_operator()->get_template_decl(), GetParam().template_decl);
}

TEST_P(CppParserOperatorTest, HasDocs) {
  EXPECT_EQ(parse_only_operator()->get_docs(), "Doc.\n");
}

TEST_P(CppParserOperatorTest, LineRange) {
  auto op = parse_only_operator();
  EXPECT_EQ(op->get_start_line(), 1u);
  EXPECT_EQ(op->get_end_line(), 1u + GetParam().end_line);
}

INSTANTIATE_TEST_SUITE_P(Demo, CppParserOperatorTest,
  ::testing::ValuesIn(OPERATOR_CASES), tcase_name);

TEST_P(CppParserMemberOperatorTest, HasName) {
  EXPECT_EQ(parse_only_member_operator()->get_name(), GetParam().name);
}

TEST_P(CppParserMemberOperatorTest, Declaration) {
  EXPECT_EQ(parse_only_member_operator()->get_decl(), GetParam().decl);
}

TEST_P(CppParserMemberOperatorTest, HasDocs) {
  EXPECT_EQ(parse_only_member_operator()->get_docs(), "Doc.\n");
}

TEST_P(CppParserMemberOperatorTest, IsNotAFunction) {
  parse(std::string("/** Class. */\nclass ClassWithMembers {\npublic:\n  /** Doc. */\n  ")
    + GetParam().code + "\n};\n");
  EXPECT_TRUE(functions(types(root())[0]).empty());
}

TEST_P(CppParserMemberOperatorTest, LineRange) {
  auto op = parse_only_member_operator();
  EXPECT_EQ(op->get_start_line(), 4u);
  EXPECT_EQ(op->get_end_line(), 4u + GetParam().end_line);
}

INSTANTIATE_TEST_SUITE_P(Demo, CppParserMemberOperatorTest,
  ::testing::ValuesIn(MEMBER_OPERATOR_CASES), tcase_name);

// ---------------------------------------------------------------------------
// Types (demo/parsing.hpp, "Types")
// ---------------------------------------------------------------------------

namespace {

const TemplateDeclCase TYPE_CASES[] = {
  {"ForwardClass", "class ForwardClass;", "ForwardClass", "class ForwardClass", "", 0},
  {"Class", "class Class {\n  //\n};", "Class", "class Class", "", 2},
  {"Struct", "struct Struct {\n  //\n};", "Struct", "struct Struct", "", 2},
  {"Union", "union Union {\n  //\n};", "Union", "union Union", "", 2},
  {"ClassTemplate", "template<class T>\nclass ClassTemplate {\n  //\n};", "ClassTemplate",
    "template<class T> class ClassTemplate", "template<class T>", 3},
  {"StructTemplate", "template<class T>\nstruct StructTemplate {\n  //\n};", "StructTemplate",
    "template<class T> struct StructTemplate", "template<class T>", 3},
  {"UnionTemplate", "template<class T>\nunion UnionTemplate {\n  //\n};", "UnionTemplate",
    "template<class T> union UnionTemplate", "template<class T>", 3},
  {"Enum", "enum Enum {\n  FIRST\n};", "Enum", "enum Enum", "", 2},
  {"EnumClass", "enum class EnumClass {\n  FIRST\n};", "EnumClass", "enum class EnumClass", "", 2},
  {"EnumStruct", "enum struct EnumStruct {\n  FIRST\n};", "EnumStruct", "enum struct EnumStruct", "", 2},
};

class CppParserTypeTest : public CppParserTestBase,
  public ::testing::WithParamInterface<TemplateDeclCase> {
protected:
  FwdRef<TypeEntity> parse_only_type() {
    parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
    auto ts = types(root());
    if (ts.size() != 1) {
      throw std::runtime_error("expected exactly one type, got " + std::to_string(ts.size()));
    }
    return ts.front();
  }
};

} // namespace

TEST_P(CppParserTypeTest, IsParsedAsType) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(types(root()).size(), 1u);
}

TEST_P(CppParserTypeTest, HasName) {
  EXPECT_EQ(parse_only_type()->get_name(), GetParam().name);
}

TEST_P(CppParserTypeTest, DeclarationExcludesBody) {
  EXPECT_EQ(parse_only_type()->get_decl(), GetParam().decl);
}

TEST_P(CppParserTypeTest, TemplateDeclaration) {
  EXPECT_EQ(parse_only_type()->get_template_decl(), GetParam().template_decl);
}

TEST_P(CppParserTypeTest, HasDocs) {
  EXPECT_EQ(parse_only_type()->get_docs(), "Doc.\n");
}

TEST_P(CppParserTypeTest, LineRangeCoversBody) {
  auto t = parse_only_type();
  EXPECT_EQ(t->get_start_line(), 1u);
  EXPECT_EQ(t->get_end_line(), 1u + GetParam().end_line);
}

INSTANTIATE_TEST_SUITE_P(Demo, CppParserTypeTest,
  ::testing::ValuesIn(TYPE_CASES), tcase_name);

TEST_F(CppParserTest, TemplateStartLineIsTemplateHeaderLine) {
  parse("/** Doc. */\ntemplate<class T>\n\nclass C {\n};\n");
  auto t = types(root());
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0]->get_start_line(), 1u);
  EXPECT_EQ(t[0]->get_end_line(), 4u);
}

TEST_F(CppParserTest, TemplateOnSameLineStartsOnThatLine) {
  parse("/** Doc. */\ntemplate<class T> T f(T x);\n");
  auto fns = functions(root());
  ASSERT_EQ(fns.size(), 1u);
  EXPECT_EQ(fns[0]->get_start_line(), 1u);
  EXPECT_EQ(fns[0]->get_end_line(), 1u);
}

TEST_F(CppParserTest, MemberTemplateStartLineIsTemplateHeaderLine) {
  parse("/** C. */\nclass C {\n  /** F. */\n  template<class T>\n  T f(T x);\n};\n");
  auto fns = functions(types(root())[0]);
  ASSERT_EQ(fns.size(), 1u);
  EXPECT_EQ(fns[0]->get_start_line(), 3u);
  EXPECT_EQ(fns[0]->get_end_line(), 4u);
}

TEST_F(CppParserTest, NonTemplateAfterTemplateIsUnaffected) {
  parse("/** T. */\ntemplate<class T>\nT f(T x);\n/** G. */\nint g();\n");
  auto fns = functions(root());
  ASSERT_EQ(fns.size(), 2u);
  EXPECT_EQ(fns[0]->get_start_line(), 1u);
  EXPECT_EQ(fns[1]->get_start_line(), 4u);
  EXPECT_EQ(fns[1]->get_template_decl(), "");
}

// ---------------------------------------------------------------------------
// Enumerators
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, EnumeratorsAreChildrenOfEnum) {
  parse(R"cpp(/** Test enumeration. */
enum Enum {
  /** First possibility. */
  FIRST,
  /** Second possibility. */
  SECOND,
  /** Third possibility. */
  THIRD
};
)cpp");
  auto e = types(root());
  ASSERT_EQ(e.size(), 1u);
  auto values = enums(e[0]);
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0]->get_name(), "FIRST");
  EXPECT_EQ(values[1]->get_name(), "SECOND");
  EXPECT_EQ(values[2]->get_name(), "THIRD");
}

TEST_F(CppParserTest, EnumeratorDeclarationIsItsName) {
  parse("/** E. */\nenum E {\n  /** First. */\n  FIRST\n};\n");
  auto values = enums(types(root())[0]);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0]->get_decl(), "FIRST");
}

TEST_F(CppParserTest, EnumeratorWithValueDeclarationIncludesValue) {
  parse("/** E. */\nenum E {\n  /** First. */\n  FIRST = 42\n};\n");
  auto values = enums(types(root())[0]);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0]->get_name(), "FIRST");
  EXPECT_EQ(values[0]->get_decl(), "FIRST = 42");
}

TEST_F(CppParserTest, EnumeratorForwardDocs) {
  parse("/** E. */\nenum E {\n  /**\n   * First possibility.\n   */\n  FIRST,\n};\n");
  auto values = enums(types(root())[0]);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0]->get_docs(), "First possibility.\n");
  EXPECT_EQ(values[0]->get_start_line(), 5u);
}

TEST_F(CppParserTest, EnumeratorsDocumentedAfter) {
  parse(R"cpp(/** Test enumeration with values documented after. */
enum EnumAfter {
  FIRST,   ///< First possibility, document after with `///` comment
  SECOND,  ///< Second possibility, document after with `///` comment
  THIRD    ///< Third possibility, document after with `///` comment
};
)cpp");
  auto values = enums(types(root())[0]);
  ASSERT_EQ(values.size(), 3u);
  EXPECT_EQ(values[0]->get_docs(), "First possibility, document after with `///` comment\n");
  EXPECT_EQ(values[1]->get_docs(), "Second possibility, document after with `///` comment\n");
  EXPECT_EQ(values[2]->get_docs(), "Third possibility, document after with `///` comment\n");
}

TEST_F(CppParserTest, EnumeratorsAreNotRootChildren) {
  parse("/** E. */\nenum E {\n  /** A. */\n  A\n};\n");
  EXPECT_EQ(count_root_children(), 1u);
}

TEST_F(CppParserTest, ScopedEnumeratorsAreChildrenOfEnum) {
  parse("/** E. */\nenum class E {\n  /** A. */\n  A,\n  /** B. */\n  B\n};\n");
  auto values = enums(types(root())[0]);
  ASSERT_EQ(values.size(), 2u);
  EXPECT_EQ(values[0]->get_name(), "A");
  EXPECT_EQ(values[1]->get_name(), "B");
}

// ---------------------------------------------------------------------------
// Type aliases and typedefs (demo/parsing.hpp, "Type Aliases")
// ---------------------------------------------------------------------------

namespace {

const TemplateDeclCase TYPEDEF_CASES[] = {
  {"Typedef", "typedef int TypeDef;", "TypeDef", "typedef int TypeDef", "", 0},
  {"TypeAlias", "using TypeAlias = int;", "TypeAlias", "using TypeAlias = int", "", 0},
  {"TypedefFunctionPointer", "typedef int (*TypeDefFunctionPointer)(int, int);", "TypeDefFunctionPointer",
    "typedef int (*TypeDefFunctionPointer)(int, int)", "", 0},
  {"TypeAliasFunctionPointer", "using TypeAliasFunctionPointer = int (*)(int, int);",
    "TypeAliasFunctionPointer", "using TypeAliasFunctionPointer = int (*)(int, int)", "", 0},
  {"TypeAliasTemplate", "template<class T>\nusing TypeAliasTemplate = int;", "TypeAliasTemplate",
    "template<class T> using TypeAliasTemplate = int", "template<class T>", 1},
};

class CppParserTypedefTest : public CppParserTestBase,
  public ::testing::WithParamInterface<TemplateDeclCase> {
protected:
  FwdRef<TypedefEntity> parse_only_typedef() {
    parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
    auto ts = typedefs(root());
    if (ts.size() != 1) {
      throw std::runtime_error("expected exactly one typedef, got " + std::to_string(ts.size()));
    }
    return ts.front();
  }
};

} // namespace

TEST_P(CppParserTypedefTest, IsParsedAsTypedefOnly) {
  parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
  EXPECT_EQ(typedefs(root()).size(), 1u);
  EXPECT_EQ(types(root()).size(), 0u);
  EXPECT_EQ(variables(root()).size(), 0u);
}

TEST_P(CppParserTypedefTest, HasName) {
  EXPECT_EQ(parse_only_typedef()->get_name(), GetParam().name);
}

TEST_P(CppParserTypedefTest, Declaration) {
  EXPECT_EQ(parse_only_typedef()->get_decl(), GetParam().decl);
}

TEST_P(CppParserTypedefTest, TemplateDeclaration) {
  EXPECT_EQ(parse_only_typedef()->get_template_decl(), GetParam().template_decl);
}

TEST_P(CppParserTypedefTest, HasDocs) {
  EXPECT_EQ(parse_only_typedef()->get_docs(), "Doc.\n");
}

TEST_P(CppParserTypedefTest, IsParsedAsMemberTypedef) {
  parse(std::string("/** Class. */\nclass C {\npublic:\n  /** Doc. */\n  ") + GetParam().code + "\n};\n");
  auto members = typedefs(types(root())[0]);
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(members[0]->get_name(), GetParam().name);
  EXPECT_EQ(members[0]->get_decl(), GetParam().decl);
  EXPECT_TRUE(typedefs(root()).empty());
}

INSTANTIATE_TEST_SUITE_P(Demo, CppParserTypedefTest,
  ::testing::ValuesIn(TYPEDEF_CASES), tcase_name);

// ---------------------------------------------------------------------------
// Concepts (demo/parsing.hpp, "Concepts")
// ---------------------------------------------------------------------------

namespace {

const TemplateDeclCase CONCEPT_CASES[] = {
  {"Concept", "template<class T>\nconcept Concept = std::is_arithmetic_v<T>;", "Concept",
    "template<class T> concept Concept = std::is_arithmetic_v<T>", "template<class T>", 1},
  {"TrivialConcept", "template<class T>\nconcept TrivialConcept = true;", "TrivialConcept",
    "template<class T> concept TrivialConcept = true", "template<class T>", 1},
};

class CppParserConceptTest : public CppParserTestBase,
  public ::testing::WithParamInterface<TemplateDeclCase> {
protected:
  FwdRef<ConceptEntity> parse_only_concept() {
    parse(std::string("/** Doc. */\n") + GetParam().code + "\n");
    auto cs = concepts(root());
    if (cs.size() != 1) {
      throw std::runtime_error("expected exactly one concept, got " + std::to_string(cs.size()));
    }
    return cs.front();
  }
};

} // namespace

TEST_P(CppParserConceptTest, HasName) {
  EXPECT_EQ(parse_only_concept()->get_name(), GetParam().name);
}

TEST_P(CppParserConceptTest, DeclarationIncludesConstraint) {
  EXPECT_EQ(parse_only_concept()->get_decl(), GetParam().decl);
}

TEST_P(CppParserConceptTest, IsAlwaysTemplate) {
  auto c = parse_only_concept();
  EXPECT_TRUE(c->is_template());
  EXPECT_EQ(c->get_template_decl(), GetParam().template_decl);
}

TEST_P(CppParserConceptTest, HasDocs) {
  EXPECT_EQ(parse_only_concept()->get_docs(), "Doc.\n");
}

TEST_P(CppParserConceptTest, LineRange) {
  auto c = parse_only_concept();
  EXPECT_EQ(c->get_start_line(), 1u);
  EXPECT_EQ(c->get_end_line(), 1u + GetParam().end_line);
}

INSTANTIATE_TEST_SUITE_P(Demo, CppParserConceptTest,
  ::testing::ValuesIn(CONCEPT_CASES), tcase_name);

// ---------------------------------------------------------------------------
// Macros (demo/parsing.hpp, "Macros")
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, ObjectLikeMacroName) {
  parse("/** Test macro. */\n#define MACRO 0\n");
  auto ms = macros(root());
  ASSERT_EQ(ms.size(), 1u);
  EXPECT_EQ(ms[0]->get_name(), "MACRO");
}

TEST_F(CppParserTest, ObjectLikeMacroDeclarationExcludesValue) {
  parse("/** Test macro. */\n#define MACRO 0\n");
  EXPECT_EQ(macros(root())[0]->get_decl(), "#define MACRO");
}

TEST_F(CppParserTest, ObjectLikeMacroDocs) {
  parse("/** Test macro. */\n#define MACRO 0\n");
  EXPECT_EQ(macros(root())[0]->get_docs(), "Test macro.\n");
}

TEST_F(CppParserTest, FunctionLikeMacroName) {
  parse("/** Test macro with arguments. */\n#define MACRO_WITH_ARGS(x, y) (x + y)\n");
  auto ms = macros(root());
  ASSERT_EQ(ms.size(), 1u);
  EXPECT_EQ(ms[0]->get_name(), "MACRO_WITH_ARGS");
}

TEST_F(CppParserTest, FunctionLikeMacroDeclarationIncludesArguments) {
  parse("/** Test macro with arguments. */\n#define MACRO_WITH_ARGS(x, y) (x + y)\n");
  EXPECT_EQ(macros(root())[0]->get_decl(), "#define MACRO_WITH_ARGS(x, y)");
}

TEST_F(CppParserTest, MacroLineRange) {
  parse("/** Test macro. */\n#define MACRO 0\n");
  auto m = macros(root())[0];
  EXPECT_EQ(m->get_start_line(), 1u);
  EXPECT_EQ(m->get_end_line(), 1u);
}

TEST_F(CppParserTest, MacroDocumentedAfterOnFollowingLine) {
  parse("#define MACRO_AFTER 0\n///< Test macro, documented after entity with `///` comment\n");
  auto ms = macros(root());
  ASSERT_EQ(ms.size(), 1u);
  EXPECT_EQ(ms[0]->get_docs(), "Test macro, documented after entity with `///` comment\n");
}

TEST_F(CppParserTest, FunctionLikeMacroDocumentedAfter) {
  parse("#define MACRO_WITH_ARGS_AFTER(x, y) (x + y)\n///< Documented after.\n");
  auto ms = macros(root());
  ASSERT_EQ(ms.size(), 1u);
  EXPECT_EQ(ms[0]->get_decl(), "#define MACRO_WITH_ARGS_AFTER(x, y)");
  EXPECT_EQ(ms[0]->get_docs(), "Documented after.\n");
}

TEST_F(CppParserTest, MacroWithoutValueIsNotAnEntity) {
  parse("/** Include guard. */\n#define GUARD\n");
  EXPECT_TRUE(macros(root()).empty());
}

TEST_F(CppParserTest, MacroBodyIsNotParsedForEntities) {
  parse("/** M. */\n#define M int hidden;\n");
  EXPECT_EQ(macros(root()).size(), 1u);
  EXPECT_TRUE(variables(root()).empty());
}

// ---------------------------------------------------------------------------
// Documentation comment styles (demo/parsing.hpp, variables a-e, d)
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, JavadocBlockCommentDocumentsFollowingEntity) {
  parse("/**\n * Documented with block comment.\n */\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documented with block comment.\n");
}

TEST_F(CppParserTest, QtBlockCommentDocumentsFollowingEntity) {
  parse("/*!\n * Documented with Qt block comment.\n */\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documented with Qt block comment.\n");
}

TEST_F(CppParserTest, TripleSlashCommentDocumentsFollowingEntity) {
  parse("/// Documented with `///` style preceding comment.\nint b;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documented with `///` style preceding comment.\n");
}

TEST_F(CppParserTest, QtLineCommentDocumentsFollowingEntity) {
  parse("//! Documented with Qt line comment.\nint b;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documented with Qt line comment.\n");
}

TEST_F(CppParserTest, ConsecutiveTripleSlashLinesAreMerged) {
  parse("/// First line\n/// second line.\nint c;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "First line\nsecond line.\n");
}

TEST_F(CppParserTest, ConsecutiveQtLinesAreMerged) {
  parse("//! First line\n//! second line.\nint c;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "First line\nsecond line.\n");
}

TEST_F(CppParserTest, TripleSlashMultiParagraph) {
  parse(R"cpp(///
/// Documented with `///` style preceding comment that wraps across multiple
/// lines and has multiple paragraphs.
///
///This is the second paragraph.
///
/// @ingroup parsing
///
/// This is the third paragraph.
///
int c;
)cpp");
  auto vars = variables(group("parsing"));
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_docs(),
    "Documented with `///` style preceding comment that wraps across multiple\n"
    "lines and has multiple paragraphs.\n\nThis is the second paragraph.\n\n\n\n"
    "This is the third paragraph.\n");
}

TEST_F(CppParserTest, TripleSlashBackwardCommentOnSameLine) {
  parse("int d;  ///< Documented after entity with `///` comment\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documented after entity with `///` comment\n");
}

TEST_F(CppParserTest, QtBackwardLineCommentOnSameLine) {
  parse("int d;  //!< Documented after.\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documented after.\n");
}

TEST_F(CppParserTest, JavadocBackwardBlockCommentOnSameLine) {
  parse("int d;  /**< Documented after. */\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documented after.\n");
}

TEST_F(CppParserTest, QtBackwardBlockCommentOnSameLine) {
  parse("int d;  /*!< Documented after. */\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documented after.\n");
}

TEST_F(CppParserTest, ConsecutiveBackwardLinesAreMerged) {
  parse("#define M 0\n///< First line\n///< second line.\n");
  auto ms = macros(root());
  ASSERT_EQ(ms.size(), 1u);
  EXPECT_EQ(ms[0]->get_docs(), "First line\nsecond line.\n");
}

TEST_F(CppParserTest, CommentWithStarAndSlashCharacters) {
  parse("/**\n * Documentation comment with * and / characters.\n */\nint e;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Documentation comment with * and / characters.\n");
}

TEST_F(CppParserTest, BlockCommentPreservesCodeIndentation) {
  parse(R"cpp(/**
 * Text.
 *
 *     int main();
 */
int whitespace1;
)cpp");
  EXPECT_EQ(only_root_variable()->get_docs(), "Text.\n\n    int main();\n");
}

TEST_F(CppParserTest, PlainLineCommentIsIgnored) {
  parse("// not documentation\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "");
  EXPECT_FALSE(only_root_variable()->is_visible());
}

TEST_F(CppParserTest, PlainBlockCommentIsIgnored) {
  parse("/* not documentation */\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "");
  EXPECT_FALSE(only_root_variable()->is_visible());
}

TEST_F(CppParserTest, PlainCommentBetweenDocAndEntityDoesNotBreakAssociation) {
  parse("/** Doc. */\n// plain\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Doc.\n");
}

TEST_F(CppParserTest, UndocumentedEntityIsInvisible) {
  parse("int a;\n");
  auto v = only_root_variable();
  EXPECT_EQ(v->get_docs(), "");
  EXPECT_FALSE(v->is_visible());
}

TEST_F(CppParserTest, DocumentedEntityIsVisible) {
  parse("/** Doc. */\nint a;\n");
  EXPECT_TRUE(only_root_variable()->is_visible());
}

TEST_F(CppParserTest, ForwardCommentDocumentsOnlyTheNextEntity) {
  parse("/** A. */\nint a;\nint b;\n");
  auto vars = variables(root());
  ASSERT_EQ(vars.size(), 2u);
  EXPECT_EQ(vars[0]->get_docs(), "A.\n");
  EXPECT_EQ(vars[1]->get_docs(), "");
}

TEST_F(CppParserTest, BackwardCommentDocumentsOnlyThePreviousEntity) {
  parse("int a; ///< A.\nint b;\n");
  auto vars = variables(root());
  ASSERT_EQ(vars.size(), 2u);
  EXPECT_EQ(vars[0]->get_docs(), "A.\n");
  EXPECT_EQ(vars[1]->get_docs(), "");
}

TEST_F(CppParserTest, BackwardCommentWithoutPrecedingEntityIsDiscarded) {
  parse("///< Orphan.\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "");
}

TEST_F(CppParserTest, LastOfTwoForwardBlockCommentsWins) {
  parse("/** First. */\n/** Second. */\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Second.\n");
}

TEST_F(CppParserTest, DifferentLineCommentStylesAreNotMerged) {
  parse("/// First.\n//! Second.\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Second.\n");
}

TEST_F(CppParserTest, TripleSlashBlocksSeparatedByBlankLineAreNotMerged) {
  parse("/// First.\n\n/// Second.\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_docs(), "Second.\n");
}

TEST_F(CppParserTest, ForwardAndBackwardCommentsOnConsecutiveEntities) {
  parse("/** A. */\nint a;\nint b; ///< B.\n/** C. */\nint c;\n");
  auto vars = variables(root());
  ASSERT_EQ(vars.size(), 3u);
  EXPECT_EQ(vars[0]->get_docs(), "A.\n");
  EXPECT_EQ(vars[1]->get_docs(), "B.\n");
  EXPECT_EQ(vars[2]->get_docs(), "C.\n");
}

TEST_F(CppParserTest, ForwardCommentLineDoesNotAffectEntityStartLine) {
  parse("/**\n * Doc.\n */\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_start_line(), 3u);
}

TEST_F(CppParserTest, ForwardAndBackwardCommentsForwardIsChosen) {
  std::string_view code = R""""(
/**
 * The Doc.
 */
int aaa; //< The Back Doc.
)"""";
  parse(code);
  EXPECT_EQ(only_root_variable()->get_name(), "aaa");
  EXPECT_EQ(only_root_variable()->get_docs(), "The Doc.\n");
}

// this is a disputable choice: giving priority **always** to the forward
// doc; we may in future decide to prioritize the closest doc.
TEST_F(CppParserTest, DistantForwardAndBackwardCommentsForwardIsChosen) {
  std::string_view code = R""""(
/**
 * The Doc 1.
 */



int aaa; //< The Back Doc.
)"""";
  parse(code);
  EXPECT_EQ(only_root_variable()->get_name(), "aaa");
  EXPECT_EQ(only_root_variable()->get_docs(), "The Doc 1.\n");
}

TEST_F(CppParserTest, MultipleForwardDocLastIsChosen) {
  std::string_view code = R""""(
/**
 * The Doc 1.
 */

/**
 * The Doc 2.
 */

///
/// The Doc 3.
///
int aaa; //< The Back Doc.
)"""";
  parse(code);
  EXPECT_EQ(only_root_variable()->get_name(), "aaa");
  EXPECT_EQ(only_root_variable()->get_docs(), "The Doc 3.\n");
}

// ---------------------------------------------------------------------------
// Groups (@ingroup)
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, InGroupMovesEntityToGroup) {
  parse("/**\n * Test variable.\n *\n * @ingroup parsing\n */\nint w;\n");
  auto vars = variables(group("parsing"));
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_name(), "w");
  EXPECT_TRUE(variables(root()).empty());
}

TEST_F(CppParserTest, InGroupIsRemovedFromDocs) {
  parse("/**\n * Test variable.\n *\n * @ingroup parsing\n */\nint w;\n");
  EXPECT_EQ(variables(group("parsing"))[0]->get_docs(), "Test variable.\n");
}

TEST_F(CppParserTest, InGroupIsRecordedOnEntity) {
  parse("/**\n * Test variable.\n *\n * @ingroup parsing\n */\nint w;\n");
  EXPECT_EQ(variables(group("parsing"))[0]->get_ingroup().view(), "parsing");
}

TEST_F(CppParserTest, InGroupInlineInTripleSlashComment) {
  parse("/// Documented with `///` style preceding comment. @ingroup parsing\nint b;\n");
  auto vars = variables(group("parsing"));
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_docs(), "Documented with `///` style preceding comment.\n");
}

TEST_F(CppParserTest, InGroupInBackwardComment) {
  parse("int d;  ///< Documented after entity with `///` comment @ingroup parsing\n");
  auto vars = variables(group("parsing"));
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_docs(), "Documented after entity with `///` comment\n");
}

TEST_F(CppParserTest, InGroupOnFollowingBackwardLine) {
  parse("#define MACRO_AFTER 0\n///< Test macro.\n///< @ingroup parsing\n");
  auto ms = macros(group("parsing"));
  ASSERT_EQ(ms.size(), 1u);
  EXPECT_EQ(ms[0]->get_docs(), "Test macro.\n");
  EXPECT_TRUE(macros(root()).empty());
}

TEST_F(CppParserTest, EntitiesWithoutInGroupStayAtRoot) {
  parse("/** A. */\nint a;\n");
  EXPECT_EQ(variables(root()).size(), 1u);
  EXPECT_TRUE(variables(group("parsing")).empty());
}

TEST_F(CppParserTest, UnknownGroupLeavesEntityAtRoot) {
  parse("/**\n * Doc.\n *\n * @ingroup nonexistent\n */\nint a;\n");
  EXPECT_EQ(variables(root()).size(), 1u);
}

TEST_F(CppParserTest, UnknownGroupIsDropped) {
  parse("/**\n * Doc.\n *\n * @ingroup nonexistent\n */\nint a;\n");
  EXPECT_EQ(only_root_variable()->get_ingroup().view(), "");
  EXPECT_EQ(only_root_variable()->get_docs(), "Doc.\n");
}

TEST_F(CppParserTest, InGroupInsideClassIsIgnored) {
  parse(R"cpp(/**
 * Class with members.
 *
 * @ingroup parsing
 */
class ClassWithMembers {
public:
  /**
   * Attempt to move a member out of its class.
   *
   * @ingroup parsing
   */
  int escapee;
};
)cpp");
  auto cls = types(group("parsing"));
  ASSERT_EQ(cls.size(), 1u);
  auto members = variables(cls[0]);
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(members[0]->get_name(), "escapee");
  EXPECT_EQ(members[0]->get_ingroup().view(), "");
  EXPECT_TRUE(variables(group("parsing")).empty());
}

TEST_F(CppParserTest, InGroupInsideNamespaceIsIgnored) {
  parse("/** N. */\nnamespace n {\n/**\n * V.\n *\n * @ingroup parsing\n */\nint v;\n}\n");
  auto ns = namespaces(root());
  ASSERT_EQ(ns.size(), 1u);
  auto vars = variables(ns[0]);
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_ingroup().view(), "");
  EXPECT_TRUE(variables(group("parsing")).empty());
}

TEST_F(CppParserTest, MembersOfGroupedClassStayInClass) {
  parse("/**\n * C.\n *\n * @ingroup parsing\n */\nclass C {\npublic:\n  /** M. */\n  int m;\n};\n");
  auto cls = types(group("parsing"));
  ASSERT_EQ(cls.size(), 1u);
  EXPECT_EQ(variables(cls[0]).size(), 1u);
  EXPECT_TRUE(variables(group("parsing")).empty());
}

TEST_F(CppParserTest, EntityInGroupAfterGroupedEntity) {
  parse("/** A.\n * @ingroup parsing\n */\nint a;\n/** B.\n * @ingroup presentation\n */\nint b;\n");
  auto p = variables(group("parsing"));
  auto q = variables(group("presentation"));
  ASSERT_EQ(p.size(), 1u);
  ASSERT_EQ(q.size(), 1u);
  EXPECT_EQ(p[0]->get_name(), "a");
  EXPECT_EQ(q[0]->get_name(), "b");
}

// ---------------------------------------------------------------------------
// Class members and nesting (demo/parsing.hpp, ClassWithMembers)
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, InnerClassIsChildOfOuterClass) {
  parse("/** Outer. */\nclass Outer {\npublic:\n  /** Inner class. */\n  class InnerClass {\n  };\n};\n");
  auto outer = types(root());
  ASSERT_EQ(outer.size(), 1u);
  auto inner = types(outer[0]);
  ASSERT_EQ(inner.size(), 1u);
  EXPECT_EQ(inner[0]->get_name(), "InnerClass");
  EXPECT_EQ(inner[0]->get_docs(), "Inner class.\n");
}

TEST_F(CppParserTest, InnerClassMemberIsChildOfInnerClass) {
  parse(R"cpp(/** Outer. */
class Outer {
public:
  /** Inner class. */
  class InnerClass {
    /** Test member variable of inner class. */
    int w;
  };
};
)cpp");
  auto inner = types(types(root())[0])[0];
  auto vars = variables(inner);
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_name(), "w");
  EXPECT_EQ(vars[0]->get_docs(), "Test member variable of inner class.\n");
  EXPECT_TRUE(variables(types(root())[0]).empty());
}

TEST_F(CppParserTest, InnerEnumIsChildOfClass) {
  parse("/** C. */\nclass C {\npublic:\n  /** Inner enumeration. */\n  enum InnerEnum {\n    /** A. */\n    A\n  };\n};\n");
  auto inner = types(types(root())[0]);
  ASSERT_EQ(inner.size(), 1u);
  EXPECT_EQ(inner[0]->get_name(), "InnerEnum");
  auto values = enums(inner[0]);
  ASSERT_EQ(values.size(), 1u);
  EXPECT_EQ(values[0]->get_name(), "A");
}

TEST_F(CppParserTest, ClassTemplateMemberIsChildOfTemplate) {
  parse("/** T. */\ntemplate<class T>\nclass ClassTemplateWithMembers {\npublic:\n  /** Test member variable. */\n  int w;\n};\n");
  auto cls = types(root());
  ASSERT_EQ(cls.size(), 1u);
  EXPECT_EQ(cls[0]->get_template_decl(), "template<class T>");
  auto vars = variables(cls[0]);
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_name(), "w");
}

TEST_F(CppParserTest, EntityAfterClassIsNotAMember) {
  parse("/** C. */\nclass C {\n  /** M. */\n  int m;\n};\n/** After. */\nint after;\n");
  auto vars = variables(root());
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_name(), "after");
  EXPECT_EQ(variables(types(root())[0]).size(), 1u);
}

TEST_F(CppParserTest, MembersKeepSourceOrderPerKind) {
  parse("/** C. */\nclass C {\n  /** X. */\n  int x;\n  /** F. */\n  int f();\n  /** Y. */\n  int y;\n};\n");
  auto cls = types(root())[0];
  auto vars = variables(cls);
  ASSERT_EQ(vars.size(), 2u);
  EXPECT_EQ(vars[0]->get_name(), "x");
  EXPECT_EQ(vars[1]->get_name(), "y");
  ASSERT_EQ(functions(cls).size(), 1u);
}

TEST_F(CppParserTest, UndocumentedMemberIsInvisible) {
  parse("/** C. */\nclass C {\n  int m;\n};\n");
  auto vars = variables(types(root())[0]);
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_FALSE(vars[0]->is_visible());
}

TEST_F(CppParserTest, LocalVariableInFunctionBodyIsInvisibleChildOfFunction) {
  parse("/** F. */\ninline int f() {\n  int local = 0;\n  return local;\n}\n");
  auto fns = functions(root());
  ASSERT_EQ(fns.size(), 1u);
  auto locals = variables(fns[0]);
  ASSERT_EQ(locals.size(), 1u);
  EXPECT_EQ(locals[0]->get_name(), "local");
  EXPECT_FALSE(locals[0]->is_visible());
  EXPECT_TRUE(variables(root()).empty());
}

TEST_F(CppParserTest, LocalVariableInOperatorBodyIsChildOfOperator) {
  parse("/** C. */\nclass C {\n  /** Op. */\n  inline int operator+(int x) {\n    int t = x;\n    return t;\n  }\n};\n");
  auto cls = types(root())[0];
  auto ops = operators(cls);
  ASSERT_EQ(ops.size(), 1u);
  auto locals = variables(ops[0]);
  ASSERT_EQ(locals.size(), 1u);
  EXPECT_EQ(locals[0]->get_name(), "t");
  EXPECT_TRUE(variables(cls).empty());
}

TEST_F(CppParserTest, TypeDefinedInVariableDeclarationKeepsItsMembers) {
  parse("/** S. */\nstruct S {\n  /** A. */\n  int a;\n} s;\n/** T. */\nint t;\n");
  auto ts = types(root());
  ASSERT_EQ(ts.size(), 1u);
  EXPECT_EQ(ts[0]->get_name(), "S");
  EXPECT_EQ(ts[0]->get_docs(), "S.\n");
  auto members = variables(ts[0]);
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(members[0]->get_name(), "a");
  auto vars = variables(root());
  ASSERT_EQ(vars.size(), 2u);
  EXPECT_EQ(vars[0]->get_name(), "s");
  EXPECT_EQ(vars[1]->get_name(), "t");
}

TEST_F(CppParserTest, TypeAndVariableInSameDeclarationShareNamespaceParent) {
  parse("/** N. */\nnamespace n {\n/** S. */\nstruct S {\n  /** A. */\n  int a;\n} s;\n}\n");
  auto ns = namespaces(root());
  ASSERT_EQ(ns.size(), 1u);
  auto ts = types(ns[0]);
  ASSERT_EQ(ts.size(), 1u);
  EXPECT_EQ(ts[0]->get_name(), "S");
  EXPECT_EQ(variables(ts[0]).size(), 1u);
  auto vars = variables(ns[0]);
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_name(), "s");
  EXPECT_TRUE(types(root()).empty());
  EXPECT_TRUE(variables(root()).empty());
}

TEST_F(CppParserTest, TypeAndVariableInSameDeclarationShareClassParent) {
  parse("/** C. */\nclass C {\n  /** S. */\n  struct S {\n    /** A. */\n    int a;\n  } s;\n};\n");
  auto cls = types(root());
  ASSERT_EQ(cls.size(), 1u);
  auto inner = types(cls[0]);
  ASSERT_EQ(inner.size(), 1u);
  EXPECT_EQ(inner[0]->get_name(), "S");
  auto members = variables(inner[0]);
  ASSERT_EQ(members.size(), 1u);
  EXPECT_EQ(members[0]->get_name(), "a");
  auto vars = variables(cls[0]);
  ASSERT_EQ(vars.size(), 1u);
  EXPECT_EQ(vars[0]->get_name(), "s");
}

TEST_F(CppParserTest, SeveralVariablesDeclaredWithTypeAreSiblingsOfIt) {
  parse("/** S. */\nstruct S {\n  /** A. */\n  int a;\n} s1, s2;\n");
  auto ts = types(root());
  ASSERT_EQ(ts.size(), 1u);
  EXPECT_EQ(variables(ts[0]).size(), 1u);
  auto vars = variables(root());
  ASSERT_EQ(vars.size(), 2u);
  EXPECT_EQ(vars[0]->get_name(), "s1");
  EXPECT_EQ(vars[1]->get_name(), "s2");
}

// TODO(issue #6: lambdas): suport matching lambdas.
// // ---------------------------------------------------------------------------
// // Lambdas
// // ---------------------------------------------------------------------------
//
// namespace {
//
// struct LambdaCase {
//   const char* label;
//   const char* code;   // a lambda expression
//   const char* decl;
// };
//
// void PrintTo(const LambdaCase& c, std::ostream* os) { *os << c.label; }
//
// std::string lcase_name(const ::testing::TestParamInfo<LambdaCase>& info) {
//   return info.param.label;
// }
//
// const LambdaCase LAMBDA_CASES[] = {
//   {"NoParameters", "[] { return 0; }", "[]"},
//   {"EmptyParameters", "[]() { return 0; }", "[]()"},
//   {"Parameters", "[](int x, int y) { return x + y; }", "[](int x, int y)"},
//   {"CaptureByReference", "[&](int x) { return x; }", "[&](int x)"},
//   {"CaptureList", "[=, &y](int x) { return x + y; }", "[=, &y](int x)"},
//   {"TrailingReturnType", "[](int x) -> int { return x; }", "[](int x) -> int"},
//   {"Mutable", "[n = 0]() mutable { return ++n; }", "[n = 0]() mutable"},
//   {"Generic", "[](auto x) { return x; }", "[](auto x)"},
//   {"TemplateParameters", "[]<class T>(T x) { return x; }", "[]<class T>(T x)"},
// };
//
// class CppParserLambdaTest : public CppParserTestBase,
//   public ::testing::WithParamInterface<LambdaCase> {
// protected:
//   FwdRef<LambdaEntity> parse_only_lambda() {
//     parse(std::string("/** V. */\nauto v = ") + GetParam().code + ";\n");
//     auto ls = lambdas(only_root_variable());
//     if (ls.size() != 1) {
//       throw std::runtime_error("expected exactly one lambda, got " + std::to_string(ls.size()));
//     }
//     return ls.front();
//   }
// };
//
// } // namespace
//
// TEST_P(CppParserLambdaTest, IsChildOfInitializedVariable) {
//   parse(std::string("/** V. */\nauto v = ") + GetParam().code + ";\n");
//   EXPECT_EQ(lambdas(only_root_variable()).size(), 1u);
//   EXPECT_TRUE(lambdas(root()).empty());
// }
//
// TEST_P(CppParserLambdaTest, IsNamedAfterVariable) {
//   EXPECT_EQ(parse_only_lambda()->get_name(), "v");
// }
//
// TEST_P(CppParserLambdaTest, DeclarationEndsBeforeBody) {
//   EXPECT_EQ(parse_only_lambda()->get_decl(), GetParam().decl);
// }
//
// TEST_P(CppParserLambdaTest, IsNotTemplate) {
//   EXPECT_FALSE(parse_only_lambda()->is_template());
// }
//
// TEST_P(CppParserLambdaTest, IsUndocumented) {
//   auto l = parse_only_lambda();
//   EXPECT_EQ(l->get_docs(), "");
//   EXPECT_FALSE(l->is_visible());
// }
//
// TEST_P(CppParserLambdaTest, VariableKeepsItsDocs) {
//   parse(std::string("/** V. */\nauto v = ") + GetParam().code + ";\n");
//   EXPECT_EQ(only_root_variable()->get_docs(), "V.\n");
//   EXPECT_EQ(only_root_variable()->get_decl(), "auto v");
// }
//
// INSTANTIATE_TEST_SUITE_P(Forms, CppParserLambdaTest,
//   ::testing::ValuesIn(LAMBDA_CASES), lcase_name);
//
// TEST_F(CppParserTest, LambdaLineRange) {
//   parse("/** V. */\nauto v = [](int x) {\n  return x;\n};\n");
//   auto l = lambdas(only_root_variable())[0];
//   EXPECT_EQ(l->get_start_line(), 1u);
//   EXPECT_EQ(l->get_end_line(), 3u);
// }
//
// TEST_F(CppParserTest, LambdaIsDocumentedByInlinePrecedingComment) {
//   parse("auto v = /** L. */ [](int x) { return x; };\n");
//   auto l = lambdas(only_root_variable())[0];
//   EXPECT_EQ(l->get_docs(), "L.\n");
//   EXPECT_TRUE(l->is_visible());
//   EXPECT_EQ(only_root_variable()->get_docs(), "");
// }
//
// TEST_F(CppParserTest, BackwardCommentDocumentsVariableNotLambda) {
//   parse("auto v = [](int x) { return x; }; ///< V.\n");
//   auto v = only_root_variable();
//   EXPECT_EQ(v->get_docs(), "V.\n");
//   EXPECT_TRUE(v->is_visible());
//   EXPECT_EQ(lambdas(v)[0]->get_docs(), "");
// }
//
// TEST_F(CppParserTest, BackwardCommentOnNextLineDocumentsVariable) {
//   parse("auto v = [](int x) {\n  return x;\n};\n///< V.\n");
//   EXPECT_EQ(only_root_variable()->get_docs(), "V.\n");
// }
//
// TEST_F(CppParserTest, BackwardCommentOfNextVariableIsNotStolen) {
//   parse("auto v = [] { return 0; };\nint w; ///< W.\n");
//   auto vars = variables(root());
//   ASSERT_EQ(vars.size(), 2u);
//   EXPECT_EQ(vars[0]->get_docs(), "");
//   EXPECT_EQ(vars[1]->get_docs(), "W.\n");
// }
//
// TEST_F(CppParserTest, BackwardCommentInsideLambdaDocumentsLocal) {
//   parse("auto v = [] {\n  int x = 0; ///< X.\n  return x;\n};\n");
//   auto v = only_root_variable();
//   EXPECT_EQ(v->get_docs(), "");
//   auto locals = variables(lambdas(v)[0]);
//   ASSERT_EQ(locals.size(), 1u);
//   EXPECT_EQ(locals[0]->get_docs(), "X.\n");
// }
//
// TEST_F(CppParserTest, InGroupOnLambdaIsIgnored) {
//   parse("auto v = /** L.\n * @ingroup parsing\n */ [] { return 0; };\n");
//   auto ls = lambdas(only_root_variable());
//   ASSERT_EQ(ls.size(), 1u);
//   EXPECT_EQ(ls[0]->get_ingroup().view(), "");
//   EXPECT_EQ(ls[0]->get_docs(), "L.\n");
// }
//
// TEST_F(CppParserTest, LambdaLocalsAreChildrenOfLambda) {
//   parse("/** V. */\nauto v = [](int x) { int inner = x; return inner; };\n/** W. */\nint w;\n");
//   auto vars = variables(root());
//   ASSERT_EQ(vars.size(), 2u);
//   EXPECT_EQ(vars[0]->get_name(), "v");
//   EXPECT_EQ(vars[1]->get_name(), "w");
//   auto locals = variables(lambdas(vars[0])[0]);
//   ASSERT_EQ(locals.size(), 1u);
//   EXPECT_EQ(locals[0]->get_name(), "inner");
//   EXPECT_FALSE(locals[0]->is_visible());
// }
//
// TEST_F(CppParserTest, DocCommentInsideLambdaDocumentsLocal) {
//   parse("auto v = [] {\n  /** Inner. */\n  int inner = 0;\n  return inner;\n};\nint w;\n");
//   auto vars = variables(root());
//   ASSERT_EQ(vars.size(), 2u);
//   EXPECT_EQ(vars[1]->get_name(), "w");
//   EXPECT_EQ(vars[1]->get_docs(), "");
//   auto locals = variables(lambdas(vars[0])[0]);
//   ASSERT_EQ(locals.size(), 1u);
//   EXPECT_EQ(locals[0]->get_docs(), "Inner.\n");
// }
//
// TEST_F(CppParserTest, ClassDefinedInsideLambdaIsChildOfLambda) {
//   parse("/** V. */\nauto v = [] {\n  /** Local. */\n  struct Local { int m; };\n  return Local{};\n};\n");
//   EXPECT_TRUE(types(root()).empty());
//   auto ts = types(lambdas(only_root_variable())[0]);
//   ASSERT_EQ(ts.size(), 1u);
//   EXPECT_EQ(ts[0]->get_name(), "Local");
//   EXPECT_EQ(variables(ts[0]).size(), 1u);
// }
//
// TEST_F(CppParserTest, NestedLambdaIsChildOfLocalVariable) {
//   parse("/** V. */\nauto v = [] {\n  int a = 0;\n  auto g = [] { int b = 0; return b; };\n  int c = g();\n  return a + c;\n};\n/** W. */\nint w;\n");
//   auto vars = variables(root());
//   ASSERT_EQ(vars.size(), 2u);
//   auto outer = lambdas(vars[0]);
//   ASSERT_EQ(outer.size(), 1u);
//   EXPECT_EQ(outer[0]->get_name(), "v");
//   auto locals = variables(outer[0]);
//   ASSERT_EQ(locals.size(), 3u);
//   EXPECT_EQ(locals[0]->get_name(), "a");
//   EXPECT_EQ(locals[1]->get_name(), "g");
//   EXPECT_EQ(locals[2]->get_name(), "c");
//   auto inner = lambdas(locals[1]);
//   ASSERT_EQ(inner.size(), 1u);
//   EXPECT_EQ(inner[0]->get_name(), "g");
//   auto inner_locals = variables(inner[0]);
//   ASSERT_EQ(inner_locals.size(), 1u);
//   EXPECT_EQ(inner_locals[0]->get_name(), "b");
// }
//
// TEST_F(CppParserTest, ConsecutiveLambdaVariablesEachOwnTheirLambda) {
//   parse("/** A. */\nauto a = [] { int x = 0; return x; };\n/** B. */\nauto b = [] { int y = 0; return y; };\n");
//   auto vars = variables(root());
//   ASSERT_EQ(vars.size(), 2u);
//   EXPECT_EQ(vars[0]->get_docs(), "A.\n");
//   EXPECT_EQ(vars[1]->get_docs(), "B.\n");
//   ASSERT_EQ(lambdas(vars[0]).size(), 1u);
//   ASSERT_EQ(lambdas(vars[1]).size(), 1u);
//   EXPECT_EQ(lambdas(vars[0])[0]->get_name(), "a");
//   EXPECT_EQ(lambdas(vars[1])[0]->get_name(), "b");
// }
//
// TEST_F(CppParserTest, MemberLambdaIsChildOfMemberVariable) {
//   parse("/** C. */\nclass C {\n  /** V. */\n  int (*v)(int) = [](int x) { int inner = x; return inner; };\n  /** W. */\n  int w;\n};\n");
//   auto cls = types(root())[0];
//   auto vars = variables(cls);
//   ASSERT_EQ(vars.size(), 2u);
//   EXPECT_EQ(vars[0]->get_name(), "v");
//   EXPECT_EQ(vars[1]->get_name(), "w");
//   auto ls = lambdas(vars[0]);
//   ASSERT_EQ(ls.size(), 1u);
//   EXPECT_EQ(ls[0]->get_name(), "v");
//   EXPECT_EQ(variables(ls[0]).size(), 1u);
//   EXPECT_TRUE(lambdas(cls).empty());
// }
//
// TEST_F(CppParserTest, LambdaArgumentIsUnnamedChildOfFunction) {
//   parse("/** F. */\ninline int f() {\n  return call([](int x) { return x; });\n}\n");
//   auto fns = functions(root());
//   ASSERT_EQ(fns.size(), 1u);
//   auto ls = lambdas(fns[0]);
//   ASSERT_EQ(ls.size(), 1u);
//   EXPECT_EQ(ls[0]->get_name(), "");
//   EXPECT_EQ(ls[0]->get_decl(), "[](int x)");
// }
//
// TEST_F(CppParserTest, LambdaInLocalVariableOfFunction) {
//   parse("/** F. */\ninline int f() {\n  auto l = [](int x) { int inner = x; return inner; };\n  int after = l(0);\n  return after;\n}\n");
//   auto fns = functions(root());
//   ASSERT_EQ(fns.size(), 1u);
//   auto locals = variables(fns[0]);
//   ASSERT_EQ(locals.size(), 2u);
//   EXPECT_EQ(locals[0]->get_name(), "l");
//   EXPECT_EQ(locals[1]->get_name(), "after");
//   EXPECT_EQ(lambdas(locals[0]).size(), 1u);
//   EXPECT_TRUE(lambdas(fns[0]).empty());
//   EXPECT_TRUE(variables(root()).empty());
// }
//
// TEST_F(CppParserTest, LambdaAtNamespaceScopeIsUnnamedChild) {
//   parse("/** N. */\nnamespace n {\nstatic_assert([] { return true; }());\n}\n");
//   auto ns = namespaces(root());
//   ASSERT_EQ(ns.size(), 1u);
//   auto ls = lambdas(ns[0]);
//   ASSERT_EQ(ls.size(), 1u);
//   EXPECT_EQ(ls[0]->get_name(), "");
//   EXPECT_EQ(ls[0]->get_decl(), "[]");
// }
//
// TEST_F(CppParserTest, TemplateVariableWithLambda) {
//   parse("/** V. */\ntemplate<class T>\nauto v = [](T x) { return x; };\n");
//   auto v = only_root_variable();
//   EXPECT_EQ(v->get_template_decl(), "template<class T>");
//   EXPECT_EQ(v->get_docs(), "V.\n");
//   auto ls = lambdas(v);
//   ASSERT_EQ(ls.size(), 1u);
//   EXPECT_EQ(ls[0]->get_decl(), "[](T x)");
//   EXPECT_FALSE(ls[0]->is_template());
// }
//
// TEST_F(CppParserTest, RemovingFileRemovesLambdas) {
//   auto file = parse("/** V. */\nauto v = [] { return 0; };\n");
//   ASSERT_EQ(lambdas(only_root_variable()).size(), 1u);
//   registry.remove_file(file->get_path());
//   EXPECT_EQ(count_root_children(), 0u);
// }

// ---------------------------------------------------------------------------
// Namespaces (demo/parsing.hpp, ns1)
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, NamespaceIsChildOfRoot) {
  parse("/** Namespace `ns1`. */\nnamespace ns1 {\n}\n");
  auto ns = namespaces(root());
  ASSERT_EQ(ns.size(), 1u);
  EXPECT_EQ(ns[0]->get_name(), "ns1");
}

TEST_F(CppParserTest, NamespaceDeclaration) {
  parse("/** Namespace `ns1`. */\nnamespace ns1 {\n}\n");
  EXPECT_EQ(namespaces(root())[0]->get_decl(), "namespace ns1");
}

TEST_F(CppParserTest, NamespaceFullNameAtRoot) {
  parse("/** Namespace `ns1`. */\nnamespace ns1 {\n}\n");
  EXPECT_EQ(namespaces(root())[0]->get_full_name(), "ns1");
}

TEST_F(CppParserTest, NamespaceDocs) {
  parse("/** Namespace `ns1`. */\nnamespace ns1 {\n}\n");
  EXPECT_EQ(namespaces(root())[0]->get_docs(), "Namespace `ns1`.\n");
  EXPECT_TRUE(namespaces(root())[0]->is_visible());
}

TEST_F(CppParserTest, UndocumentedNamespaceIsInvisible) {
  parse("namespace ns1 {\n}\n");
  auto ns = namespaces(root());
  ASSERT_EQ(ns.size(), 1u);
  EXPECT_FALSE(ns[0]->is_visible());
}

TEST_F(CppParserTest, NamespaceLineRange) {
  parse("/** N. */\nnamespace ns1 {\n  //\n}\n");
  auto ns = namespaces(root())[0];
  EXPECT_EQ(ns->get_start_line(), 1u);
  EXPECT_EQ(ns->get_end_line(), 3u);
}

TEST_F(CppParserTest, NamespaceIsRegisteredByFullName) {
  parse("/** N. */\nnamespace ns1 {\n}\n");
  auto ns = registry.get_namespace("ns1");
  ASSERT_TRUE(ns.has_value());
  EXPECT_EQ((*ns)->get_name(), "ns1");
}

TEST_F(CppParserTest, NestedNamespaceIsChildOfOuter) {
  parse("/** ns1. */\nnamespace ns1 {\n  /** ns2a. */\n  namespace ns2a {\n  }\n}\n");
  auto outer = namespaces(root());
  ASSERT_EQ(outer.size(), 1u);
  auto inner = namespaces(outer[0]);
  ASSERT_EQ(inner.size(), 1u);
  EXPECT_EQ(inner[0]->get_name(), "ns2a");
  EXPECT_EQ(inner[0]->get_decl(), "namespace ns2a");
}

TEST_F(CppParserTest, NestedNamespaceFullName) {
  parse("/** ns1. */\nnamespace ns1 {\n  /** ns2a. */\n  namespace ns2a {\n  }\n}\n");
  EXPECT_EQ(namespaces(namespaces(root())[0])[0]->get_full_name(), "ns1::ns2a");
  EXPECT_TRUE(registry.get_namespace("ns1::ns2a").has_value());
}

TEST_F(CppParserTest, NestedNamespaceSpecifierCreatesHierarchy) {
  parse("/** Nested. */\nnamespace a::b {\n}\n");
  auto outer = namespaces(root());
  ASSERT_EQ(outer.size(), 1u);
  EXPECT_EQ(outer[0]->get_name(), "a");
  auto inner = namespaces(outer[0]);
  ASSERT_EQ(inner.size(), 1u);
  EXPECT_EQ(inner[0]->get_name(), "b");
}

TEST_F(CppParserTest, NestedNamespaceSpecifierFullNames) {
  parse("/** Nested. */\nnamespace a::b {\n}\n");
  auto outer = namespaces(root())[0];
  EXPECT_EQ(outer->get_full_name(), "a");
  EXPECT_EQ(namespaces(outer)[0]->get_full_name(), "a::b");
}

TEST_F(CppParserTest, NestedNamespaceSpecifierDeclarations) {
  parse("/** Nested. */\nnamespace a::b {\n}\n");
  auto outer = namespaces(root())[0];
  EXPECT_EQ(outer->get_decl(), "namespace a");
  EXPECT_EQ(namespaces(outer)[0]->get_decl(), "namespace a::b");
}

TEST_F(CppParserTest, NestedNamespaceSpecifierDocsGoToInnermost) {
  parse("/** Nested. */\nnamespace a::b {\n}\n");
  auto outer = namespaces(root())[0];
  EXPECT_EQ(outer->get_docs(), "");
  EXPECT_FALSE(outer->is_visible());
  auto inner = namespaces(outer)[0];
  EXPECT_EQ(inner->get_docs(), "Nested.\n");
  EXPECT_TRUE(inner->is_visible());
}

TEST_F(CppParserTest, NestedNamespaceSpecifierReusesExistingNamespace) {
  parse(R"cpp(/** Namespace `ns1`. */
namespace ns1 {
}

/** Namespace `ns1::ns2b` declared with nested namespace specifier. */
namespace ns1::ns2b {
}
)cpp");
  auto ns = namespaces(root());
  ASSERT_EQ(ns.size(), 1u);
  EXPECT_EQ(ns[0]->get_docs(), "Namespace `ns1`.\n");
  auto inner = namespaces(ns[0]);
  ASSERT_EQ(inner.size(), 1u);
  EXPECT_EQ(inner[0]->get_full_name(), "ns1::ns2b");
  EXPECT_EQ(inner[0]->get_docs(), "Namespace `ns1::ns2b` declared with nested namespace specifier.\n");
}

TEST_F(CppParserTest, ThreeLevelNestedNamespaceSpecifierReusesTwoLevels) {
  parse(R"cpp(/** ns1. */
namespace ns1 {
  /** ns2a. */
  namespace ns2a {
  }
}

/** Namespace `ns1::ns2a::ns3` declared with nested namespace specifier. */
namespace ns1::ns2a::ns3 {
}
)cpp");
  auto ns1 = namespaces(root());
  ASSERT_EQ(ns1.size(), 1u);
  auto ns2 = namespaces(ns1[0]);
  ASSERT_EQ(ns2.size(), 1u);
  auto ns3 = namespaces(ns2[0]);
  ASSERT_EQ(ns3.size(), 1u);
  EXPECT_EQ(ns3[0]->get_name(), "ns3");
  EXPECT_EQ(ns3[0]->get_full_name(), "ns1::ns2a::ns3");
  EXPECT_EQ(ns3[0]->get_decl(), "namespace ns1::ns2a::ns3");
}

TEST_F(CppParserTest, EntitiesInsideNamespaceAreChildren) {
  parse("/** N. */\nnamespace n {\n/** V. */\nint v;\n/** F. */\nint f();\n/** C. */\nclass C {};\n}\n");
  auto ns = namespaces(root())[0];
  EXPECT_EQ(variables(ns).size(), 1u);
  EXPECT_EQ(functions(ns).size(), 1u);
  EXPECT_EQ(types(ns).size(), 1u);
  EXPECT_TRUE(variables(root()).empty());
  EXPECT_TRUE(functions(root()).empty());
  EXPECT_TRUE(types(root()).empty());
}

TEST_F(CppParserTest, EntitiesInsideNestedSpecifierNamespaceGoToInnermost) {
  parse("/** N. */\nnamespace a::b {\n/** V. */\nint v;\n}\n");
  auto a = namespaces(root())[0];
  auto b = namespaces(a)[0];
  EXPECT_TRUE(variables(a).empty());
  ASSERT_EQ(variables(b).size(), 1u);
  EXPECT_EQ(variables(b)[0]->get_name(), "v");
}

TEST_F(CppParserTest, EntityAfterNamespaceIsAtRoot) {
  parse("/** N. */\nnamespace n {\n}\n/** V. */\nint v;\n");
  EXPECT_EQ(variables(root()).size(), 1u);
  EXPECT_TRUE(variables(namespaces(root())[0]).empty());
}

// ---------------------------------------------------------------------------
// Preprocessor defines
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, DefineReplacesIdentifierInContent) {
  auto file = parse("INT a;\n", {{"INT", "int"}});
  EXPECT_EQ(file->get_content(), "int a;\n");
}

TEST_F(CppParserTest, DefineReplacementIsVisibleInDeclaration) {
  parse("/** A. */\nINT a;\n", {{"INT", "int"}});
  EXPECT_EQ(only_root_variable()->get_decl(), "int a");
}

TEST_F(CppParserTest, DefineToEmptyRemovesAnnotation) {
  parse("/** F. */\nint EXPORT f(int x);\n", {{"EXPORT", ""}});
  auto fns = functions(root());
  ASSERT_EQ(fns.size(), 1u);
  EXPECT_EQ(fns[0]->get_name(), "f");
  EXPECT_EQ(fns[0]->get_decl(), "int  f(int x)");
}

TEST_F(CppParserTest, DefineReplacesAllOccurrences) {
  auto file = parse("INT a;\nINT b;\n", {{"INT", "int"}});
  EXPECT_EQ(file->get_content(), "int a;\nint b;\n");
}

TEST_F(CppParserTest, DefineIsNotAppliedInsideMacroDefinitions) {
  auto file = parse("#define M INT\nINT a;\n", {{"INT", "int"}});
  EXPECT_EQ(file->get_content(), "#define M INT\nint a;\n");
}

TEST_F(CppParserTest, NoDefinesLeavesContentUnchanged) {
  constexpr std::string_view code = "INT a;\n";
  auto file = parse(code);
  EXPECT_EQ(file->get_content(), code);
}

// ---------------------------------------------------------------------------
// Coverage line counts
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, WithoutCoverageLineCountsAreEmpty) {
  auto file = parse("int f() {\n  return 0;\n}\n");
  EXPECT_TRUE(std::as_const(*file).get_line_counts().empty());
  EXPECT_EQ(file->get_included_lines(), 0u);
}

TEST_F(CppParserTest, CoverageMarksReturnStatementLine) {
  auto file = parse("int f() {\n  return 0;\n}\n", {}, true);
  const auto& counts = std::as_const(*file).get_line_counts();
  ASSERT_EQ(counts.size(), 3u);
  EXPECT_EQ(counts[0], -1);
  EXPECT_EQ(counts[1], 0);
  EXPECT_EQ(counts[2], -1);
  EXPECT_EQ(file->get_included_lines(), 1u);
}

TEST_F(CppParserTest, CoverageMarksCallExpressionLine) {
  auto file = parse("void g();\nvoid f() {\n  g();\n}\n", {}, true);
  const auto& counts = std::as_const(*file).get_line_counts();
  ASSERT_EQ(counts.size(), 4u);
  EXPECT_EQ(counts[2], 0);
  EXPECT_EQ(file->get_included_lines(), 1u);
}

TEST_F(CppParserTest, CoverageCountsEachLineOnce) {
  auto file = parse("int f(int x) {\n  return x + x * x;\n}\n", {}, true);
  EXPECT_EQ(file->get_included_lines(), 1u);
}

TEST_F(CppParserTest, CoverageMarksMultiLineStatement) {
  auto file = parse("int f(int x) {\n  return x +\n    x;\n}\n", {}, true);
  const auto& counts = std::as_const(*file).get_line_counts();
  ASSERT_EQ(counts.size(), 4u);
  EXPECT_EQ(counts[1], 0);
  EXPECT_EQ(counts[2], 0);
  EXPECT_EQ(file->get_included_lines(), 2u);
}

TEST_F(CppParserTest, CoverageExcludesDeclarationsWithoutCode) {
  auto file = parse("int a;\nint f(int x);\nstruct S { int m; };\n", {}, true);
  EXPECT_EQ(file->get_included_lines(), 0u);
}

TEST_F(CppParserTest, CoverageExcludesDefaultArgumentExpressions) {
  auto file = parse("int f(int x = 1 + 2);\n", {}, true);
  EXPECT_EQ(file->get_included_lines(), 0u);
}

TEST_F(CppParserTest, CoverageExcludesStaticAssert) {
  auto file = parse("static_assert(1 + 1 == 2);\n", {}, true);
  EXPECT_EQ(file->get_included_lines(), 0u);
}

TEST_F(CppParserTest, CoverageExcludesMacroBodies) {
  auto file = parse("#define M (1 + 2)\n", {}, true);
  EXPECT_EQ(file->get_included_lines(), 0u);
}

TEST_F(CppParserTest, CoverageExcludesIfConstexprCondition) {
  auto file = parse("int f() {\n  if constexpr (1 + 1 == 2) {\n  }\n  return 0;\n}\n", {}, true);
  const auto& counts = std::as_const(*file).get_line_counts();
  ASSERT_EQ(counts.size(), 5u);
  EXPECT_EQ(counts[1], -1);
  EXPECT_EQ(counts[3], 0);
}

TEST_F(CppParserTest, CoverageIncludesPlainIfCondition) {
  auto file = parse("int f(int x) {\n  if (x + 1 == 2) {\n  }\n  return 0;\n}\n", {}, true);
  const auto& counts = std::as_const(*file).get_line_counts();
  ASSERT_EQ(counts.size(), 5u);
  EXPECT_EQ(counts[1], 0);
  EXPECT_EQ(counts[3], 0);
}

TEST_F(CppParserTest, CoverageExcludesConstexprVariableInitializer) {
  auto file = parse("int f() {\n  constexpr int k = 1 + 2;\n  return k;\n}\n", {}, true);
  const auto& counts = std::as_const(*file).get_line_counts();
  ASSERT_EQ(counts.size(), 4u);
  EXPECT_EQ(counts[1], -1);
  EXPECT_EQ(counts[2], 0);
}

TEST_F(CppParserTest, CoverageIncludesNonConstexprVariableInitializer) {
  auto file = parse("int f() {\n  int k = 1 + 2;\n  return k;\n}\n", {}, true);
  const auto& counts = std::as_const(*file).get_line_counts();
  ASSERT_EQ(counts.size(), 4u);
  EXPECT_EQ(counts[1], 0);
}

TEST_F(CppParserTest, CoverageExcludesSizeofAndDecltype) {
  auto file = parse("int f() {\n  using T = decltype(1 + 2);\n  return sizeof(1 + 2);\n}\n", {}, true);
  const auto& counts = std::as_const(*file).get_line_counts();
  ASSERT_EQ(counts.size(), 4u);
  EXPECT_EQ(counts[1], -1);
  EXPECT_EQ(counts[2], 0); // the return statement itself is still executable
}

// ---------------------------------------------------------------------------
// Whole demo files, cross-checked against docs/generated
// ---------------------------------------------------------------------------

TEST_F(CppParserTest, DemoParsingGroupEntityCounts) {
  parse_demo("demo/parsing.hpp");
  auto g = group("parsing");
  // tables of docs/generated/parsing/index.md
  EXPECT_EQ(types(g).size(), 13u);
  EXPECT_EQ(typedefs(g).size(), 5u);
  EXPECT_EQ(concepts(g).size(), 2u);
  EXPECT_EQ(macros(g).size(), 4u);
  EXPECT_EQ(variables(g).size(), 19u);
  EXPECT_EQ(operators(g).size(), 6u);
  EXPECT_EQ(functions(g).size(), 9u);
}

TEST_F(CppParserTest, DemoParsingRootHoldsOnlyNamespaces) {
  parse_demo("demo/parsing.hpp");
  auto ns = namespaces(root());
  ASSERT_EQ(ns.size(), 1u);
  EXPECT_EQ(ns[0]->get_name(), "ns1");
  EXPECT_EQ(count_root_children(), 1u);
}

TEST_F(CppParserTest, DemoParsingNamespaceTree) {
  parse_demo("demo/parsing.hpp");
  // docs/generated/ns1/{ns2a/ns3,ns2b}
  auto ns1 = namespaces(root())[0];
  auto children = namespaces(ns1);
  ASSERT_EQ(children.size(), 2u);
  EXPECT_EQ(children[0]->get_full_name(), "ns1::ns2a");
  EXPECT_EQ(children[1]->get_full_name(), "ns1::ns2b");
  auto ns3 = namespaces(children[0]);
  ASSERT_EQ(ns3.size(), 1u);
  EXPECT_EQ(ns3[0]->get_full_name(), "ns1::ns2a::ns3");
  EXPECT_TRUE(namespaces(children[1]).empty());
}

TEST_F(CppParserTest, DemoParsingTypeNamesInSourceOrder) {
  parse_demo("demo/parsing.hpp");
  std::vector<std::string_view> names;
  for (auto& t : types(group("parsing"))) {
    names.push_back(t->get_name());
  }
  EXPECT_EQ(names, (std::vector<std::string_view>{
    "ForwardClass", "Class", "Struct", "Union", "ClassTemplate", "StructTemplate",
    "UnionTemplate", "Enum", "EnumAfter", "EnumClass", "EnumStruct",
    "ClassWithMembers", "ClassTemplateWithMembers"}));
}

TEST_F(CppParserTest, DemoParsingClassWithMembersCounts) {
  parse_demo("demo/parsing.hpp");
  std::optional<FwdRef<TypeEntity>> cwm;
  for (auto& t : types(group("parsing"))) {
    if (t->get_name() == "ClassWithMembers") {
      cwm = t;
    }
  }
  ASSERT_TRUE(cwm.has_value());
  // docs/generated/parsing/ClassWithMembers/index.md
  EXPECT_EQ(types(*cwm).size(), 2u);      // InnerClass, InnerEnum
  EXPECT_EQ(typedefs(*cwm).size(), 2u);   // InnerTypeDef, InnerTypeAlias
  EXPECT_EQ(variables(*cwm).size(), 12u); // x ... fp, escapee
  EXPECT_EQ(functions(*cwm).size(), 11u); // ctor, dtor, 9 x f
  EXPECT_EQ(operators(*cwm).size(), 11u); // 6 x operator+, operator=, 4 conversions
}

TEST_F(CppParserTest, DemoParsingMacroDeclarations) {
  parse_demo("demo/parsing.hpp");
  auto ms = macros(group("parsing"));
  ASSERT_EQ(ms.size(), 4u);
  EXPECT_EQ(ms[0]->get_decl(), "#define MACRO");
  EXPECT_EQ(ms[1]->get_decl(), "#define MACRO_WITH_ARGS(x, y)");
  EXPECT_EQ(ms[2]->get_decl(), "#define MACRO_AFTER");
  EXPECT_EQ(ms[3]->get_decl(), "#define MACRO_WITH_ARGS_AFTER(x, y)");
}

TEST_F(CppParserTest, DemoParsingAllGroupEntitiesAreVisible) {
  parse_demo("demo/parsing.hpp");
  auto g = group("parsing");
  for (auto& v : variables(g)) {
    EXPECT_TRUE(v->is_visible()) << v->get_name();
  }
  for (auto& f : functions(g)) {
    EXPECT_TRUE(f->is_visible()) << f->get_name();
  }
  for (auto& t : types(g)) {
    EXPECT_TRUE(t->is_visible()) << t->get_name();
  }
}

TEST_F(CppParserTest, DemoPresentationFunctions) {
  parse_demo("demo/presentation.hpp");
  auto fns = functions(group("presentation"));
  ASSERT_EQ(fns.size(), 4u);
  EXPECT_EQ(fns[0]->get_decl(), "int f(int x, int y)");
  EXPECT_EQ(fns[1]->get_decl(), "int g(int x, int y)");
  EXPECT_EQ(fns[2]->get_decl(), "template<class T> int h(int x, int y, int& z, int& w)");
  EXPECT_EQ(fns[3]->get_decl(), "template<class T> int i(int x, int y, int& z, int& w)");
  EXPECT_EQ(count_root_children(), 0u);
}

TEST_F(CppParserTest, DemoPresentationSummaryLines) {
  parse_demo("demo/presentation.hpp");
  auto fns = functions(group("presentation"));
  ASSERT_EQ(fns.size(), 4u);
  // first lines of the descriptions in docs/generated/presentation/index.md
  EXPECT_TRUE(fns[0]->get_docs().starts_with(
    "Function with documentation demonstrating various presentation features.\n"));
  EXPECT_TRUE(fns[1]->get_docs().starts_with(
    "Function with documentation demonstrating various presentation features,\n"
    "this time using end-of-line comments.\n"));
  EXPECT_TRUE(fns[2]->get_docs().starts_with("Function demonstrating all possible annotations.\n"));
  EXPECT_TRUE(fns[3]->get_docs().starts_with(
    "Function demonstrating all possible annotations, this time using\nend-of-line comments.\n"));
}
