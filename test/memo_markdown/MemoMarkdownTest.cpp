// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 The InkDesk Authors

#include <GfxRenderer.h>
#include <gtest/gtest.h>
#include <util/MemoMarkdown.h>

#include <string>

using memomd::Kind;

namespace {

// Fixture metrics (stub renderer): every glyph is 8 px wide, lines are 16 px.
memomd::Doc layout(const std::string& text, const int width = 480) {
  GfxRenderer renderer;
  return memomd::layout(renderer, 0, text, width);
}

std::string textOf(const memomd::Line& line) {
  std::string s;
  for (const auto& piece : line.pieces) s += piece.text;
  return s;
}

}  // namespace

TEST(MemoMarkdown, RecognizesBlockKinds) {
  const auto doc = layout("# 标题\n- 牛奶\n1. 第一\n> 引用\n---\n\n正文");
  ASSERT_EQ(doc.lines.size(), 7u);
  EXPECT_EQ(doc.lines[0].kind, Kind::Heading);
  EXPECT_EQ(doc.lines[0].level, 1);
  EXPECT_EQ(textOf(doc.lines[0]), "标题");
  EXPECT_EQ(doc.lines[1].kind, Kind::Bullet);
  EXPECT_EQ(textOf(doc.lines[1]), "牛奶");
  EXPECT_EQ(doc.lines[2].kind, Kind::Number);
  EXPECT_EQ(doc.lines[2].marker, "1.");
  EXPECT_EQ(doc.lines[3].kind, Kind::Quote);
  EXPECT_EQ(doc.lines[4].kind, Kind::Rule);
  EXPECT_EQ(doc.lines[5].kind, Kind::Blank);
  EXPECT_EQ(doc.lines[6].kind, Kind::Text);
}

TEST(MemoMarkdown, ChecklistItemsAreNumberedAndStruckWhenDone) {
  const auto doc = layout("- [ ] 买菜\n- [x] 交报告\n[ ] no bullet\n  - [X] nested");
  ASSERT_EQ(doc.checkCount, 4);
  ASSERT_EQ(doc.lines.size(), 4u);
  EXPECT_FALSE(doc.lines[0].checked);
  EXPECT_TRUE(doc.lines[1].checked);
  EXPECT_EQ(doc.lines[1].checkIndex, 1);
  EXPECT_TRUE(doc.lines[1].pieces.front().strike);
  EXPECT_FALSE(doc.lines[0].pieces.front().strike);
  EXPECT_EQ(textOf(doc.lines[2]), "no bullet");
  EXPECT_EQ(doc.lines[3].level, 1);
  EXPECT_TRUE(doc.lines[3].checked);
}

TEST(MemoMarkdown, InlineStylesSplitIntoPieces) {
  const auto doc = layout("a **bold** *it* ~~gone~~ `x*y` [link](http://e.x) \\*lit\\*");
  ASSERT_EQ(doc.lines.size(), 1u);
  const auto& p = doc.lines[0].pieces;
  EXPECT_EQ(textOf(doc.lines[0]), "a bold it gone x*y link *lit*");
  bool sawBold = false, sawItalic = false, sawStrike = false;
  for (const auto& piece : p) {
    if (piece.text == "bold") sawBold = piece.style == EpdFontFamily::BOLD;
    if (piece.text == "it") sawItalic = piece.style == EpdFontFamily::ITALIC;
    if (piece.text == "gone") sawStrike = piece.strike;
  }
  EXPECT_TRUE(sawBold);
  EXPECT_TRUE(sawItalic);
  EXPECT_TRUE(sawStrike);
}

TEST(MemoMarkdown, SnakeCaseUnderscoresStayLiteral) {
  const auto doc = layout("file_name_here");
  EXPECT_EQ(textOf(doc.lines[0]), "file_name_here");
}

TEST(MemoMarkdown, ChineseWrapsPerCharacterAndKeepsPunctuationAttached) {
  // 10 glyphs per line; the comma after the tenth character stays on line one.
  const auto doc = layout("一二三四五六七八九十，甲乙", 80);
  ASSERT_EQ(doc.lines.size(), 2u);
  EXPECT_EQ(textOf(doc.lines[0]), "一二三四五六七八九十，");
  EXPECT_EQ(textOf(doc.lines[1]), "甲乙");
  EXPECT_FALSE(doc.lines[1].first);
}

