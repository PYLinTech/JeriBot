#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace JeriBot {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;

    Json() : type_(Type::Null) {}
    Json(std::nullptr_t) : type_(Type::Null) {}
    Json(bool v) : type_(Type::Bool), bool_(v) {}
    Json(int v) : type_(Type::Number), number_(v) {}
    Json(double v) : type_(Type::Number), number_(v) {}
    Json(const char* v) : type_(Type::String), string_(v) {}
    Json(const std::string& v) : type_(Type::String), string_(v) {}
    Json(std::string&& v) : type_(Type::String), string_(std::move(v)) {}
    Json(const Array& v) : type_(Type::Array), array_(v) {}
    Json(Array&& v) : type_(Type::Array), array_(std::move(v)) {}
    Json(const Object& v) : type_(Type::Object), object_(v) {}
    Json(Object&& v) : type_(Type::Object), object_(std::move(v)) {}

    Json(const Json& other);
    Json(Json&& other) noexcept;
    Json& operator=(const Json& other);
    Json& operator=(Json&& other) noexcept;
    ~Json();

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    bool asBool() const;
    double asNumber() const;
    int asInt() const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    Json& operator[](const std::string& key);
    const Json& operator[](const std::string& key) const;
    Json& operator[](size_t index);
    const Json& operator[](size_t index) const;

    size_t size() const;
    bool contains(const std::string& key) const;

    std::string dump(int indent = -1) const;

    static Json parse(const std::string& text, std::string& error);
    static Json loadFile(const std::string& path, std::string& error);

private:
    Type type_;

    union {
        bool bool_;
        double number_;
        std::string string_;
        Array array_;
        Object object_;
    };

    void destroy();
    void copyFrom(const Json& other);
    void moveFrom(Json&& other) noexcept;
};

} // namespace JeriBot