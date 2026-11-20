/**
 * Unit tests for TextLineCursor.
 *
 * A TextLineCursor is a view over (part of) an external text that also knows
 * the 0-based number of the line its first character lies on, i.e., the number
 * of '\n' characters that precede the fragment in the whole text. Every
 * operation that derives a new fragment (advance(), substr()) must therefore
 * keep the line number consistent with the position of the fragment inside the
 * original text.
 */
#include <doxydown/TextLineCursor.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kText = "first\nsecond\n\nfourth\nfifth";
//                                  0    5 6    1213    20    25
// line:                            0       1      2 3      4

// number of newlines in text[0, offset): the line `offset` lies on
std::size_t line_of(std::string_view text, std::size_t offset) {
  return static_cast<std::size_t>(
      std::count(text.begin(), text.begin() + offset, '\n'));
}

// ---------------------------------------------------------------------------
// construction and accessors
// ---------------------------------------------------------------------------

TEST(TextLineCursorTest, DefaultConstructedIsEmpty) {
  TextLineCursor c;
  EXPECT_TRUE(c.empty());
  EXPECT_EQ(c.size(), 0u);
  EXPECT_EQ(c.get_line_number(), 0u);
  EXPECT_TRUE(c.view().empty());
  EXPECT_EQ(c.begin(), c.end());
}

TEST(TextLineCursorTest, ConstructFromViewStartsAtLineZero) {
  TextLineCursor c(kText);
  EXPECT_FALSE(c.empty());
  EXPECT_EQ(c.size(), kText.size());
  EXPECT_EQ(c.get_line_number(), 0u);
  EXPECT_EQ(c.view(), kText);
  // the cursor refers to the external text, it does not copy it
  EXPECT_EQ(c.view().data(), kText.data());
}

TEST(TextLineCursorTest, ConstructFromEmptyView) {
  TextLineCursor c{std::string_view()};
  EXPECT_TRUE(c.empty());
  EXPECT_EQ(c.get_line_number(), 0u);
}

TEST(TextLineCursorTest, NewlinesInTextDoNotAffectInitialLine) {
  TextLineCursor c{std::string_view("\n\n\nabc")};
  EXPECT_EQ(c.get_line_number(), 0u);
}

TEST(TextLineCursorTest, IndexingReturnsCharacters) {
  TextLineCursor c(kText);
  for (std::size_t i = 0; i < kText.size(); ++i) {
    EXPECT_EQ(c[i], kText[i]) << "at index " << i;
  }
}

TEST(TextLineCursorTest, IteratorsSpanTheText) {
  TextLineCursor c(kText);
  EXPECT_EQ(c.begin(), kText.begin());
  EXPECT_EQ(c.end(), kText.end());
  EXPECT_EQ(c.cbegin(), c.begin());
  EXPECT_EQ(c.cend(), c.end());
  EXPECT_EQ(std::string(c.begin(), c.end()), std::string(kText));
}

TEST(TextLineCursorTest, ToStringCopiesText) {
  TextLineCursor c(kText);
  EXPECT_EQ(c.to_string(), std::string(kText));
  EXPECT_EQ(TextLineCursor().to_string(), "");
}

TEST(TextLineCursorTest, ImplicitConversionToStringView) {
  TextLineCursor c(kText);
  std::string_view v = c;
  EXPECT_EQ(v, kText);
}

TEST(TextLineCursorTest, ComparisonWithStringView) {
  TextLineCursor c(std::string_view("abc"));
  EXPECT_TRUE(std::string_view("abc") == c);
  EXPECT_FALSE(std::string_view("abd") == c);
  EXPECT_FALSE(std::string_view("ab") == c);
  // C++20 rewritten candidate: the reversed form works as well
  EXPECT_TRUE(c == std::string_view("abc"));
}

TEST(TextLineCursorTest, ComparisonIgnoresLineNumber) {
  TextLineCursor c(std::string_view("a\nb"));
  auto b = c.substr(2);
  ASSERT_EQ(b.get_line_number(), 1u);
  EXPECT_TRUE(std::string_view("b") == b);
}

