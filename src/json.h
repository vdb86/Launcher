// json.h - a small, self-contained JSON parser for the Launcher config.
//
// No external dependencies (only the C++ standard library), so it drops into the build with no
// changes to build.bat / CMakeLists. Parses the full JSON grammar (objects, arrays, strings with
// escapes incl. \uXXXX + surrogate pairs, numbers, true/false/null). Input is a UTF-8 byte string;
// string values are stored as UTF-8 std::string and converted to std::wstring at the call site
// (see Utf8ToWide in main.cpp) so this header stays platform-independent.
//
// Usage:
//   std::string err;
//   json::Value root = json::parse(utf8Bytes, err);
//   if (!err.empty()) { ...report err...; }
//   const json::Value* cats = root.find("categories");
//   if (cats && cats->isArray()) for (const json::Value& c : cats->arr) { ... }
//
// GPLv3 (part of the Launcher project).
#pragma once
#include <string>
#include <vector>
#include <utility>

namespace json {

enum class Type { Null, Bool, Number, String, Array, Object };

struct Value
{
    Type type = Type::Null;
    bool        boolean = false;
    double      number  = 0.0;
    std::string str;                                  // UTF-8
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;   // insertion order preserved

    bool isNull()   const { return type == Type::Null;   }
    bool isBool()   const { return type == Type::Bool;   }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray()  const { return type == Type::Array;  }
    bool isObject() const { return type == Type::Object; }

    // Object member lookup by key (nullptr if not an object or key absent).
    const Value* find(const char* key) const
    {
        if (type != Type::Object) return nullptr;
        for (const auto& kv : obj)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }

    // Typed getters with defaults (safe on any node).
    std::string asString(const std::string& def = std::string()) const { return type == Type::String ? str : def; }
    bool        asBool  (bool   def = false) const { return type == Type::Bool   ? boolean : def; }
    double      asNumber(double def = 0.0)   const { return type == Type::Number ? number  : def; }

    // Object convenience getters.
    std::string getStr (const char* key, const std::string& def = std::string()) const { const Value* v = find(key); return v ? v->asString(def) : def; }
    bool        getBool(const char* key, bool   def = false) const { const Value* v = find(key); return v ? v->asBool(def)   : def; }
    double      getNum (const char* key, double def = 0.0)   const { const Value* v = find(key); return v ? v->asNumber(def) : def; }
};

namespace detail {

struct Parser
{
    const char* p;
    const char* end;
    int         line = 1;
    std::string error;

    explicit Parser(const std::string& s) : p(s.data()), end(s.data() + s.size()) {}

    void fail(const char* msg)
    {
        if (error.empty())
            error = std::string("JSON parse error (line ") + std::to_string(line) + "): " + msg;
    }

    void skipWs()
    {
        while (p < end)
        {
            char c = *p;
            if (c == '\n') { line++; p++; }
            else if (c == ' ' || c == '\t' || c == '\r') p++;
            else if (c == '/' && p + 1 < end && p[1] == '/')   // tolerate // line comments
            {
                p += 2;
                while (p < end && *p != '\n') p++;
            }
            else if (c == '/' && p + 1 < end && p[1] == '*')   // tolerate /* block */ comments
            {
                p += 2;
                while (p + 1 < end && !(p[0] == '*' && p[1] == '/')) { if (*p == '\n') line++; p++; }
                if (p + 1 < end) p += 2;
            }
            else break;
        }
    }

