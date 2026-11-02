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

#include "JsonishParser.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace Moon::Matc
{
    namespace
    {
        std::string ResolveEscapes(const std::string& p_string)
        {
            std::string out;
            out.reserve(p_string.length());

            bool inEscape = false;
            for (const char c : p_string)
            {
                if (inEscape)
                {
                    switch (c)
                    {
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case 'n': out += '\n'; break;
                    case 'f': out += '\f'; break;
                    case 'b': out += '\b'; break;
                    case '"': out += '"'; break;
                    case 'u':
                        // TODO: implement unicode escape sequences
                        std::cerr << "Unicode escape sequences currently not supported." << std::endl;
                        break;
                    default:
                        std::cerr << "Invalid escape sequence \\" << c << "." << std::endl;
                        return out;
                    }
                    inEscape = false;
                }
                else if (c == '\\')
                {
                    inEscape = true;
                }
                else
                {
                    out += c;
                }
            }

            if (inEscape)
            {
                std::cerr << "Incomplete escape sequence." << std::endl;
            }

            return out;
        }
    }

    JsonishString::JsonishString(const std::string& p_string, bool p_resolveEscapes)
        : JsonishValue(Type::String), mString(p_resolveEscapes ? ResolveEscapes(p_string) : p_string)
    {
    }

    std::unique_ptr<JsonishObject> JsonishParser::parse() noexcept
    {
        return std::unique_ptr<JsonishObject>(parseObject());
    }

    JsonishObject* JsonishParser::parseObject() noexcept
    {
        if (!consumeLexeme(JsonType::BLOCK_START))
        {
            reportError("expected token '{'");
            return nullptr;
        }

        // Empty blocks are allowed.
        if (consumeLexeme(JsonType::BLOCK_END))
        {
            return new JsonishObject();
        }

        JsonishObject* object = parseMembers();
        if (object == nullptr)
        {
            return nullptr;
        }

        if (!consumeLexeme(JsonType::BLOCK_END))
        {
            reportError("expected token '}'");
            delete object;
            return nullptr;
        }

        return object;
    }

    JsonishArray* JsonishParser::parseArray() noexcept
    {
        if (!consumeLexeme(JsonType::ARRAY_START))
        {
            reportError("expected token '['");
            return nullptr;
        }

        // Empty arrays are allowed.
        if (consumeLexeme(JsonType::ARRAY_END))
        {
            return new JsonishArray();
        }

        JsonishArray* array = parseElements();
        if (array == nullptr)
        {
            return nullptr;
        }

        if (!consumeLexeme(JsonType::ARRAY_END))
        {
            reportError("expected token ']'");
            delete array;
            return nullptr;
        }

        return array;
    }

    JsonishPair JsonishParser::parsePair() noexcept
    {
        JsonishPair pair;

        const JsonLexeme* keyLexeme = consumeLexeme(JsonType::STRING);
        if (keyLexeme == nullptr)
        {
            return pair;
        }

        if (!consumeLexeme(JsonType::COLUMN))
        {
            return pair;
        }

        JsonishValue* value = parseValue();
        if (value == nullptr)
        {
            return pair;
        }

        pair.key = keyLexeme->getStringValue();
        pair.value = value;
        return pair;
    }

    const JsonLexeme* JsonishParser::consumeLexeme(JsonType p_type) noexcept
    {
        if (mCursor >= mLexemes.size())
        {
            return nullptr;
        }

        const JsonLexeme* lexeme = &mLexemes[mCursor];
        if (lexeme->getType() != p_type)
        {
            return nullptr;
        }

        ++mCursor;
        return lexeme;
    }

    const JsonLexeme* JsonishParser::peekNextLexemeType() const noexcept
    {
        return mCursor < mLexemes.size() ? &mLexemes[mCursor] : nullptr;
    }

    const JsonLexeme* JsonishParser::peekCurrentLexemeType() const noexcept
    {
        return mCursor > 0 ? &mLexemes[mCursor - 1] : nullptr;
    }

    JsonishObject* JsonishParser::parseMembers() noexcept
    {
        JsonishObject* object = new JsonishObject();

        while (true)
        {
            const JsonishPair pair = parsePair();
            if (pair.value == nullptr)
            {
                // Accept a comma after the last member.
                const JsonLexeme* lexeme = peekNextLexemeType();
                if (lexeme != nullptr && lexeme->getType() == JsonType::BLOCK_END)
                {
                    break;
                }

                reportError("unable to parse pair");
                delete object;
                return nullptr;
            }

            object->addPair(pair);

            if (!consumeLexeme(JsonType::COMMA))
            {
                break;
            }
        }

        return object;
    }

    JsonishArray* JsonishParser::parseElements() noexcept
    {
        JsonishArray* array = new JsonishArray();

        while (true)
        {
            JsonishValue* value = parseValue();
            if (value == nullptr)
            {
                // Accept a comma after the last element.
                const JsonLexeme* lexeme = peekNextLexemeType();
                if (lexeme != nullptr && lexeme->getType() == JsonType::ARRAY_END)
                {
                    break;
                }

                reportError("unable to read value");
                delete array;
                return nullptr;
            }
            array->addValue(value);

            if (!consumeLexeme(JsonType::COMMA))
            {
                break;
            }
        }

        return array;
    }

    const JsonLexeme* JsonishParser::consumeParenthesizedGroup() noexcept
    {
        size_t depth = 0;
        while (mCursor < mLexemes.size())
        {
            const JsonLexeme* lexeme = &mLexemes[mCursor++];
            if (lexeme->getType() == JsonType::PARENTHESIS_START)
            {
                ++depth;
            }
            else if (lexeme->getType() == JsonType::PARENTHESIS_END)
            {
                --depth;
                if (depth == 0)
                {
                    return lexeme;
                }
            }
        }

        reportError("unterminated '(' group");
        return nullptr;
    }

    JsonishValue* JsonishParser::parseString() noexcept
    {
        const JsonLexeme* stringLexeme = consumeLexeme(JsonType::STRING);
        if (stringLexeme == nullptr)
        {
            return nullptr;
        }

        const JsonLexeme* next = peekNextLexemeType();

        // A GLSL initialiser: `vec4(0.5, 0.5, 0.5, 1.0)`. The whole constructor is kept as
        // one string value, exactly as it was written.
        if (next != nullptr && next->getType() == JsonType::PARENTHESIS_START)
        {
            const JsonLexeme* last = consumeParenthesizedGroup();
            if (last == nullptr)
            {
                return nullptr;
            }
            const std::string text(stringLexeme->getStart(),
                last->getEnd() - stringLexeme->getStart() + 1);
            return new JsonishString(text, false);
        }

        // Strings optionally have an array suffix, as in "float[9]". Keep the suffix in the
        // string's text; it is the type spelling the material file uses.
        if (next != nullptr && next->getType() == JsonType::ARRAY_START)
        {
            JsonishArray* array = parseArray();
            if (array == nullptr)
            {
                return nullptr;
            }
            delete array;

            const JsonLexeme* after = peekNextLexemeType();
            if (after != nullptr)
            {
                const size_t length = after->getStart() - stringLexeme->getStart();
                return new JsonishString(std::string(stringLexeme->getStart(), length));
            }

            const JsonLexeme* last = peekCurrentLexemeType();
            const size_t length = last->getEnd() - stringLexeme->getStart() + 1;
            return new JsonishString(std::string(stringLexeme->getStart(), length));
        }

        return new JsonishString(std::string(stringLexeme->getStart(), stringLexeme->getSize()));
    }

    JsonishValue* JsonishParser::parseValue() noexcept
    {
        const JsonLexeme* next = peekNextLexemeType();
        if (next == nullptr)
        {
            return nullptr;
        }

        switch (next->getType())
        {
        case JsonType::STRING:
            return parseString();
        case JsonType::NUMBER:
        {
            consumeLexeme(JsonType::NUMBER);
            return new JsonishNumber(
                std::strtof(next->getStart(), nullptr), next->getStringValue());
        }
        case JsonType::BLOCK_START:
            return parseObject();
        case JsonType::ARRAY_START:
            return parseArray();
        case JsonType::BOOLEAN:
        {
            consumeLexeme(JsonType::BOOLEAN);
            return new JsonishBool(next->getStringValue() == "true");
        }
        case JsonType::NUll:
            consumeLexeme(JsonType::NUll);
            return new JsonishNull();
        case JsonType::PARENTHESIS_START:
        {
            // A bare parenthesised value (`(1.0)`) is kept raw as well.
            const JsonLexeme* last = consumeParenthesizedGroup();
            if (last == nullptr)
            {
                return nullptr;
            }
            return new JsonishString(
                std::string(next->getStart(), last->getEnd() - next->getStart() + 1), false);
        }
        default:
            reportError("unexpected token");
            return nullptr;
        }
    }

    void JsonishParser::reportError(const char* p_message) noexcept
    {
        if (mErrorReported)
        {
            return;
        }

        std::cerr << "Syntax error, " << p_message;

        const JsonLexeme* lexeme = peekNextLexemeType();
        if (lexeme == nullptr)
        {
            lexeme = peekCurrentLexemeType();
        }

        if (lexeme != nullptr)
        {
            std::cerr << " at line:" << lexeme->getLine()
                << " position:" << lexeme->getLinePosition() << std::endl
                << "  got lexeme type: "
                << JsonLexeme::getTypeString(lexeme->getType()) << std::endl
                << "  got lexeme value: " << lexeme->getStringValue() << std::endl;
        }
        else
        {
            std::cerr << " but reached end of file instead." << std::endl;
        }

        mErrorReported = true;
    }
}
