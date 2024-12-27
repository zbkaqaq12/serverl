#pragma once
#include "json.hpp"
#include <string>

using json = nlohmann::json;

class JsonUtil {
public:
    // 解析JSON字符串
    static bool parse(const std::string& input, json& output) {
        try {
            output = json::parse(input);
            return true;
        } catch(...) {
            return false;
        }
    }

    // 验证JSON格式
    static bool isValid(const std::string& input) {
        try {
            json::parse(input);
            return true;
        } catch(...) {
            return false;
        }
    }

    // 转换为字符串
    static std::string toString(const json& j) {
        return j.dump();
    }
};
