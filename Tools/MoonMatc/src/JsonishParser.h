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

#ifndef MOONMATC_JSONISHPARSER_H
#define MOONMATC_JSONISHPARSER_H

#include "JsonishLexeme.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Moon::Matc {

// The parsed form of the material DSL: the same shape as filament-matp's JsonishParser.
//
//   object  : {} | { members }
//   members : pair | pair , members
//   pair    : string : value
//   array   : [] | [ elements ]
//   value   : string | number | object | array | true | false | null
//
// The material DSL also writes GLSL initialisers (`default : vec4(1.0, ...)`). Those are
// stored as a STRING whose text is the exact source slice, so a consumer never has to
// re-parse the value: it asks the object for the key and reads the value it needs.

class JsonishNull;
class JsonishBool;
class JsonishNumber;
class JsonishString;
class JsonishArray;
class JsonishObject;

class JsonishValue
{
public:
    enum class Type
    {
        String,
        Number,
        Object,
        Array,
        Bool,
        Null,
    };

    static const char* TypeToString(Type p_type)
    {
        switch (p_type)
        {
        case Type::String: return "STRING";
        case Type::Number: return "NUMBER";
        case Type::Object: return "OBJECT";
        case Type::Array: return "ARRAY";
        case Type::Bool: return "BOOL";
        case Type::Null: return "NULL";
        }
        return "NULL";
    }

    const JsonishBool* toJsonBool() const
    {
        return mType == Type::Bool ? reinterpret_cast<const JsonishBool*>(this) : nullptr;
    }

    const JsonishNumber* toJsonNumber() const
    {
        return mType == Type::Number ? reinterpret_cast<const JsonishNumber*>(this) : nullptr;
    }

    const JsonishString* toJsonString() const
    {
        return mType == Type::String ? reinterpret_cast<const JsonishString*>(this) : nullptr;
    }

    const JsonishObject* toJsonObject() const
    {
        return mType == Type::Object ? reinterpret_cast<const JsonishObject*>(this) : nullptr;
    }

    const JsonishArray* toJsonArray() const
    {
        return mType == Type::Array ? reinterpret_cast<const JsonishArray*>(this) : nullptr;
    }

    explicit JsonishValue(Type p_type) : mType(p_type) {}
    virtual ~JsonishValue() = default;

    Type getType() const { return mType; }

protected:
    Type mType;
};

class JsonishNull final: public JsonishValue
{
public:
    JsonishNull() : JsonishValue(Type::Null) {}
    ~JsonishNull() override = default;
};

class JsonishBool final: public JsonishValue
{
public:
    explicit JsonishBool(bool p_bool) : JsonishValue(Type::Bool), mBool(p_bool) {}
    ~JsonishBool() override = default;

    bool getBool() const { return mBool; }

private:
    bool mBool;
};

class JsonishNumber final: public JsonishValue
{
public:
    JsonishNumber(float p_float, std::string p_text)
        : JsonishValue(Type::Number), mFloat(p_float), mText(std::move(p_text)) {}
    ~JsonishNumber() override = default;

    float getFloat() const { return mFloat; }

    /** The number exactly as it was written ("0.005", "1e-6"). The material file keeps the
     * designer values as text, so this is what the parameter table stores. */
    const std::string& getString() const { return mText; }

private:
    float mFloat;
    std::string mText;
};

class JsonishString final: public JsonishValue
{
public:
    /** p_resolveEscapes is false for the raw GLSL initialiser text, which is copied as is. */
    explicit JsonishString(const std::string& p_string, bool p_resolveEscapes = true);

    ~JsonishString() override = default;

    const std::string& getString() const { return mString; }

private:
    std::string mString;
};

struct JsonishPair
{
    std::string key;
    JsonishValue* value = nullptr;
};

class JsonishObject final: public JsonishValue
{
public:
    JsonishObject() : JsonishValue(Type::Object) {}

    ~JsonishObject() override
    {
        for (const auto& [key, value] : mMembers)
        {
            delete value;
        }
    }

    void addPair(const JsonishPair& p_pair)
    {
        if (const auto it = mMembers.find(p_pair.key); it != mMembers.end())
        {
            delete it->second;
        }
        mMembers[p_pair.key] = p_pair.value;
    }

    const std::unordered_map<std::string, JsonishValue*>& getEntries() const noexcept
    {
        return mMembers;
    }

    bool hasKey(const std::string& p_key) const noexcept
    {
        return mMembers.find(p_key) != mMembers.end();
    }

    const JsonishValue* getValue(const std::string& p_key) const noexcept
    {
        const auto it = mMembers.find(p_key);
        return it != mMembers.end() ? it->second : nullptr;
    }

private:
    std::unordered_map<std::string, JsonishValue*> mMembers;
};

class JsonishArray final: public JsonishValue
{
public:
    JsonishArray() : JsonishValue(Type::Array) {}

    ~JsonishArray() override
    {
        for (const JsonishValue* value : mElements)
        {
            delete value;
        }
    }

    void addValue(const JsonishValue* p_value) { mElements.push_back(p_value); }

    const std::vector<const JsonishValue*>& getElements() const noexcept { return mElements; }

private:
    std::vector<const JsonishValue*> mElements;
};

class JsonishParser
{
public:
    explicit JsonishParser(const std::vector<JsonLexeme>& p_lexemes)
        : mCursor(0), mLexemes(p_lexemes) {}

    std::unique_ptr<JsonishObject> parse() noexcept;

private:
    JsonishObject* parseObject() noexcept;
    JsonishArray* parseArray() noexcept;
    JsonishPair parsePair() noexcept;
    JsonishValue* parseValue() noexcept;
    JsonishObject* parseMembers() noexcept;
    JsonishArray* parseElements() noexcept;
    JsonishValue* parseString() noexcept;
    /** Consumes a balanced `( ... )` group, returning its closing lexeme. */
    const JsonLexeme* consumeParenthesizedGroup() noexcept;

    const JsonLexeme* consumeLexeme(JsonType p_type) noexcept;
    const JsonLexeme* peekNextLexemeType() const noexcept;
    const JsonLexeme* peekCurrentLexemeType() const noexcept;

    void reportError(const char* p_message) noexcept;

    size_t mCursor = 0;
    const std::vector<JsonLexeme>& mLexemes;
    bool mErrorReported = false;
};

} // namespace Moon::Matc

#endif // MOONMATC_JSONISHPARSER_H