TEST(TextLineCursorTest, StreamOutputPrintsText) {
  std::ostringstream ss;
  ss << TextLineCursor(kText);
  EXPECT_EQ(ss.str(), std::string(kText));
}

TEST(TextLineCursorTest, CopyIsIndependent) {
  TextLineCursor a(kText);
  TextLineCursor b = a;
  b.advance(6);
  EXPECT_EQ(a.view(), kText);
  EXPECT_EQ(a.get_line_number(), 0u);
  EXPECT_EQ(b.get_line_number(), 1u);

  TextLineCursor c;
  c = b;
  EXPECT_EQ(c.view(), b.view());
  EXPECT_EQ(c.get_line_number(), b.get_line_number());
}

// ---------------------------------------------------------------------------
// advance()
// ---------------------------------------------------------------------------

TEST(TextLineCursorTest, AdvanceWithinLineKeepsLineNumber) {
  TextLineCursor c(kText);
  c.advance(3);
  EXPECT_EQ(c.view(), kText.substr(3));
  EXPECT_EQ(c.get_line_number(), 0u);
}

TEST(TextLineCursorTest, AdvanceZeroIsNoOp) {
  TextLineCursor c(kText);
  c.advance(0);
  EXPECT_EQ(c.view(), kText);
  EXPECT_EQ(c.get_line_number(), 0u);
}

TEST(TextLineCursorTest, AdvanceOntoNewlineDoesNotCountIt) {
  TextLineCursor c(kText);
  c.advance(5);  // cursor is on the first '\n', still on line 0
  ASSERT_EQ(c[0], '\n');
  EXPECT_EQ(c.get_line_number(), 0u);
  c.advance(1);  // past the '\n'
  EXPECT_EQ(c.view(), kText.substr(6));
  EXPECT_EQ(c.get_line_number(), 1u);
}

TEST(TextLineCursorTest, AdvanceOverConsecutiveNewlines) {
  TextLineCursor c(kText);
  c.advance(14);  // past "first\nsecond\n\n"
  EXPECT_EQ(c.view(), kText.substr(14));
  EXPECT_EQ(c.get_line_number(), 3u);
}

TEST(TextLineCursorTest, AdvanceAccumulates) {
  TextLineCursor c(kText);
  c.advance(4);
  c.advance(4);
  c.advance(6);
  EXPECT_EQ(c.view(), kText.substr(14));
  EXPECT_EQ(c.get_line_number(), 3u);
}

TEST(TextLineCursorTest, AdvanceToEndCountsAllNewlines) {
  TextLineCursor c(kText);
  c.advance(kText.size());
  EXPECT_TRUE(c.empty());
  EXPECT_EQ(c.get_line_number(), 4u);
}

TEST(TextLineCursorTest, AdvanceToEndOfTextEndingWithNewline) {
  TextLineCursor c(std::string_view("a\nb\n"));
  c.advance(4);
  EXPECT_TRUE(c.empty());
  EXPECT_EQ(c.get_line_number(), 2u);
}

TEST(TextLineCursorTest, AdvanceBeyondSizeThrowsAndLeavesCursorUnchanged) {
  TextLineCursor c(kText);
  c.advance(6);
  EXPECT_THROW(c.advance(c.size() + 1), std::out_of_range);
  EXPECT_EQ(c.view(), kText.substr(6));
  EXPECT_EQ(c.get_line_number(), 1u);
}

TEST(TextLineCursorTest, AdvanceOnEmptyCursor) {
  TextLineCursor c;
  EXPECT_NO_THROW(c.advance(0));
  EXPECT_THROW(c.advance(1), std::out_of_range);
}

TEST(TextLineCursorTest, CarriageReturnIsNotANewline) {
  TextLineCursor c(std::string_view("a\r\nb\rc"));
  c.advance(3);  // past "a\r\n"
  EXPECT_EQ(c.get_line_number(), 1u);
  c.advance(2);  // past "b\r"
  EXPECT_EQ(c.view(), "c");
  EXPECT_EQ(c.get_line_number(), 1u);
}

// ---------------------------------------------------------------------------
// substr(start, length)
// ---------------------------------------------------------------------------

