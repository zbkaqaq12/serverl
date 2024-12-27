#pragma once

#include <memory>
#include <vector>
#include <stdexcept>
#include "../model/user_model.h"

namespace service {

// 自定义异常类
class UserServiceException : public std::runtime_error {
public:
    explicit UserServiceException(const std::string& message) : std::runtime_error(message) {}
};

class UserService {
public:
    static UserService* GetInstance() {
        static UserService instance;
        return &instance;
    }

    // 创建用户
    model::User createUser(const json& userData) {
        // 验证用户数据
        validateUserData(userData);
        
        // 检查邮箱是否已存在
        if (isEmailExists(userData["email"])) {
            throw UserServiceException("Email already exists");
        }

        // TODO: 密码加密
        // TODO: 保存到数据库
        model::User user = model::User::fromJson(userData);
        // 设置创建时间等
        user.setCreatedAt(getCurrentTimestamp());
        user.setUpdatedAt(getCurrentTimestamp());

        return user;
    }

    // 获取用户列表
    std::vector<model::User> getUsers(int page = 1, int limit = 10) {
        // TODO: 从数据库获取用户列表
        std::vector<model::User> users;
        return users;
    }

    // 根据ID获取用户
    model::User getUserById(const std::string& id) {
        // TODO: 从数据库获取用户
        if (id.empty()) {
            throw UserServiceException("User not found");
        }
        return model::User();
    }

    // 更新用户
    model::User updateUser(const std::string& id, const json& userData) {
        auto user = getUserById(id);
        
        // 验证并更新数据
        if (userData.contains("username")) {
            user.setUsername(userData["username"]);
        }
        if (userData.contains("email")) {
            if (userData["email"] != user.getEmail() && isEmailExists(userData["email"])) {
                throw UserServiceException("Email already exists");
            }
            user.setEmail(userData["email"]);
        }
        
        user.setUpdatedAt(getCurrentTimestamp());
        // TODO: 保存到数据库
        
        return user;
    }

    // 删除用户
    void deleteUser(const std::string& id) {
        // 确保用户存在
        getUserById(id);
        // TODO: 从数据库删除用户
    }

private:
    UserService() = default;
    
    // 验证用户数据
    void validateUserData(const json& userData) {
        if (!userData.contains("username") || !userData.contains("email") || !userData.contains("password")) {
            throw UserServiceException("Missing required fields");
        }
        
        // 验证邮箱格式
        if (!isValidEmail(userData["email"])) {
            throw UserServiceException("Invalid email format");
        }
        
        // 验证密码强度
        if (!isValidPassword(userData["password"])) {
            throw UserServiceException("Password too weak");
        }
    }
    
    // 辅助方法
    bool isEmailExists(const std::string& email) {
        // TODO: 检查邮箱是否存在
        return false;
    }
    
    bool isValidEmail(const std::string& email) {
        // TODO: 实现邮箱格式验证
        return true;
    }
    
    bool isValidPassword(const std::string& password) {
        // TODO: 实现密码强度验证
        return password.length() >= 6;
    }
    
    std::string getCurrentTimestamp() {
        // TODO: 实现获取当前时间戳
        return "2023-12-24 00:00:00";
    }
};

} // namespace service 