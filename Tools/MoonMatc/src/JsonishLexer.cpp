/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "JsonishLexer.h"

#include <string.h>

namespace Moon::Matc {

JsonType JsonishLexer::readIdentifier() noexcept {
    const char* lexemeStart = mCursor;

    while (hasMore() && isIdentifierCharacter(*mCursor)) {
        consume();
    }

    size_t lexemeSize = mCursor - lexemeStart;

    // Check what kind of keyword we got here. The size is part of the comparison: strncmp
    // alone would also accept "trueValue" as the boolean true.
    if (lexemeSize == 4 && strncmp("true", lexemeStart, lexemeSize) == 0) {
        return BOOLEAN;
    } else if (lexemeSize == 5 && strncmp("false", lexemeStart, lexemeSize) == 0) {
        return BOOLEAN;
    } else if (lexemeSize == 4 && strncmp("null", lexemeStart, lexemeSize) == 0) {
        return NUll;
    } else {
        // Relax constraint where string MUST be surrounded by " characters.
        // All identifiers are treated as STRINGs.
        return STRING;
    }
}

void JsonishLexer::readString() noexcept {
    consume(); // Skip first double quote character.
    while (hasMore() && *mCursor != '"') {
        // An escaped quote is part of the string, not its end.
        if (*mCursor == '\\' && mCursor + 1 < mEnd) {
            consume();
        }
        consume();
    }
    if (hasMore()) {
        consume(); // Skip the last double quote character.
    }
}

JsonType JsonishLexer::readPunctuation() noexcept {
    char punctuation = consume();

    // Add lexeme.
    switch (punctuation) {
        case ',': return COMMA;
        case ':': return COLUMN;
        case '[': return ARRAY_START;
        case ']': return ARRAY_END;
        case '{': return BLOCK_START;
        case '}': return BLOCK_END;
        case '(': return PARENTHESIS_START;
        case ')': return PARENTHESIS_END;
        default : return SYMBOL; // Consumed a character the lexer does not name.
    }
}

void JsonishLexer::readNumber() noexcept {
    if (hasMore() && (isNumericCharacter(*mCursor) || *mCursor == '-')) {
        consume();
    }

    while(hasMore() && isNumericCharacter(*mCursor)) {
        consume();
    }

    if (!hasMore()) {
        return;
    }

    // Read fraction if present.
    if (*mCursor == '.') {
        consume();
        while (hasMore() && isNumericCharacter(*mCursor)) {
            consume();
        }
    }

    if (!hasMore()) {
        return;
    }

    // Read the exponent if present ("1e-6"): one token, so a value slice never has to
    // guess where the number ended.
    if (*mCursor == 'e' || *mCursor == 'E') {
        consume();
        if (hasMore() && (*mCursor == '+' || *mCursor == '-')) {
            consume();
        }
        while (hasMore() && isNumericCharacter(*mCursor)) {
            consume();
        }
    }
}

bool JsonishLexer::readLexeme() noexcept {
    skipWhiteSpace();

    if (!hasMore()) {
        return true;
    }

    const char* lexemeStart = mCursor;
    size_t line = getLine();
    size_t cursor = getCursor();

    JsonType lexemeType;
    switch (char c = peek()) {
        case '"' :
            readString();
            lexemeType = STRING;
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
        case '-':
            readNumber();
            lexemeType = NUMBER;
            break;
        case ',':
        case ':':
        case '[':
        case ']':
        case '{':
        case '}':
        case '(':
        case ')':
            lexemeType = readPunctuation();
            break;
        default:
            if (isIdentifierCharacter(c)) {
                lexemeType = readIdentifier();
            } else {
                // Punctuation the material DSL may carry (`#`, `.`, `+`, ...): consume it and
                // keep lexing. A strict JSON lexer would stop here, but this one feeds a
                // block parser whose value text is sliced from the source between lexemes.
                consume();
                lexemeType = SYMBOL;
            }
            break;
    }

    JsonLexeme lexeme(lexemeType, lexemeStart, mCursor - 1, line, cursor);
    lexeme = lexeme.trim();
    mLexemes.push_back(lexeme);

    return true;
}

} // namespace Moon::Matc
