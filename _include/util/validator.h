// _include/util/validator.h
#pragma once
#include <string>
#include <regex>

class Validator {
public:
    // 验证邮箱格式
    static bool isValidEmail(const std::string& email) {
        const std::regex pattern("^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\\.[a-zA-Z]{2,}$");
        return std::regex_match(email, pattern);
    }

    // 验证手机号格式
    static bool isValidPhone(const std::string& phone) {
        const std::regex pattern("^1[3-9]\\d{9}$");
        return std::regex_match(phone, pattern);
    }

    // 验证用户名格式（字母开头，允许字母数字下划线，6-20位）
    static bool isValidUsername(const std::string& username) {
        const std::regex pattern("^[a-zA-Z][a-zA-Z0-9_]{5,19}$");
        return std::regex_match(username, pattern);
    }
};