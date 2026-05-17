#include "Json.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace JeriBot {

Json::Json(const Json& other) : type_(Type::Null)
{
    copyFrom(other);
}

Json::Json(Json&& other) noexcept : type_(Type::Null)
{
    moveFrom(std::move(other));
}

Json& Json::operator=(const Json& other)
{
    if (this != &other) {
        destroy();
        copyFrom(other);
    }
    return *this;
}

Json& Json::operator=(Json&& other) noexcept
{
    if (this != &other) {
        destroy();
        moveFrom(std::move(other));
    }
    return *this;
}

Json::~Json()
{
    destroy();
}

void Json::destroy()
{
    switch (type_) {
    case Type::String:
        string_.~basic_string();
        break;
    case Type::Array:
        array_.~vector();
        break;
    case Type::Object:
        object_.~map();
        break;
    default:
        break;
    }
    type_ = Type::Null;
}

void Json::copyFrom(const Json& other)
{
    type_ = other.type_;
    switch (type_) {
    case Type::Bool:
        bool_ = other.bool_;
        break;
    case Type::Number:
        number_ = other.number_;
        break;
    case Type::String:
        new (&string_) std::string(other.string_);
        break;
    case Type::Array:
        new (&array_) Array(other.array_);
        break;
    case Type::Object:
        new (&object_) Object(other.object_);
        break;
    default:
        break;
    }
}

void Json::moveFrom(Json&& other) noexcept
{
    type_ = other.type_;
    switch (type_) {
    case Type::Bool:
        bool_ = other.bool_;
        break;
    case Type::Number:
        number_ = other.number_;
        break;
    case Type::String:
        new (&string_) std::string(std::move(other.string_));
        break;
    case Type::Array:
        new (&array_) Array(std::move(other.array_));
        break;
    case Type::Object:
        new (&object_) Object(std::move(other.object_));
        break;
    default:
        break;
    }
    other.type_ = Type::Null;
}

bool Json::asBool() const
{
    return bool_;
}

double Json::asNumber() const
{
    return number_;
}

int Json::asInt() const
{
    return static_cast<int>(number_);
}

const std::string& Json::asString() const
{
    return string_;
}

const Json::Array& Json::asArray() const
{
    return array_;
}

const Json::Object& Json::asObject() const
{
    return object_;
}

std::string& Json::asString()
{
    return string_;
}

Json::Array& Json::asArray()
{
    return array_;
}

Json::Object& Json::asObject()
{
    return object_;
}

Json& Json::operator[](const std::string& key)
{
    if (type_ == Type::Null) {
        type_ = Type::Object;
        new (&object_) Object();
    }
    return object_[key];
}

const Json& Json::operator[](const std::string& key) const
{
    static Json nullVal;
    auto it = object_.find(key);
    return it != object_.end() ? it->second : nullVal;
}

Json& Json::operator[](size_t index)
{
    return array_[index];
}

const Json& Json::operator[](size_t index) const
{
    return array_[index];
}

size_t Json::size() const
{
    switch (type_) {
    case Type::Array:
        return array_.size();
    case Type::Object:
        return object_.size();
    default:
        return 0;
    }
}

bool Json::contains(const std::string& key) const
{
    if (type_ != Type::Object) return false;
    return object_.find(key) != object_.end();
}

class JsonDumper {
public:
    JsonDumper(int indent) : indent_(indent) {}

    std::string dump(const Json& v)
    {
        switch (v.type()) {
        case Json::Type::Null:
            return "null";
        case Json::Type::Bool:
            return v.asBool() ? "true" : "false";
        case Json::Type::Number:
            return formatNumber(v.asNumber());
        case Json::Type::String:
            return formatString(v.asString());
        case Json::Type::Array:
            return formatArray(v.asArray());
        case Json::Type::Object:
            return formatObject(v.asObject());
        }
        return "null";
    }

private:
    int indent_;
    int depth_ = 0;

    std::string indentStr() const
    {
        if (indent_ <= 0) return {};
        return std::string(static_cast<size_t>(depth_ * indent_), ' ');
    }

    std::string newline() const
    {
        if (indent_ <= 0) return {};
        return "\n";
    }

