/**
 * Unit tests for StyleParsers, MkDocs style only.
 *
 * Each test feeds a documentation comment, exactly as CppParser extracts it
 * from a source file (from the opening marker to the end of the comment,
 * consecutive line comments merged into a single text), to the parser returned
 * by make_doc_parser(StyleOption::MKDOCS) and inspects the resulting Doc.
 *
 * All comment forms accepted by Doxygen are covered:
 *   - before the entity: `/ **`, `/ *!`, `///`, `//!`
 *   - after the entity:  `/ **<`, `/ *!<`, `///<`, `//!<`
 *
 * Only the @-commands handled by StyleParsers.cpp are in scope: everything
 * else is plain Markdown and must be emitted unchanged; the full Doxygen
 * syntax (e.g., \-prefixed commands, @brief, LaTeX-like formulas) is not
 * supported on purpose.
 *
 * Tests whose name starts with DISABLED_ describe the expected behavior for
 * known defects; run them with --gtest_also_run_disabled_tests.
 */
#include <doxydown/Doc.hpp>
#include <doxydown/StyleOption.hpp>
#include <doxydown/TextLineCursor.hpp>
#include <doxydown/doc_parser/DocParser.hpp>
#include <doxydown/doc_parser/StyleParsers.hpp>

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using ::testing::HasSubstr;
using ::testing::Not;

namespace {

DocParser& mkdocs_parser() {
  static std::unique_ptr<DocParser> parser = make_doc_parser(StyleOption::MKDOCS);
  return *parser;
}

/**
 * Parse a comment; the returned Doc may reference the comment text (through
 * Doc::ingroup), so the caller must keep @p comment alive.
 */
Doc parse(std::string_view comment) {
  return mkdocs_parser().parse(TextLineCursor(comment));
}

std::string docs_of(std::string_view comment) {
  return parse(comment).docs;
}

/*
 * Comment forms
 */

struct CommentForm {
  const char* name;
  const char* opener;
  bool block;  // `/ * ... * /` comment, otherwise sequence of line comments