    // Encode a Unicode code point as UTF-8 into out.
    static void appendUtf8(std::string& out, unsigned cp)
    {
        if (cp <= 0x7F) out.push_back((char)cp);
        else if (cp <= 0x7FF)
        {
            out.push_back((char)(0xC0 | (cp >> 6)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            out.push_back((char)(0xE0 | (cp >> 12)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back((char)(0xF0 | (cp >> 18)));
            out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (cp & 0x3F)));
        }
    }

    bool parseHex4(unsigned& out)
    {
        if (end - p < 4) return false;
        unsigned v = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = p[i];
            v <<= 4;
            if      (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
            else return false;
        }
        p += 4;
        out = v;
        return true;
    }

    bool parseString(std::string& out)
    {
        if (p >= end || *p != '"') { fail("expected string"); return false; }
        p++; // opening quote
        while (p < end)
        {
            char c = *p++;
            if (c == '"') return true;
            if (c == '\\')
            {
                if (p >= end) { fail("unterminated escape"); return false; }
                char e = *p++;
                switch (e)
                {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u':
                {
                    unsigned cp = 0;
                    if (!parseHex4(cp)) { fail("bad \\u escape"); return false; }
                    if (cp >= 0xD800 && cp <= 0xDBFF) // high surrogate -> expect low surrogate
                    {
                        if (end - p >= 2 && p[0] == '\\' && p[1] == 'u')
                        {
                            p += 2;
                            unsigned lo = 0;
                            if (!parseHex4(lo)) { fail("bad low surrogate"); return false; }
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else { appendUtf8(out, cp); cp = lo; } // unpaired; emit both
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("invalid escape character"); return false;
                }
            }
            else
            {
                if ((unsigned char)c < 0x20) { fail("control character in string"); return false; }
                out.push_back(c); // raw byte (UTF-8 passes through untouched)
            }
        }
        fail("unterminated string");
        return false;
    }

    bool parseNumber(Value& v)
    {
        const char* start = p;
        if (p < end && *p == '-') p++;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' ||
                           *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) p++;
        std::string num(start, p);
        if (num.empty()) { fail("invalid number"); return false; }
        try { v.number = std::stod(num); }
        catch (...) { fail("invalid number"); return false; }
        v.type = Type::Number;
        return true;
    }

    bool matchLiteral(const char* lit)
    {
        size_t n = 0; while (lit[n]) n++;
        if ((size_t)(end - p) < n) return false;
        for (size_t i = 0; i < n; ++i) if (p[i] != lit[i]) return false;
        p += n;
        return true;
    }

    bool parseValue(Value& v, int depth)
    {
        if (depth > 200) { fail("nesting too deep"); return false; }
        skipWs();
        if (p >= end) { fail("unexpected end of input"); return false; }
        char c = *p;
        if (c == '{') return parseObject(v, depth);
        if (c == '[') return parseArray(v, depth);
        if (c == '"') { v.type = Type::String; return parseString(v.str); }
        if (c == 't') { if (matchLiteral("true"))  { v.type = Type::Bool; v.boolean = true;  return true; } fail("invalid token"); return false; }
        if (c == 'f') { if (matchLiteral("false")) { v.type = Type::Bool; v.boolean = false; return true; } fail("invalid token"); return false; }
        if (c == 'n') { if (matchLiteral("null"))  { v.type = Type::Null; return true; } fail("invalid token"); return false; }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(v);
        fail("unexpected character");
        return false;
    }

    bool parseArray(Value& v, int depth)
    {
        v.type = Type::Array;
        p++; // [
        skipWs();
        if (p < end && *p == ']') { p++; return true; }
        for (;;)
        {
            Value elem;
            if (!parseValue(elem, depth + 1)) return false;
            v.arr.push_back(std::move(elem));
            skipWs();
            if (p >= end) { fail("unterminated array"); return false; }
            if (*p == ',') { p++; skipWs(); if (p < end && *p == ']') { p++; return true; } continue; } // allow trailing comma
            if (*p == ']') { p++; return true; }
            fail("expected ',' or ']'");
            return false;
        }
    }

    bool parseObject(Value& v, int depth)
    {
        v.type = Type::Object;
        p++; // {
        skipWs();
        if (p < end && *p == '}') { p++; return true; }
        for (;;)
        {
            skipWs();
            if (p >= end || *p != '"') { fail("expected object key string"); return false; }
            std::string key;
            if (!parseString(key)) return false;
            skipWs();
            if (p >= end || *p != ':') { fail("expected ':' after key"); return false; }
            p++;
            Value val;
            if (!parseValue(val, depth + 1)) return false;
            v.obj.emplace_back(std::move(key), std::move(val));
            skipWs();
            if (p >= end) { fail("unterminated object"); return false; }
            if (*p == ',') { p++; skipWs(); if (p < end && *p == '}') { p++; return true; } continue; } // allow trailing comma
            if (*p == '}') { p++; return true; }
            fail("expected ',' or '}'");
            return false;
        }
    }
};

} // namespace detail

// Parse UTF-8 JSON text. On error, `errorOut` is set to a human-readable message and the returned
// Value is Null. A leading UTF-8 BOM is tolerated.
inline Value parse(const std::string& text, std::string& errorOut)
{
    errorOut.clear();
    std::string body = text;
    if (body.size() >= 3 && (unsigned char)body[0] == 0xEF &&
        (unsigned char)body[1] == 0xBB && (unsigned char)body[2] == 0xBF)
        body.erase(0, 3); // strip BOM

    detail::Parser parser(body);
    Value root;
    if (!parser.parseValue(root, 0))
    {
        errorOut = parser.error.empty() ? "unknown JSON parse error" : parser.error;
        return Value();
    }
    parser.skipWs();
    if (parser.p != parser.end)
    {
        errorOut = "trailing content after JSON value";
        return Value();
    }
    return root;
}

} // namespace json
