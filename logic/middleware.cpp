#include "middleware.h"
#include "Logger.h"
#include <algorithm>
#include <unordered_map>
#include <ctime>
#include<jwt.h>

// CORS中间件实现
CorsMiddleware::CorsMiddleware(const std::string& allowOrigin,
                             const std::string& allowMethods,
                             const std::string& allowHeaders)
    : allowOrigin_(allowOrigin)
    , allowMethods_(allowMethods)
    , allowHeaders_(allowHeaders) {}

bool CorsMiddleware::handle(http::HttpRequest& request, http::HttpResponse& response) {
    auto logger = Logger::GetInstance();

    // 1. 检查是否需要CORS处理
    if (!needCors(request.getPath())) {
        return true;  // 不需要CORS处理，继续下一个中间件
    }

    // 2. 设置基本的CORS头部
    response.setHeader("Access-Control-Allow-Origin", allowOrigin_);
    response.setHeader("Access-Control-Allow-Methods", allowMethods_);
    response.setHeader("Access-Control-Allow-Headers", allowHeaders_);
    response.setHeader("Access-Control-Allow-Credentials", "true");

    // 3. 处理预检请求
    if (request.getMethod() == "OPTIONS") {
        response.setHeader("Access-Control-Max-Age", "86400"); // 24小时缓存预检请求结果
        response.setStatus(204); // No Content
        logger->logSystem(LogLevel::DEBUG, "Handled OPTIONS preflight request");
        return false; // 中断处理链，直接返回
    }

    logger->logSystem(LogLevel::DEBUG, "Applied CORS headers for %s request", request.getMethod().c_str());
    return true; // 继续处理链
}

bool CorsMiddleware::needCors(const std::string& path) {
    // 可以根据需求配置哪些路径需要CORS
    // 这里简单处理：所有/api/开头的路径都需要CORS
    return path.substr(0, 5) == "/api/";
}

// 认证中间件实现
AuthMiddleware::AuthMiddleware(const std::string& authHeader)
    : authHeader_(authHeader) {
    // 初始化公钥（在实际应用中应该从配置文件加载）
    jwtSecret_ = "your-secret-key";  // 实际应用中应该使用更安全的密钥
}

bool AuthMiddleware::handle(http::HttpRequest& request, http::HttpResponse& response) {
    auto logger = Logger::GetInstance();

    // 1. 检查是否是公开路径
    if (isPublicPath(request.getPath())) {
        logger->logSystem(LogLevel::DEBUG, "Skipping auth for public path: %s", request.getPath().c_str());
        return true;
    }

    // 2. 获取认证头
    auto token = request.getHeader(authHeader_);
    if (token.empty()) {
        logger->logSystem(LogLevel::WARN, "No auth token provided for path: %s", request.getPath().c_str());
        sendUnauthorized(response, "Authentication required");
        return false;
    }

    // 3. 验证token格式
    if (token.substr(0, 7) != "Bearer ") {
        logger->logSystem(LogLevel::WARN, "Invalid token format");
        sendUnauthorized(response, "Invalid token format");
        return false;
    }

    std::string actualToken = token.substr(7); // 移除"Bearer "前缀

    try {
        // 4. 验证token（简化版本）
        auto userData = verifyToken(actualToken);
        
        // 5. 将用户信息添加到请求中
        request.setAttribute("user", userData);
        
        logger->logSystem(LogLevel::DEBUG, "Successfully authenticated user: %s", 
            userData.value("username", "unknown").c_str());
        
        return true;

    } catch (const std::exception& e) {
        logger->logSystem(LogLevel::ERROR, "Token verification failed: %s", e.what());
        sendUnauthorized(response, "Invalid or expired token");
        return false;
    }
}

bool AuthMiddleware::isPublicPath(const std::string& path) {
    static const std::vector<std::string> publicPaths = {
        "/api/login",
        "/api/register",
        "/api/public",
        "/api/health"
    };
    
    return std::find(publicPaths.begin(), publicPaths.end(), path) != publicPaths.end();
}

void AuthMiddleware::sendUnauthorized(http::HttpResponse& response, const std::string& message) {
    response.setStatus(401);
    response.setHeader("Content-Type", "application/json");
    response.setBody(R"({"success":false,"message":")" + message + R"("})");
}

// 简化版的token验证
json AuthMiddleware::verifyToken(const std::string& token) {
    // 这里实现一个简单的token验证逻辑
    // 在实际应用中，你应该使用更安全的方式，比如JWT
    
    // 模拟token验证
    // token格式: userId_timestamp_signature
    auto parts = splitToken(token);
    if (parts.size() != 3) {
        throw std::runtime_error("Invalid token format");
    }

    // 检查时间戳是否过期
    time_t timestamp = std::stoll(parts[1]);
    if (time(NULL) - timestamp > 3600) { // 1小时过期
        throw std::runtime_error("Token has expired");
    }

    // 构造用户信息
    json userData = {
        {"userId", parts[0]},
        {"timestamp", timestamp}
    };

    return userData;
}

std::vector<std::string> AuthMiddleware::splitToken(const std::string& token) {
    std::vector<std::string> parts;
    std::string::size_type start = 0;
    std::string::size_type end = 0;

    while ((end = token.find('_', start)) != std::string::npos) {
        parts.push_back(token.substr(start, end - start));
        start = end + 1;
    }
    parts.push_back(token.substr(start));

    return parts;
}