  friend std::ostream& operator<<(std::ostream& os, const CommentForm& f) {
    return os << f.name;
  }
};

const CommentForm FORMS[] = {
  {"JavadocBlock", "/**", true},
  {"QtBlock", "/*!", true},
  {"TripleSlash", "///", false},
  {"QtLine", "//!", false},
  {"JavadocBlockAfter", "/**<", true},
  {"QtBlockAfter", "/*!<", true},
  {"TripleSlashAfter", "///<", false},
  {"QtLineAfter", "//!<", false},
};

/**
 * Build a comment of the given form holding @p lines. Block comments use the
 * usual layout with the opener and the closer on lines of their own and a
 * leading ` * ` on each line; line comments repeat the opener on each line.
 * An empty line yields a bare ` *` or opener.
 */
std::string make_comment(const CommentForm& form, const std::vector<std::string>& lines) {
  std::string res;
  if (form.block) {
    res = form.opener;
    for (const auto& l : lines) {
      res += l.empty() ? "\n *" : "\n * " + l;
    }
    res += "\n */";
  } else {
    for (std::size_t i = 0; i < lines.size(); ++i) {
      if (i > 0) {
        res += "\n";
      }
      res += form.opener;
      if (!lines[i].empty()) {
        res += " " + lines[i];
      }
    }
  }
  return res;
}

struct FormCase {
  const char* name;
  std::vector<std::string> lines;
  std::string expected;
};

const FormCase FORM_CASES[] = {
  {"Brief", {"Brief."}, "Brief.\n"},
  {"TwoLines", {"Line one", "line two."}, "Line one\nline two.\n"},
  {"TwoParagraphs", {"Para one.", "", "Para two."}, "Para one.\n\nPara two.\n"},
  {"LeadingEmptyLineIsDropped", {"", "Text."}, "Text.\n"},
  {"TrailingEmptyLinesAreDropped", {"Text.", "", ""}, "Text.\n"},
  {"MarkdownIsPassedThrough",
    {"Text with `code`, *emphasis*, a [link](#f)."},
    "Text with `code`, *emphasis*, a [link](#f).\n"},
  {"MarkdownList",
    {"List:", "", "  - One.", "  - Two."},
    "List:\n\n  - One.\n  - Two.\n"},
  {"ParamsAndReturn",
    {"Text.", "", "@param x The x.", "@param y The y.", "", "@return Sum."},
    "Text.\n\n"
    "\n:material-location-enter: `x`\n:    The x.\n    "
    "\n:material-location-enter: `y`\n:    The y.\n\n"
    "\n:material-keyboard-return: **Return**\n:    Sum.\n"},
  {"MultiLineParamIsIndented",
    {"@param x Description spanning", "two lines.", "", "After."},
    "\n:material-location-enter: `x`\n:    Description spanning\n    two lines.\n\nAfter.\n"},
  {"Admonition",
    {"@note", "Body line", "second.", "", "After."},
    "\n!!! note\n    Body line\n    second.\n\nAfter.\n"},
  {"IngroupIsRemovedFromDocs", {"@ingroup grp", "Text."}, "\nText.\n"},
};

struct FormAndCase {
  CommentForm form;
  FormCase c;
};

std::vector<FormAndCase> all_forms_and_cases() {
  std::vector<FormAndCase> res;
  for (const auto& f : FORMS) {
    for (const auto& c : FORM_CASES) {
      res.push_back({f, c});
    }
  }
  return res;
}

class CommentFormTest : public ::testing::TestWithParam<FormAndCase> {};

TEST_P(CommentFormTest, ProducesExpectedDocs) {
  const auto& [form, c] = GetParam();
  const std::string comment = make_comment(form, c.lines);
  SCOPED_TRACE("comment:\n" + comment);
  const Doc doc = parse(comment);
  EXPECT_EQ(doc.docs, c.expected);
  EXPECT_FALSE(doc.hide);
}

INSTANTIATE_TEST_SUITE_P(MkDocs, CommentFormTest,
  ::testing::ValuesIn(all_forms_and_cases()),
  [](const auto& info) {
    return std::string(info.param.form.name) + "_" + info.param.c.name;
  });

/*
 * Single-line comments: `/ ** text * /` on one line, and each line comment
 * form with a single line.
 */

struct SingleLineCase {
  const char* name;
  const char* comment;
};

class SingleLineTest : public ::testing::TestWithParam<SingleLineCase> {};

TEST_P(SingleLineTest, ProducesBrief) {
  const Doc doc = parse(GetParam().comment);
  EXPECT_EQ(doc.docs, "Brief.\n");
  EXPECT_TRUE(doc.ingroup.empty());
  EXPECT_FALSE(doc.hide);
}

INSTANTIATE_TEST_SUITE_P(MkDocs, SingleLineTest, ::testing::Values(
    SingleLineCase{"JavadocBlock", "/** Brief. */"},
    SingleLineCase{"QtBlock", "/*! Brief. */"},
    SingleLineCase{"JavadocBlockAfter", "/**< Brief. */"},
    SingleLineCase{"QtBlockAfter", "/*!< Brief. */"},
    SingleLineCase{"TripleSlash", "/// Brief."},
    SingleLineCase{"QtLine", "//! Brief."},
    SingleLineCase{"TripleSlashAfter", "///< Brief."},
    SingleLineCase{"QtLineAfter", "//!< Brief."},
    SingleLineCase{"TripleSlashNoSpace", "///Brief."},
    SingleLineCase{"QtLineNoSpace", "//!Brief."},
    SingleLineCase{"TripleSlashAfterNoSpace", "///<Brief."},
    SingleLineCase{"QtLineAfterNoSpace", "//!<Brief."},
    SingleLineCase{"JavadocBlockNoSpaces", "/**Brief.*/"},
    SingleLineCase{"TrailingNewLine", "/** Brief. */\n"},
    SingleLineCase{"TrailingSpaces", "/// Brief.   "}
  ),
  [](const auto& info) { return std::string(info.param.name); });

/*
 * Layout variants
 */

TEST(StyleParsersLayout, BlockTextOnOpeningAndClosingLines) {
  EXPECT_EQ(docs_of("/** Line one\n * line two. */"), "Line one\nline two.\n");
}

TEST(StyleParsersLayout, BlockCommentAfterEntityOnSeveralLines) {
  EXPECT_EQ(docs_of("/**< Line one\n *   line two. */"), "Line one\n  line two.\n");
}

TEST(StyleParsersLayout, BlockWithoutLeadingStarsKeepsExtraIndentation) {
  // the first space of each line is dropped, the rest is kept
  EXPECT_EQ(docs_of("/**\n   Line one\n   line two.\n */"), "  Line one\n  line two.\n");
}

TEST(StyleParsersLayout, BlockStarWithoutSpace) {
  EXPECT_EQ(docs_of("/**\n *Line one\n *line two.\n */"), "Line one\nline two.\n");
}

TEST(StyleParsersLayout, BlockExtraSpacesAfterStarAreKept) {
  // indentation after ` * ` matters in Markdown (e.g., nested lists)
  EXPECT_EQ(docs_of("/**\n * List:\n *\n *   - One.\n *     - Nested.\n */"),
    "List:\n\n  - One.\n    - Nested.\n");
}

TEST(StyleParsersLayout, IndentedLineCommentContinuation) {
  // as for comments of class members, where lines after the first are indented
  EXPECT_EQ(docs_of("/// Line one\n    /// line two."), "Line one\nline two.\n");
  EXPECT_EQ(docs_of("//! Line one\n\t//! line two."), "Line one\nline two.\n");
  EXPECT_EQ(docs_of("///< Line one\n          ///< line two."), "Line one\nline two.\n");
  EXPECT_EQ(docs_of("//!< Line one\n  //!< line two."), "Line one\nline two.\n");
}

TEST(StyleParsersLayout, IndentedBlockCommentContinuation) {
  EXPECT_EQ(docs_of("/**\n     * Line one\n     * line two.\n     */"), "Line one\nline two.\n");
}

TEST(StyleParsersLayout, SpacesAfterOpenerAreKeptButOne) {
  EXPECT_EQ(docs_of("/**   Spaced.   */"), "  Spaced.\n");
}

TEST(StyleParsersLayout, StarsAndSlashesInsideText) {
  EXPECT_EQ(docs_of("/** x * y / z */"), "x * y / z\n");
  EXPECT_EQ(docs_of("/// a/b, *c*, **d**"), "a/b, *c*, **d**\n");
}

TEST(StyleParsersLayout, SentencePunctuationInsideWords) {
  EXPECT_EQ(docs_of("/** a.b!c?d */"), "a.b!c?d\n");
}

TEST(StyleParsersLayout, OutputAlwaysEndsWithSingleNewLine) {
  for (const char* c : {"/** Text. */", "/** Text.\n *\n *\n */", "/// Text.\n///\n///",
      "/** Text.    \n */", "/** Text. \t */"}) {
    SCOPED_TRACE(c);
    const std::string docs = docs_of(c);
    ASSERT_FALSE(docs.empty());
    EXPECT_EQ(docs.back(), '\n');
    EXPECT_EQ(docs, "Text.\n");
  }
}

/*
 * Empty and invalid comments
 */

class EmptyCommentTest : public ::testing::TestWithParam<const char*> {};

TEST_P(EmptyCommentTest, ProducesSingleNewLine) {
  const Doc doc = parse(GetParam());
  EXPECT_EQ(doc.docs, "\n");
  EXPECT_TRUE(doc.ingroup.empty());
  EXPECT_FALSE(doc.hide);
}

INSTANTIATE_TEST_SUITE_P(MkDocs, EmptyCommentTest, ::testing::Values(
  "///", "//!", "///<", "//!<", "/** */", "/*! */", "/**< */", "/*!< */",
  "/***/", "/**\n */", "/**\n *\n */", "///\n///", "/// "));

class NotADocCommentTest : public ::testing::TestWithParam<const char*> {};

TEST_P(NotADocCommentTest, Throws) {
  EXPECT_THROW(parse(GetParam()), std::logic_error);
}

INSTANTIATE_TEST_SUITE_P(MkDocs, NotADocCommentTest, ::testing::Values(
  "// Plain line comment.",
  "/* Plain block comment. */",
  "//< Not a Doxygen marker.",
  "/*< Not a Doxygen marker. */",
  "//| Not a Doxygen marker.",
  "/*| Not a Doxygen marker. */",
  "//|< Not a Doxygen marker.",
  "/*|< Not a Doxygen marker. */",
  "/**/",
  "/**/ Text after an empty comment.",
  "Text outside a comment.",
  "  /// Leading whitespace is not part of the comment.",
  ""));

/*
 * Parameters and sections
 */

TEST(StyleParsersCommands, ParamVariants) {
  EXPECT_EQ(docs_of("/** @param x X. */"), "\n:material-location-enter: `x`\n:    X.\n");
  EXPECT_EQ(docs_of("/** @param[in] x X. */"), "\n:material-location-enter: `x`\n:    X.\n");
  EXPECT_EQ(docs_of("/** @param[out] x X. */"), "\n:material-location-exit: `x`\n:    X.\n");
  EXPECT_EQ(docs_of("/** @param[in,out] x X. */"),
    "\n:material-location-enter::material-location-exit: `x`\n:    X.\n");
  EXPECT_EQ(docs_of("/** @tparam T Type. */"), "\n:material-code-tags: `T`\n:    Type.\n");
}

TEST(StyleParsersCommands, ParamDescriptionOnNextLine) {
  EXPECT_EQ(docs_of("/** @param[in] x\n *  X. */"),
    "\n:material-location-enter: `x`\n:   \n     X.\n");
}

class SectionContinuationTest : public ::testing::TestWithParam<const char*> {};

TEST_P(SectionContinuationTest, ContinuationLineIsIndented) {
  const std::string docs = docs_of(std::string("/// @") + GetParam() + " First line\n/// second line.");
  EXPECT_THAT(docs, HasSubstr("First line\n    second line.\n"));
}

TEST_P(SectionContinuationTest, ParagraphBreakEndsSection) {
  const std::string docs = docs_of(std::string("/**\n * @") + GetParam()
    + " Description.\n *\n * After.\n */");
  EXPECT_THAT(docs, HasSubstr("Description.\n\nAfter.\n"));
}

INSTANTIATE_TEST_SUITE_P(MkDocs, SectionContinuationTest, ::testing::Values(
  "param x", "param[in] x", "param[out] x", "param[in,out] x", "tparam T",
  "return", "returns", "result", "pre", "post", "throw", "see", "sa"));

TEST(StyleParsersOtherStyles, SectionContinuationIsNotIndented) {
  // only MkDocs indents continuation lines of sections other than @param
  const std::string comment = "/// @return First\n/// second.";
  EXPECT_EQ(make_doc_parser(StyleOption::PANDOCS)->parse(TextLineCursor(comment)).docs,
    "\nReturn\n:    First\nsecond.\n");
  EXPECT_EQ(make_doc_parser(StyleOption::PLAIN)->parse(TextLineCursor(comment)).docs,
    "\n\n  - **Return**\n\n\n     First\nsecond.\n");
}

TEST(StyleParsersOtherStyles, AdmonitionBodyOnSameLineStaysThere) {
  // only MkDocs moves the inline body of an admonition to a new line
  const std::string comment = "/** @todo T. @note N. */";
  EXPECT_EQ(make_doc_parser(StyleOption::PANDOCS)->parse(TextLineCursor(comment)).docs,
    "\n::: example\n T. \n::: note\n N. \n:::\n:::\n");
  EXPECT_EQ(make_doc_parser(StyleOption::PLAIN)->parse(TextLineCursor(comment)).docs,
    "> **To-do**\n>  T. > **NOTE**\n>  N.\n");
}

TEST(StyleParsersCommands, Sections) {
  EXPECT_EQ(docs_of("/** @pre P. @post Q. @throw E. @see F. @sa G. */"),
    "\n:material-check-circle-outline: **Pre-condition**\n:    P. "
    "\n:material-check-circle-outline: **Post-condition**\n:    Q. "
    "\n:material-alert-circle-outline: **Throw**\n:    E. "
    "\n:material-eye-outline: **See**\n:    F. "
    "\n:material-eye-outline: **See**\n:    G.\n");
}

TEST(StyleParsersCommands, ReturnSynonyms) {
  const std::string expected = "\n:material-keyboard-return: **Return**\n:    R.\n";
  EXPECT_EQ(docs_of("/** @return R. */"), expected);
  EXPECT_EQ(docs_of("/** @returns R. */"), expected);
  EXPECT_EQ(docs_of("/** @result R. */"), expected);
}

TEST(StyleParsersCommands, SectionsOnSeparateLines) {
  EXPECT_EQ(docs_of("/// @pre P.\n/// @post Q."),
    "\n:material-check-circle-outline: **Pre-condition**\n:    P.\n    "
    "\n:material-check-circle-outline: **Post-condition**\n:    Q.\n");
}

TEST(StyleParsersCommands, ParamInsideAdmonitionEndsIt) {
  EXPECT_EQ(docs_of("/** @note\n * N.\n * @param x X.\n */"),
    "\n!!! note\n    N.\n    \n:material-location-enter: `x`\n:    X.\n");
}

/*
 * Inline formatting
 */

TEST(StyleParsersCommands, InlineFormatting) {
  EXPECT_EQ(docs_of("/** @p x */"), "**`x`**\n");
  EXPECT_EQ(docs_of("/** @e word */"), "*word*\n");
  EXPECT_EQ(docs_of("/** @em word */"), "*word*\n");
  EXPECT_EQ(docs_of("/** @a word */"), "*word*\n");
  EXPECT_EQ(docs_of("/** @b word */"), "**word**\n");
  EXPECT_EQ(docs_of("/** @c word */"), "`word`\n");
}

TEST(StyleParsersCommands, InlineFormattingWithinText) {
  EXPECT_EQ(docs_of("/**\n * Text @c code and @b bold, @p x.\n */"),
    "Text `code` and **bold,** **`x.`**\n");
}

TEST(StyleParsersCommands, Anchor) {
  EXPECT_EQ(docs_of("/** @anchor here Text. */"), "<a name=\"here\"></a> Text.\n");
}

TEST(StyleParsersCommands, Ref) {
  EXPECT_EQ(docs_of("/** See @ref target text. */"), "See [text.](#target)\n");
}

TEST(StyleParsersCommands, CodeBlock) {
  const std::string docs = docs_of("/** @code\n * int x;\n * @endcode\n */");
  EXPECT_THAT(docs, HasSubstr("```\nint x;\n"));
  EXPECT_EQ(docs.substr(docs.size() - 4), "```\n");
  EXPECT_EQ(docs_of("/// @verbatim\n/// raw\n/// @endverbatim"), "\n```\nraw\n\n```\n");
}

TEST(StyleParsersCommands, MarkdownFencedCodeIsPassedThrough) {
  EXPECT_EQ(docs_of("/**\n * ```cpp\n * void f(int x);\n * ```\n */"),
    "```cpp\nvoid f(int x);\n```\n");
}

/*
 * Admonitions
 */

class AdmonitionTest : public ::testing::TestWithParam<const char*> {};

TEST_P(AdmonitionTest, BodyOnFollowingLinesIsIndented) {
  const std::string name = GetParam();
  EXPECT_EQ(docs_of("/**\n * @" + name + "\n * Body line\n * second.\n */"),
    "\n!!! " + name + "\n    Body line\n    second.\n");
}

TEST_P(AdmonitionTest, ParagraphBreakEndsAdmonition) {
  const std::string name = GetParam();
  EXPECT_EQ(docs_of("/// @" + name + "\n/// Body.\n///\n/// After."),
    "\n!!! " + name + "\n    Body.\n\nAfter.\n");
}

TEST_P(AdmonitionTest, AfterText) {
  const std::string name = GetParam();
  EXPECT_EQ(docs_of("/**\n * Text.\n * @" + name + "\n * Body.\n */"),
    "Text.\n\n!!! " + name + "\n    Body.\n");
}

TEST_P(AdmonitionTest, BodyOnSameLineMovesToNextLine) {
  const std::string name = GetParam();
  EXPECT_EQ(docs_of("/** @" + name + " Body. */"), "\n!!! " + name + "\n    Body.\n");
  EXPECT_EQ(docs_of("/// @" + name + "   Body."), "\n!!! " + name + "\n    Body.\n");
}

TEST_P(AdmonitionTest, BodyStartingOnSameLineContinuesOnNextOnes) {
  const std::string name = GetParam();
  EXPECT_EQ(docs_of("/**\n * @" + name + " Body line\n * second.\n *\n * After.\n */"),
    "\n!!! " + name + "\n    Body line\n    second.\n\nAfter.\n");
}

TEST_P(AdmonitionTest, BodyStartingWithInlineCommand) {
  const std::string name = GetParam();
  EXPECT_EQ(docs_of("/** @" + name + " @c code here. */"),
    "\n!!! " + name + "\n    `code` here.\n");
}

TEST_P(AdmonitionTest, EmptyAdmonition) {
  const std::string name = GetParam();
  EXPECT_EQ(docs_of("/** Text. @" + name + " */"), "Text. \n!!! " + name + "\n");
}

INSTANTIATE_TEST_SUITE_P(MkDocs, AdmonitionTest, ::testing::Values(
  "note", "abstract", "info", "tip", "success", "question", "warning",
  "failure", "danger", "bug", "example", "quote"));

struct LegacyAdmonition {
  const char* command;
  const char* header;
};

class LegacyAdmonitionTest : public ::testing::TestWithParam<LegacyAdmonition> {};

TEST_P(LegacyAdmonitionTest, MapsToMkDocsAdmonition) {
  const auto& [command, header] = GetParam();
  EXPECT_EQ(docs_of(std::string("/** @") + command + "\n * Body.\n *\n * After.\n */"),
    std::string("\n") + header + "\n    Body.\n\nAfter.\n");
}

TEST_P(LegacyAdmonitionTest, BodyOnSameLineMovesToNextLine) {
  const auto& [command, header] = GetParam();
  EXPECT_EQ(docs_of(std::string("/** @") + command + " Body.\n * More.\n */"),
    std::string("\n") + header + "\n    Body.\n    More.\n");
}

INSTANTIATE_TEST_SUITE_P(MkDocs, LegacyAdmonitionTest, ::testing::Values(
    LegacyAdmonition{"attention", "!!! warning \"Attention\""},
    LegacyAdmonition{"todo", "!!! example \"To-do\""},
    LegacyAdmonition{"remark", "!!! quote \"Remark\""}
  ),
  [](const auto& info) { return std::string(info.param.command); });

/*
 * Groups and visibility
 */

TEST(StyleParsersCommands, IngroupIsStoredAndRemoved) {
  const std::string comment = "/** Text. @ingroup grp More. */";
  const Doc doc = parse(comment);
  EXPECT_EQ(doc.ingroup.view(), "grp");
  EXPECT_EQ(doc.docs, "Text.  More.\n");
  EXPECT_FALSE(doc.hide);
}

TEST(StyleParsersCommands, IngroupRemembersLineNumber) {
  const std::string comment = "/**\n * Text.\n *\n * @ingroup parsing\n */";
  const Doc doc = parse(comment);
  EXPECT_EQ(doc.ingroup.view(), "parsing");
  EXPECT_EQ(doc.ingroup.get_line_number(), 3u);
  EXPECT_EQ(doc.docs, "Text.\n");
}

TEST(StyleParsersCommands, IngroupInCommentAfterEntity) {
  // from demo/parsing.hpp
  const std::string comment =
    "///< Test macro, documented after entity with `///` comment\n///< @ingroup parsing";
  const Doc doc = parse(comment);
  EXPECT_EQ(doc.ingroup.view(), "parsing");
  EXPECT_EQ(doc.ingroup.get_line_number(), 1u);
  EXPECT_EQ(doc.docs, "Test macro, documented after entity with `///` comment\n");
}

TEST(StyleParsersCommands, IngroupOnSameLineAsText) {
  // from demo/parsing.hpp
  const std::string comment = "///< Documented after entity with `///` comment @ingroup parsing";
  const Doc doc = parse(comment);
  EXPECT_EQ(doc.ingroup.view(), "parsing");
  EXPECT_EQ(doc.docs, "Documented after entity with `///` comment\n");
}

TEST(StyleParsersCommands, LastIngroupWins) {
  const std::string comment = "/** @ingroup a @ingroup b */";
  EXPECT_EQ(parse(comment).ingroup.view(), "b");
}

TEST(StyleParsersCommands, NoIngroupMeansEmptyGroup) {
  const Doc doc = parse("/** Text. */");
  EXPECT_TRUE(doc.ingroup.empty());
}

TEST(StyleParsersCommands, FileAndInternalHideEntity) {
  for (const char* c : {"/** @file */", "/// @file", "/** @internal Hidden. */",
      "//!< @internal"}) {
    SCOPED_TRACE(c);
    EXPECT_TRUE(parse(c).hide);
  }
  EXPECT_EQ(docs_of("/** @internal Hidden. */"), " Hidden.\n");
}

class StructuralCommandTest : public ::testing::TestWithParam<const char*> {};

TEST_P(StructuralCommandTest, IsDroppedWithItsName) {
  const std::string name = GetParam();
  const std::string docs = docs_of("/** @" + name + " Name Text. */");
  EXPECT_EQ(docs, " Text.\n");
  EXPECT_FALSE(parse("/** @" + name + " Name */").hide);
}

INSTANTIATE_TEST_SUITE_P(MkDocs, StructuralCommandTest, ::testing::Values(
  "def", "var", "fn", "class", "struct", "union", "enum", "typedef",
  "namespace", "interface", "protocol", "property"));

/*
 * Escapes and unknown commands
 */

TEST(StyleParsersCommands, Escapes) {
  EXPECT_EQ(docs_of("/** @@ */"), "@\n");
  EXPECT_EQ(docs_of("/** @/ */"), "/\n");
  EXPECT_EQ(docs_of("/** a@@b */"), "a@@b\n");  // inside a word, not a command
}

TEST(StyleParsersCommands, UnknownAtCommandIsKeptVerbatim) {
  // unsupported Doxygen commands are not stripped, just emitted as text
  EXPECT_EQ(docs_of("/** @unknowncommand x */"), "@unknowncommand x\n");
  EXPECT_EQ(docs_of("/** @brief Brief. */"), "@brief Brief.\n");
}

/*
 * Consistency between comment forms on a full example (demo/presentation.hpp,
 * functions h() and i(), rendered in docs/generated/presentation/index.md).
 */

const std::vector<std::string> ANNOTATIONS_DEMO = {
  "Function demonstrating all possible annotations.",
  "",
  "@ingroup presentation",
  "",
  "@tparam T Template parameter.",
  "",
  "@param x Input parameter.",
  "@param[in] y Input parameter.",
  "@param[out] z Output parameter.",
  "@param[in,out] w Input-output parameter.",
  "@param u Input parameter with a description that spans across multiple",
  "lines.",
  "",
  "@return Return value.",
  "",
  "@pre Pre-condition.",
  "@post Post-condition.",
  "@throw Exception.",
  "",
  "@see [f](#f)",
};

const std::string ANNOTATIONS_DEMO_DOCS =
  "Function demonstrating all possible annotations.\n"
  "\n"
  "\n"
  "\n"
  "\n"
  ":material-code-tags: `T`\n"
  ":    Template parameter.\n"
  "\n"
  "\n"
  ":material-location-enter: `x`\n"
  ":    Input parameter.\n"
  "    \n"
  ":material-location-enter: `y`\n"
  ":    Input parameter.\n"
  "    \n"
  ":material-location-exit: `z`\n"
  ":    Output parameter.\n"
  "    \n"
  ":material-location-enter::material-location-exit: `w`\n"
  ":    Input-output parameter.\n"
  "    \n"
  ":material-location-enter: `u`\n"
  ":    Input parameter with a description that spans across multiple\n"
  "    lines.\n"
  "\n"
  "\n"
  ":material-keyboard-return: **Return**\n"
  ":    Return value.\n"
  "\n"
  "\n"
  ":material-check-circle-outline: **Pre-condition**\n"
  ":    Pre-condition.\n"
  "    \n"
  ":material-check-circle-outline: **Post-condition**\n"
  ":    Post-condition.\n"
  "    \n"
  ":material-alert-circle-outline: **Throw**\n"
  ":    Exception.\n"
  "\n"
  "\n"
  ":material-eye-outline: **See**\n"
  ":    [f](#f)\n";

class AnnotationsDemoTest : public ::testing::TestWithParam<CommentForm> {};

TEST_P(AnnotationsDemoTest, MatchesGeneratedDocs) {
  const std::string comment = make_comment(GetParam(), ANNOTATIONS_DEMO);
  const Doc doc = parse(comment);
  EXPECT_EQ(doc.docs, ANNOTATIONS_DEMO_DOCS);
  EXPECT_EQ(doc.ingroup.view(), "presentation");
  EXPECT_FALSE(doc.hide);
}

INSTANTIATE_TEST_SUITE_P(MkDocs, AnnotationsDemoTest, ::testing::ValuesIn(FORMS),
  [](const auto& info) { return std::string(info.param.name); });

/*
 * Known defects: these tests describe the expected behavior and are disabled
 * until the parser is fixed.
 */

TEST(StyleParsersKnownDefects, DISABLED_ListItemKeepsItsText) {
  // the word following @li / @arg is consumed and dropped
  EXPECT_THAT(docs_of("/** @li item */"), HasSubstr("item"));
  EXPECT_THAT(docs_of("/** @arg item */"), HasSubstr("item"));
}

TEST(StyleParsersKnownDefects, DISABLED_RefWithoutTextUsesTarget) {
  EXPECT_EQ(docs_of("/** @ref target */"), "[target](#target)\n");
}

TEST(StyleParsersKnownDefects, DISABLED_CodeBlockHasNoTrailingEmptyLine) {
  EXPECT_EQ(docs_of("/** @code\n * int x;\n * @endcode\n */"), "\n```\nint x;\n```\n");
}

}  // namespace