TEST(MemoMarkdown, WrappedListItemsIndentContinuationLines) {
  const auto doc = layout("- [ ] one two three four five six", 120);
  ASSERT_GE(doc.lines.size(), 2u);
  EXPECT_EQ(doc.lines[1].kind, Kind::Check);
  EXPECT_FALSE(doc.lines[1].first);
  EXPECT_EQ(doc.lines[1].checkIndex, 0);
  EXPECT_EQ(doc.lines[0].indent, doc.lines[1].indent);
}

TEST(MemoMarkdown, ToggleFlipsOnlyTheRequestedItem) {
  // Sibling items are independent; nesting is covered by the tests below.
  std::string text = "# 清单\n- [ ] 一\n- [x] 二\n普通\n[ ] 三";
  ASSERT_TRUE(memomd::toggleCheck(text, 0));
  EXPECT_EQ(text, "# 清单\n- [x] 一\n- [x] 二\n普通\n[ ] 三");
  ASSERT_TRUE(memomd::toggleCheck(text, 1));
  EXPECT_EQ(text, "# 清单\n- [x] 一\n- [ ] 二\n普通\n[ ] 三");
  ASSERT_TRUE(memomd::toggleCheck(text, 2));
  EXPECT_EQ(text, "# 清单\n- [x] 一\n- [ ] 二\n普通\n[x] 三");
  EXPECT_FALSE(memomd::toggleCheck(text, 3));
}

TEST(MemoMarkdown, ParentTicksWhenAllSubItemsAreDone) {
  std::string text = "- [ ] 给妈妈打电话\n  - [ ] 订餐厅\n  - [ ] 买花\n- [ ] 别的事";
  ASSERT_TRUE(memomd::toggleCheck(text, 1));
  EXPECT_EQ(text, "- [ ] 给妈妈打电话\n  - [x] 订餐厅\n  - [ ] 买花\n- [ ] 别的事");
  ASSERT_TRUE(memomd::toggleCheck(text, 2));
  EXPECT_EQ(text, "- [x] 给妈妈打电话\n  - [x] 订餐厅\n  - [x] 买花\n- [ ] 别的事");
  // Unticking a sub-item reopens the parent.
  ASSERT_TRUE(memomd::toggleCheck(text, 1));
  EXPECT_EQ(text, "- [ ] 给妈妈打电话\n  - [ ] 订餐厅\n  - [x] 买花\n- [ ] 别的事");
}

TEST(MemoMarkdown, TickingAParentTicksItsSubItems) {
  std::string text = "- [ ] 周末\n  - [ ] 买菜\n    - [ ] 牛奶\n  - [x] 洗衣\n- [ ] 下周";
  ASSERT_TRUE(memomd::toggleCheck(text, 0));
  EXPECT_EQ(text, "- [x] 周末\n  - [x] 买菜\n    - [x] 牛奶\n  - [x] 洗衣\n- [ ] 下周");
  ASSERT_TRUE(memomd::toggleCheck(text, 0));
  EXPECT_EQ(text, "- [ ] 周末\n  - [ ] 买菜\n    - [ ] 牛奶\n  - [ ] 洗衣\n- [ ] 下周");
}

TEST(MemoMarkdown, CompletionPropagatesThroughEveryLevel) {
  std::string text = "- [ ] A\n  - [ ] B\n    - [ ] C\n\n    - [ ] D\n  - [x] E";
  ASSERT_TRUE(memomd::toggleCheck(text, 2));  // C
  EXPECT_EQ(text, "- [ ] A\n  - [ ] B\n    - [x] C\n\n    - [ ] D\n  - [x] E");
  ASSERT_TRUE(memomd::toggleCheck(text, 3));  // D, across the blank line: B then A complete
  EXPECT_EQ(text, "- [x] A\n  - [x] B\n    - [x] C\n\n    - [x] D\n  - [x] E");
}

TEST(MemoMarkdown, SubItemsEndAtTheFirstShallowerLine) {
  // "说明" is not indented, so the second list is not part of the first item.
  std::string text = "- [ ] 第一项\n说明\n  - [ ] 不是子项";
  ASSERT_TRUE(memomd::toggleCheck(text, 0));
  EXPECT_EQ(text, "- [x] 第一项\n说明\n  - [ ] 不是子项");
}
