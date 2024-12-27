#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <regex>
#include "base_controller.h"
#include "middleware.h"
#include "CSocket.h"

// 前向声明
namespace http {
    class HttpRequest;
    class HttpResponse;
}

class BaseController;
class Middleware;

// 类型定义
using RouteParams = std::unordered_map<std::string, std::string>;
using ControllerHandler = std::function<void(http::HttpRequest&, http::HttpResponse&, const RouteParams&)>;

class Router {
public:
    // 获取单例实例
    static Router* GetInstance();
    
    // 路由组类定义
    class RouteGroup {
    public:
        RouteGroup(Router* router, const std::string& prefix);
        
        // 添加路由到组
        void addRoute(const std::string& method, 
                     const std::string& path,
                     const std::shared_ptr<BaseController>& controller,
                     const ControllerHandler& handler);
        
        // 添加中间件到组
        void use(std::shared_ptr<Middleware> middleware);
        
    private:
        Router* router_;
        std::string prefix_;
        std::vector<std::shared_ptr<Middleware>> middlewares_;
        
        friend class Router;
    };

    // 创建路由组
    RouteGroup group(const std::string& prefix);

    // 添加路由
    void addRoute(const std::string& method, 
             const std::string& path,
             const std::shared_ptr<BaseController>& controller,
             const ControllerHandler& handler);

    // 添加全局中间件
    void use(std::shared_ptr<Middleware> middleware);

    // 处理HTTP请求
    bool handleRequest(lpconnection_t pConn);

    // 注册控制器
    template <typename ControllerT>
    void registerController() {
        auto controller = std::make_shared<ControllerT>();
        controller->registerRoutes(this);
        controllers_.push_back(controller);
    }

    // 清理路由
    void cleanup();
    
    // 获取路由数量
    size_t getRouteCount() const;

    // 初始化所有路由
    void initRoutes() {
        cleanup();  // 清理现有路由
        // 路由初始化会在各个Controller的registerRoutes中完成
    }

private:
    Router() = default;
    ~Router() = default;
    Router(const Router&) = delete;
    Router& operator=(const Router&) = delete;

    // 路由结构
    struct Route {
        std::string method;          // HTTP方法
        std::string path;            // 原始路径
        std::regex pattern;          // 路径匹配模式
        std::vector<std::string> paramNames;  // 参数名列表
        std::shared_ptr<BaseController> controller;  // 控制器
        ControllerHandler handler;    // 处理函数
        std::vector<std::shared_ptr<Middleware>> middlewares;  // 路由级中间件
    };

    // 辅助方法
    bool matchRoute(const Route& route, const std::string& path, RouteParams& params);
    bool executeMiddleware(const std::vector<std::shared_ptr<Middleware>>& middlewares,
                         http::HttpRequest& request,
                         http::HttpResponse& response);

    // 成员变量
    std::vector<Route> routes_;
    std::vector<std::shared_ptr<BaseController>> controllers_;
    std::vector<std::shared_ptr<Middleware>> globalMiddlewares_;
    static Router* instance;
    static std::mutex instanceMutex;
    mutable std::mutex routesMutex;  // 保护路由表的互斥锁
};