TEST(TextLineCursorTest, SubstrFromStartKeepsLineNumber) {
  TextLineCursor c(kText);
  // a literal 0 would be ambiguous with substr(const_iterator, length), since
  // the iterator is a pointer on some standard libraries
  auto s = c.substr(std::size_t{0}, 5);
  EXPECT_EQ(s.view(), "first");
  EXPECT_EQ(s.get_line_number(), 0u);
}

TEST(TextLineCursorTest, SubstrTracksLineOfStart) {
  TextLineCursor c(kText);
  auto s = c.substr(6, 6);
  EXPECT_EQ(s.view(), "second");
  EXPECT_EQ(s.get_line_number(), 1u);

  auto f = c.substr(14, 6);
  EXPECT_EQ(f.view(), "fourth");
  EXPECT_EQ(f.get_line_number(), 3u);
}

TEST(TextLineCursorTest, SubstrNewlinesInsideFragmentDoNotAffectItsLine) {
  TextLineCursor c(kText);
  auto s = c.substr(6, 14);  // "second\n\nfourth"
  EXPECT_EQ(s.view(), "second\n\nfourth");
  EXPECT_EQ(s.get_line_number(), 1u);
}

TEST(TextLineCursorTest, SubstrStartingOnNewlineIsOnPreviousLine) {
  TextLineCursor c(kText);
  auto s = c.substr(12, 2);  // "\n\n" after "second"
  EXPECT_EQ(s.view(), "\n\n");
  EXPECT_EQ(s.get_line_number(), 1u);
}

TEST(TextLineCursorTest, SubstrDoesNotModifySource) {
  TextLineCursor c(kText);
  c.advance(6);
  auto s = c.substr(7, 3);
  (void)s;
  EXPECT_EQ(c.view(), kText.substr(6));
  EXPECT_EQ(c.get_line_number(), 1u);
}

TEST(TextLineCursorTest, SubstrLengthIsClampedToEnd) {
  TextLineCursor c(kText);
  auto s = c.substr(21, 1000);
  EXPECT_EQ(s.view(), "fifth");
  EXPECT_EQ(s.get_line_number(), 4u);
}

TEST(TextLineCursorTest, SubstrAtEndIsEmptyOnLastLine) {
  TextLineCursor c(kText);
  auto s = c.substr(kText.size(), 0);
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.get_line_number(), 4u);
}

TEST(TextLineCursorTest, SubstrStartBeyondSizeThrows) {
  TextLineCursor c(kText);
  EXPECT_THROW(c.substr(kText.size() + 1, 1), std::out_of_range);
}

TEST(TextLineCursorTest, SubstrOfAdvancedCursorIsRelativeToWholeText) {
  TextLineCursor c(kText);
  c.advance(6);                // "second\n\nfourth\nfifth", line 1
  auto s = c.substr(8, 6);     // "fourth"
  EXPECT_EQ(s.view(), "fourth");
  EXPECT_EQ(s.get_line_number(), 3u);
}

TEST(TextLineCursorTest, NestedSubstrAccumulatesLines) {
  TextLineCursor c(kText);
  auto outer = c.substr(6);            // "second\n\nfourth\nfifth", line 1
  auto middle = outer.substr(7);       // "\nfourth\nfifth", line 2
  auto inner = middle.substr(8, 5);    // "fifth", line 4
  EXPECT_EQ(outer.get_line_number(), 1u);
  EXPECT_EQ(middle.view(), "\nfourth\nfifth");
  EXPECT_EQ(middle.get_line_number(), 2u);
  EXPECT_EQ(inner.view(), "fifth");
  EXPECT_EQ(inner.get_line_number(), 4u);
}

TEST(TextLineCursorTest, SubstrThenAdvanceTracksWholeText) {
  TextLineCursor c(kText);
  auto s = c.substr(6, 14);  // "second\n\nfourth", line 1
  s.advance(8);              // "fourth"
  EXPECT_EQ(s.view(), "fourth");
  EXPECT_EQ(s.get_line_number(), 3u);
}

// ---------------------------------------------------------------------------
// substr(start)
// ---------------------------------------------------------------------------

