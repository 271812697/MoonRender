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

#ifndef MOONMATC_JSONLEXEME_H
#define MOONMATC_JSONLEXEME_H

#include "Lexeme.h"

namespace Moon::Matc {

enum JsonType {
    BOOLEAN,
    NUll,
    STRING,
    NUMBER,
    BLOCK_START,
    BLOCK_END,
    COMMA,
    ARRAY_START,
    ARRAY_END,
    COLUMN,
    /** Parentheses are not JSON, but the material DSL writes GLSL initialisers after the
     * colon (`default : vec4(1.0, 0.5, 0.5, 1.0)`). They are tokens so the caller can keep
     * the commas inside a constructor from ending the property value. */
    PARENTHESIS_START,
    PARENTHESIS_END,
    /** Any other single character (the GTX/GLSL punctuation a value may contain). The text
     * between two lexemes is still sliced from getStart()/getEnd(), so the token only has
     * to let the lexer move past the character without stopping. */
    SYMBOL,
};

class JsonLexeme final: public Lexeme<JsonType > {
public:
    static const char* getTypeString(JsonType type) {
        switch (type) {
            case BOOLEAN: return "BOOLEAN";
            case NUll: return "NULL";
            case STRING: return "STRING";
            case NUMBER: return "NUMBER";
            case BLOCK_START: return "BLOCK_START";
            case BLOCK_END: return "BLOCK_END";
            case COMMA: return "COMMA";
            case ARRAY_START: return "ARRAY_START";
            case ARRAY_END: return "ARRAY_END";
            case COLUMN: return "COLUMN";
            case PARENTHESIS_START: return "PARENTHESIS_START";
            case PARENTHESIS_END: return "PARENTHESIS_END";
            case SYMBOL: return "SYMBOL";
        }
        return "UNKNOWN";
    }

    JsonLexeme(JsonType type, const char* start, const char* end, size_t line, size_t pos) :
            Lexeme(type, start, end, line, pos) {
    }

    // Remove double quote around the lexeme if it is a STRING.
    JsonLexeme trim() {
        if (mType != STRING) {
            return *this;
        }
        const char *start = (*mStart == '"') ? mStart + 1 : mStart;
        const char *end = (*mEnd == '"') ? mEnd - 1 : mEnd;
        return { mType, start, end, mLineNumber, mPosition };
    }

};

} // namespace Moon::Matc

#endif // MOONMATC_JSONLEXEME_H
