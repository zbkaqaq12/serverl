#pragma once

#include "request.h"
#include "response.h"
#include <vector>
#include <string>
#include "json.hpp"

using json = nlohmann::json;

class Middleware {
public:
    virtual ~Middleware() = default;
    
    // 处理请求的接口
    // 返回true继续处理链，返回false中断处理
    virtual bool handle(http::HttpRequest& request, http::HttpResponse& response) = 0;
};

// CORS中间件
class CorsMiddleware : public Middleware {
public:
    CorsMiddleware(const std::string& allowOrigin = "*",
                  const std::string& allowMethods = "GET, POST, PUT, DELETE, OPTIONS",
                  const std::string& allowHeaders = "Content-Type, Authorization");
    
    bool handle(http::HttpRequest& request, http::HttpResponse& response) override;

private:
    bool needCors(const std::string& path);
    std::string allowOrigin_;
    std::string allowMethods_;
    std::string allowHeaders_;
};

// 认证中间件
class AuthMiddleware : public Middleware {
public:
    explicit AuthMiddleware(const std::string& authHeader = "Authorization");
    bool handle(http::HttpRequest& request, http::HttpResponse& response) override;

private:
    bool isPublicPath(const std::string& path);
    void sendUnauthorized(http::HttpResponse& response, const std::string& message);
    json verifyToken(const std::string& token);
    std::vector<std::string> splitToken(const std::string& token);
    
    std::string authHeader_;
    std::string jwtSecret_;  // JWT密钥
};