TEST(TextLineCursorTest, SubstrToEndTracksLine) {
  TextLineCursor c(kText);
  auto s = c.substr(14);
  EXPECT_EQ(s.view(), "fourth\nfifth");
  EXPECT_EQ(s.get_line_number(), 3u);
}

TEST(TextLineCursorTest, SubstrToEndFromZeroIsWholeText) {
  TextLineCursor c(kText);
  auto s = c.substr(0);
  EXPECT_EQ(s.view(), kText);
  EXPECT_EQ(s.get_line_number(), 0u);
}

TEST(TextLineCursorTest, SubstrToEndAtSizeIsEmpty) {
  TextLineCursor c(kText);
  auto s = c.substr(kText.size());
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.get_line_number(), 4u);
}

TEST(TextLineCursorTest, SubstrToEndBeyondSizeThrows) {
  TextLineCursor c(kText);
  EXPECT_THROW(c.substr(kText.size() + 1), std::out_of_range);
}

// ---------------------------------------------------------------------------
// substr(iterator, length)
// ---------------------------------------------------------------------------

TEST(TextLineCursorTest, SubstrFromIteratorTracksLine) {
  TextLineCursor c(kText);
  auto it = std::find(c.begin(), c.end(), 'f');  // "first"
  ASSERT_EQ(it, c.begin());
  auto s = c.substr(it, 5);
  EXPECT_EQ(s.view(), "first");
  EXPECT_EQ(s.get_line_number(), 0u);

  auto it2 = c.begin() + 14;  // "fourth"
  auto s2 = c.substr(it2, 6);
  EXPECT_EQ(s2.view(), "fourth");
  EXPECT_EQ(s2.get_line_number(), 3u);
}

TEST(TextLineCursorTest, SubstrFromIteratorMatchesIndexOverload) {
  TextLineCursor c(kText);
  c.advance(3);
  for (std::size_t i = 0; i <= c.size(); ++i) {
    auto by_index = c.substr(i, 4);
    auto by_iter = c.substr(c.begin() + i, 4);
    EXPECT_EQ(by_iter.view(), by_index.view()) << "at offset " << i;
    EXPECT_EQ(by_iter.get_line_number(), by_index.get_line_number())
        << "at offset " << i;
  }
}

TEST(TextLineCursorTest, SubstrFromEndIteratorIsEmpty) {
  TextLineCursor c(kText);
  auto s = c.substr(c.end(), 3);
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.get_line_number(), 4u);
}

// ---------------------------------------------------------------------------
// consistency over every offset
// ---------------------------------------------------------------------------

TEST(TextLineCursorTest, LineNumberMatchesNewlineCountAtEveryOffset) {
  for (std::string_view text :
       {kText, std::string_view("\n\n\n"), std::string_view("no newlines"),
        std::string_view("\nleading"), std::string_view("trailing\n")}) {
    TextLineCursor c(text);
    for (std::size_t i = 0; i <= text.size(); ++i) {
      const std::size_t expected = line_of(text, i);

      EXPECT_EQ(c.substr(i).get_line_number(), expected)
          << "substr(" << i << ") of \"" << text << "\"";
      EXPECT_EQ(c.substr(i, 1).get_line_number(), expected)
          << "substr(" << i << ", 1) of \"" << text << "\"";
      EXPECT_EQ(c.substr(c.begin() + i, 1).get_line_number(), expected)
          << "substr(begin+" << i << ", 1) of \"" << text << "\"";

      TextLineCursor adv(text);
      adv.advance(i);
      EXPECT_EQ(adv.get_line_number(), expected)
          << "advance(" << i << ") of \"" << text << "\"";
      EXPECT_EQ(adv.view(), text.substr(i));
    }
  }
}

TEST(TextLineCursorTest, StepwiseAdvanceMatchesSingleAdvance) {
  TextLineCursor stepwise(kText);
  for (std::size_t i = 1; i <= kText.size(); ++i) {
    stepwise.advance(1);
    TextLineCursor once(kText);
    once.advance(i);
    EXPECT_EQ(stepwise.get_line_number(), once.get_line_number())
        << "after " << i << " steps";
    EXPECT_EQ(stepwise.view(), once.view());
  }
}

}  // namespace