    static std::string formatNumber(double v)
    {
        if (std::isfinite(v) && v == std::floor(v) && std::abs(v) < 1e15) {
            return std::to_string(static_cast<int64_t>(v));
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.17g", v);
        return buf;
    }

    static std::string formatString(const std::string& s)
    {
        std::string out = "\"";
        for (char c : s) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char hex[8];
                    snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned char>(c));
                    out += hex;
                } else {
                    out += c;
                }
                break;
            }
        }
        out += '"';
        return out;
    }

    std::string formatArray(const Json::Array& arr)
    {
        if (arr.empty()) return "[]";
        std::string sep = indent_ > 0 ? "," : ", ";
        std::string nl = newline();
        std::string ind = indentStr();
        std::string result = "[" + nl;
        ++depth_;
        std::string innerIndent = indentStr();
        --depth_;
        for (size_t i = 0; i < arr.size(); ++i) {
            result += innerIndent + dump(arr[i]);
            if (i + 1 < arr.size()) result += sep;
            result += nl;
        }
        result += ind + "]";
        return result;
    }

    std::string formatObject(const Json::Object& obj)
    {
        if (obj.empty()) return "{}";
        std::string sep = indent_ > 0 ? "," : ", ";
        std::string nl = newline();
        std::string ind = indentStr();
        std::string result = "{" + nl;
        ++depth_;
        std::string innerIndent = indentStr();
        --depth_;
        size_t i = 0;
        for (auto& [k, v] : obj) {
            result += innerIndent + formatString(k) + (indent_ > 0 ? ": " : ":") + dump(v);
            if (i + 1 < obj.size()) result += sep;
            result += nl;
            ++i;
        }
        result += ind + "}";
        return result;
    }
};

std::string Json::dump(int indent) const
{
    JsonDumper d(indent);
    return d.dump(*this);
}

class JsonParser {
public:
    JsonParser(const std::string& text) : text_(text), pos_(0) {}

    Json parse(std::string& error)
    {
        Json result = parseValue(error);
        if (error.empty()) {
            skipWhitespace();
            if (pos_ < text_.size()) {
                error = formatError("解析完成后存在多余内容");
            }
        }
        return result;
    }

private:
    std::string text_;
    size_t pos_;

    char peek()
    {
        return pos_ < text_.size() ? text_[pos_] : '\0';
    }

    char next()
    {
        return pos_ < text_.size() ? text_[pos_++] : '\0';
    }

    void skipWhitespace()
    {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    std::string formatError(const std::string& msg)
    {
        size_t line = 1, col = 1;
        for (size_t i = 0; i < pos_ && i < text_.size(); ++i) {
            if (text_[i] == '\n') { ++line; col = 1; }
            else { ++col; }
        }
        return "第" + std::to_string(line) + "行第" + std::to_string(col) + "列: " + msg;
    }

    Json parseValue(std::string& error)
    {
        skipWhitespace();
        if (pos_ >= text_.size()) {
            error = formatError("意外的结束，期望值");
            return {};
        }
        switch (peek()) {
        case 'n':
            return parseNull(error);
        case 't':
        case 'f':
            return parseBool(error);
        case '"':
            return parseString(error);
        case '[':
            return parseArray(error);
        case '{':
            return parseObject(error);
        case '-':
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
            return parseNumber(error);
        default:
            error = formatError("意外的字符 '" + std::string(1, peek()) + "'");
            return {};
        }
    }

    Json parseNull(std::string& error)
    {
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return {};
        }
        error = formatError("期望 'null'");
        return {};
    }

