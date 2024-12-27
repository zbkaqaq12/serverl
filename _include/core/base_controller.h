#pragma once

#include "request.h"
#include "response.h"
#include "json.hpp"
#include <unordered_map>
#include <memory>

class Router;  // 前向声明

using json = nlohmann::json;
using RouteParams = std::unordered_map<std::string, std::string>;

class BaseController : public std::enable_shared_from_this<BaseController> {
public:
    virtual ~BaseController() = default;
    virtual void registerRoutes(Router* router) = 0;

protected:
    // 统一的响应格式
    struct ApiResponse {
        bool success;
        int code;
        std::string message;
        json data;

        json toJson() const {
            return {
                {"success", success},
                {"code", code},
                {"message", message},
                {"data", data}
            };
        }
    };

    // 辅助方法
    void sendJson(http::HttpResponse& response, const json& data, int status = 200) {
        response.setStatus(status);
        response.setHeader("Content-Type", "application/json");
        response.setBody(data.dump());
    }

    void sendSuccess(http::HttpResponse& response, const json& data = json::object()) {
        ApiResponse apiResponse{true, 200, "Success", data};
        sendJson(response, apiResponse.toJson());
    }

    void sendError(http::HttpResponse& response, int status, const std::string& message) {
        ApiResponse apiResponse{false, status, message, json::object()};
        sendJson(response, apiResponse.toJson(), status);
    }

    // 请求参数处理
    template<typename T>
    T getQueryParam(const http::HttpRequest& request, const std::string& name, const T& defaultValue) {
        auto value = request.getQueryParam(name);
        if (value.empty()) {
            return defaultValue;
        }
        try {
            if constexpr (std::is_same_v<T, int>) {
                return std::stoi(value);
            } else if constexpr (std::is_same_v<T, double>) {
                return std::stod(value);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return value;
            }
        } catch (...) {
            return defaultValue;
        }
        return defaultValue;
    }

    json getRequestBody(const http::HttpRequest& request) {
        try {
            return json::parse(request.getBody());
        } catch (const json::exception& e) {
            throw std::runtime_error("Invalid JSON body");
        }
    }
};