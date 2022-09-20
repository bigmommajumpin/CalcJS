#include <poincare/input_beautification.h>
#include "parsing/tokenizer.h"

namespace Poincare {
int InputBeautification::ApplyBeautification(Layout lastAddedLayout, LayoutCursor * layoutCursor, Context * context, bool forceCursorRightOfText) {
  Layout parent = lastAddedLayout.parent();
  assert(!parent.isUninitialized());
  int indexOfLastAddedLayout = parent.indexOfChild(lastAddedLayout);

  // Try beautifying the last added layout (ex: <=, ->, ...)
  for (BeautificationRule beautificationRule : convertWhenInputted) {
    if (ShouldBeBeautifiedWhenInputted(parent, indexOfLastAddedLayout, beautificationRule)) {
      assert(beautificationRule.listOfBeautifiedAliases.mainAlias() == beautificationRule.listOfBeautifiedAliases);
      // This does not work with multiple aliases
      int startIndexOfBeautification = indexOfLastAddedLayout - strlen(beautificationRule.listOfBeautifiedAliases.mainAlias()) + 1;
      RemoveLayoutsBetweenIndexAndReplaceWithPattern(parent, startIndexOfBeautification, indexOfLastAddedLayout, beautificationRule, layoutCursor, false, forceCursorRightOfText);
      return startIndexOfBeautification;
    }
  }

  /* From now on, trigger the beautification only if a non-identifier layout
   * was inputted. */
  if (lastAddedLayout.type() == LayoutNode::Type::CodePointLayout && Tokenizer::IsIdentifierMaterial(static_cast<CodePointLayout&>(lastAddedLayout).codePoint())) {
    return indexOfLastAddedLayout;
  }

  // Get the identifiers string preceding this last input.
  int lastIndexOfIdentifier = indexOfLastAddedLayout - 1;
  if (lastIndexOfIdentifier < 0) {
    return indexOfLastAddedLayout;
  }
  int firstIndexOfIdentifier = lastIndexOfIdentifier;
  Layout currentLayout = parent.childAtIndex(firstIndexOfIdentifier);
  while (currentLayout.type() == LayoutNode::Type::CodePointLayout
         && Tokenizer::IsIdentifierMaterial(static_cast<CodePointLayout&>(currentLayout).codePoint())) {
    firstIndexOfIdentifier--;
    if (firstIndexOfIdentifier < 0) {
      break;
    }
    currentLayout = parent.childAtIndex(firstIndexOfIdentifier);
  }
  firstIndexOfIdentifier++;
  if (firstIndexOfIdentifier > lastIndexOfIdentifier) {
    return indexOfLastAddedLayout;
  }
  int result = firstIndexOfIdentifier;

  // Fill the buffer with the identifiers string
  constexpr static int bufferSize = 220; // TODO : Factorize with TextField::k_maxBufferSize
  char identifiersString[bufferSize];
  int bufferCurrentLength = 0;
  for (int i = firstIndexOfIdentifier; i <= lastIndexOfIdentifier; i++) {
    Layout currentChild = parent.childAtIndex(i);
    assert(currentChild.type() == LayoutNode::Type::CodePointLayout);
    CodePoint c = static_cast<CodePointLayout&>(currentChild).codePoint();
    // This does not add null termination
    UTF8Decoder::CodePointToChars(c, identifiersString + bufferCurrentLength, bufferSize - bufferCurrentLength);
    bufferCurrentLength += UTF8Decoder::CharSizeOfCodePoint(c);
  }
  identifiersString[bufferCurrentLength] = 0;

  // Tokenize the identifiers string (ex: xpiabs = x*pi*abs)
  ParsingContext parsingContext(context, ParsingContext::ParsingMethod::Classic);
  Tokenizer tokenizer = Tokenizer(identifiersString, &parsingContext);
  Token currentIdentifier = tokenizer.popToken();
  Token nextIdentifier = Token(Token::Undefined);
  while (currentIdentifier.type() != Token::EndOfStream) {
    nextIdentifier = tokenizer.popToken();
    int numberOfLayoutsAddedOrRemoved = 0;
    // Try to beautify each token
    for (BeautificationRule beautificationRule : convertWhenFollowedByANonIdentifierChar) {
      int comparison = CompareAndBeautifyIdentifier(currentIdentifier.text(), currentIdentifier.length(), beautificationRule, parent, firstIndexOfIdentifier, &numberOfLayoutsAddedOrRemoved, layoutCursor, false, forceCursorRightOfText);
      if (comparison <= 0) { // Break if equal or past the alphabetical order
        break;
      }
    }
    // Only the last token can be a function
    if (nextIdentifier.type() == Token::EndOfStream && numberOfLayoutsAddedOrRemoved == 0 && lastAddedLayout.type() == LayoutNode::Type::ParenthesisLayout) {
      for (BeautificationRule beautificationRule : convertWhenFollowedByParentheses) {
        int comparison = CompareAndBeautifyIdentifier(currentIdentifier.text(), currentIdentifier.length(), beautificationRule, parent, firstIndexOfIdentifier, &numberOfLayoutsAddedOrRemoved, layoutCursor, true, forceCursorRightOfText);
        if (comparison <= 0) { // Break if equal or past the alphabetical order
          break;
        }
      }
    }
    firstIndexOfIdentifier += currentIdentifier.length() + numberOfLayoutsAddedOrRemoved;
    currentIdentifier = nextIdentifier;
  }
  return result;
}

bool InputBeautification::ShouldBeBeautifiedWhenInputted(Layout parent, int indexOfLastAddedLayout, BeautificationRule beautificationRule) {
  assert(beautificationRule.listOfBeautifiedAliases.mainAlias() == beautificationRule.listOfBeautifiedAliases);
  // This does not work with multiple aliases
  const char * pattern = beautificationRule.listOfBeautifiedAliases.mainAlias();
  int length = strlen(pattern);
  if (indexOfLastAddedLayout + 1 < length) {
    return false;
  }
  // Compare the code points inputted with the the pattern
  for (int i = 0; i < length; i++) {
    Layout child = parent.childAtIndex(indexOfLastAddedLayout - (length - 1) + i);
    if (child.type() != LayoutNode::Type::CodePointLayout || static_cast<CodePointLayout&>(child).codePoint() != pattern[i]) {
      return false;
    }
  }
  return true;
}

int InputBeautification::CompareAndBeautifyIdentifier(const char * identifier, size_t identifierLength, BeautificationRule beautificationRule, Layout parent, int startIndex, int * numberOfLayoutsAddedOrRemoved, LayoutCursor * layoutCursor, bool isBeautifyingFunction, bool forceCursorRightOfText) {
  if (isBeautifyingFunction) {
    // TODO
    return -1;
  }
  AliasesList patternAliases = beautificationRule.listOfBeautifiedAliases;
  int comparison = patternAliases.maxDifferenceWith(identifier, identifierLength);
  if (comparison == 0) {
    *numberOfLayoutsAddedOrRemoved = RemoveLayoutsBetweenIndexAndReplaceWithPattern(parent, startIndex, startIndex + identifierLength - 1, beautificationRule, layoutCursor, isBeautifyingFunction, forceCursorRightOfText);
  }
  return comparison;
}

int InputBeautification::RemoveLayoutsBetweenIndexAndReplaceWithPattern(Layout parent, int startIndex, int endIndex, BeautificationRule beautificationRule, LayoutCursor * layoutCursor, bool isBeautifyingFunction, bool forceCursorRightOfText) {
  int currentNumberOfChildren = parent.numberOfChildren();
  // Create pattern layout
  Layout inserted = beautificationRule.layoutBuilder();
  if (isBeautifyingFunction) {
    // TODO
    return 0;
  }
  // Remove layout
  LayoutCursor tempCursor = layoutCursor->clone(); // avoid altering the cursor by cloning it.
  for (int i = endIndex; i >= startIndex; i--) {
    parent.removeChildAtIndex(i, &tempCursor, true);
  }
  if (!forceCursorRightOfText && isBeautifyingFunction) {
    // Put the cursor inside the beautified function.
    // TODO
  }
  // Replace input with pattern
  tempCursor.addLayoutAndMoveCursor(inserted);
  if (layoutCursor->layout().isUninitialized() || !layoutCursor->layout().hasAncestor(parent, true)) {
    // Pointed layout has been deleted by beautification. Use the temp cursor.
    layoutCursor->setLayout(tempCursor.layout());
    layoutCursor->setPosition(tempCursor.position());
  }
  return parent.numberOfChildren() - currentNumberOfChildren;
}

}