    Json parseBool(std::string& error)
    {
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return false;
        }
        error = formatError("期望布尔值");
        return {};
    }

    Json parseNumber(std::string& error)
    {
        size_t start = pos_;
        if (peek() == '-') next();

        if (peek() == '0') {
            next();
        } else if (peek() >= '1' && peek() <= '9') {
            while (peek() >= '0' && peek() <= '9') next();
        } else {
            error = formatError("无效数字");
            return {};
        }

        if (peek() == '.') {
            next();
            if (peek() < '0' || peek() > '9') {
                error = formatError("小数点后需要数字");
                return {};
            }
            while (peek() >= '0' && peek() <= '9') next();
        }

        if (peek() == 'e' || peek() == 'E') {
            next();
            if (peek() == '+' || peek() == '-') next();
            if (peek() < '0' || peek() > '9') {
                error = formatError("指数后需要数字");
                return {};
            }
            while (peek() >= '0' && peek() <= '9') next();
        }

        std::string numStr = text_.substr(start, pos_ - start);
        double val = 0;
        try {
            val = std::stod(numStr);
        } catch (...) {
            error = formatError("数字解析失败: " + numStr);
            return {};
        }
        return val;
    }

    std::string parseStringContent(std::string& error)
    {
        if (next() != '"') {
            error = formatError("期望 '\"'");
            return {};
        }
        std::string result;
        while (true) {
            if (pos_ >= text_.size()) {
                error = formatError("字符串未闭合");
                return {};
            }
            char c = next();
            if (c == '"') break;
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    error = formatError("字符串未闭合");
                    return {};
                }
                char esc = next();
                switch (esc) {
                case '"':
                    result += '"';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '/':
                    result += '/';
                    break;
                case 'b':
                    result += '\b';
                    break;
                case 'f':
                    result += '\f';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case 'r':
                    result += '\r';
                    break;
                case 't':
                    result += '\t';
                    break;
                case 'u': {
                    if (pos_ + 4 > text_.size()) {
                        error = formatError("不完整的Unicode转义");
                        return {};
                    }
                    std::string hex = text_.substr(pos_, 4);
                    pos_ += 4;
                    unsigned int cp = 0;
                    for (char h : hex) {
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                        else {
                            error = formatError("无效的Unicode十六进制");
                            return {};
                        }
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (pos_ + 6 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                            error = formatError("缺少代理对低半部分");
                            return {};
                        }
                        pos_ += 2;
                        if (pos_ + 4 > text_.size()) {
                            error = formatError("不完整的Unicode代理对");
                            return {};
                        }
                        std::string hex2 = text_.substr(pos_, 4);
                        pos_ += 4;
                        unsigned int lo = 0;
                        for (char h : hex2) {
                            lo <<= 4;
                            if (h >= '0' && h <= '9') lo |= h - '0';
                            else if (h >= 'a' && h <= 'f') lo |= h - 'a' + 10;
                            else if (h >= 'A' && h <= 'F') lo |= h - 'A' + 10;
                        }
                        if (lo < 0xDC00 || lo > 0xDFFF) {
                            error = formatError("无效的代理对低半部分");
                            return {};
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    encodeUtf8(cp, result);
                    break;
                }
                default:
                    error = formatError("无效的转义字符 '\\" + std::string(1, esc) + "'");
                    return {};
                }
            } else {
                result += c;
            }
        }
        return result;
    }

    static void encodeUtf8(unsigned int cp, std::string& out)
    {
        if (cp <= 0x7F) {
            out += static_cast<char>(cp);
        } else if (cp <= 0x7FF) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0xFFFF) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF) {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    Json parseString(std::string& error)
    {
        std::string s = parseStringContent(error);
        if (!error.empty()) return {};
        return s;
    }

    Json parseArray(std::string& error)
    {
        if (next() != '[') {
            error = formatError("期望 '['");
            return {};
        }
        skipWhitespace();
        Json::Array arr;
        if (peek() == ']') {
            next();
            return arr;
        }
        while (true) {
            Json val = parseValue(error);
            if (!error.empty()) return {};
            arr.push_back(std::move(val));
            skipWhitespace();
            if (peek() == ',') {
                next();
                skipWhitespace();
            } else if (peek() == ']') {
                next();
                break;
            } else {
                error = formatError("期望 ',' 或 ']'");
                return {};
            }
        }
        return arr;
    }

    Json parseObject(std::string& error)
    {
        if (next() != '{') {
            error = formatError("期望 '{'");
            return {};
        }
        skipWhitespace();
        Json::Object obj;
        if (peek() == '}') {
            next();
            return obj;
        }
        while (true) {
            skipWhitespace();
            std::string key = parseStringContent(error);
            if (!error.empty()) return {};
            skipWhitespace();
            if (next() != ':') {
                error = formatError("期望 ':'");
                return {};
            }
            skipWhitespace();
            Json val = parseValue(error);
            if (!error.empty()) return {};
            obj.emplace(std::move(key), std::move(val));
            skipWhitespace();
            if (peek() == ',') {
                next();
                skipWhitespace();
            } else if (peek() == '}') {
                next();
                break;
            } else {
                error = formatError("期望 ',' 或 '}'");
                return {};
            }
        }
        return obj;
    }
};

Json Json::parse(const std::string& text, std::string& error)
{
    error.clear();
    JsonParser parser(text);
    return parser.parse(error);
}

Json Json::loadFile(const std::string& path, std::string& error)
{
    error.clear();
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        error = "无法打开文件: " + path;
        return {};
    }
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();
    return parse(content, error);
}

} // namespace JeriBot