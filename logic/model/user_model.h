#pragma once

#include <string>
#include "json.hpp"

using json = nlohmann::json;

namespace model {

class User {
public:
    User() = default;
    User(const std::string& id, const std::string& username, const std::string& email)
        : id_(id), username_(username), email_(email) {}

    // Getters
    const std::string& getId() const { return id_; }
    const std::string& getUsername() const { return username_; }
    const std::string& getEmail() const { return email_; }
    const std::string& getPassword() const { return password_; }
    const std::string& getCreatedAt() const { return created_at_; }
    const std::string& getUpdatedAt() const { return updated_at_; }

    // Setters
    void setId(const std::string& id) { id_ = id; }
    void setUsername(const std::string& username) { username_ = username; }
    void setEmail(const std::string& email) { email_ = email; }
    void setPassword(const std::string& password) { password_ = password; }
    void setCreatedAt(const std::string& created_at) { created_at_ = created_at; }
    void setUpdatedAt(const std::string& updated_at) { updated_at_ = updated_at; }

    // JSON序列化和反序列化
    json toJson() const {
        return {
            {"id", id_},
            {"username", username_},
            {"email", email_},
            {"created_at", created_at_},
            {"updated_at", updated_at_}
        };
    }

    static User fromJson(const json& j) {
        User user;
        user.setId(j["id"].get<std::string>());
        user.setUsername(j["username"].get<std::string>());
        user.setEmail(j["email"].get<std::string>());
        if (j.contains("password")) {
            user.setPassword(j["password"].get<std::string>());
        }
        if (j.contains("created_at")) {
            user.setCreatedAt(j["created_at"].get<std::string>());
        }
        if (j.contains("updated_at")) {
            user.setUpdatedAt(j["updated_at"].get<std::string>());
        }
        return user;
    }

private:
    std::string id_;
    std::string username_;
    std::string email_;
    std::string password_;  // 注意：实际存储时应该是加密的
    std::string created_at_;
    std::string updated_at_;
};

} // namespace model 