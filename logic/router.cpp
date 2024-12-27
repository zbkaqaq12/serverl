#include "router.h"
#include "Logger.h"
#include "request.h"
#include "response.h"
#include <algorithm>
#include <regex>

// 单例实现
Router* Router::instance = nullptr;
std::mutex Router::instanceMutex;

Router* Router::GetInstance() {
    if (instance == nullptr) {
        std::lock_guard<std::mutex> lock(instanceMutex);
        if (instance == nullptr) {
            instance = new Router();
        }
    }
    return instance;
}

Router::RouteGroup Router::group(const std::string& prefix) {
    return RouteGroup(this, prefix);
}

void Router::addRoute(const std::string& method, 
                     const std::string& path,
                     const std::shared_ptr<BaseController>& controller,
                     const ControllerHandler& handler) {
    std::lock_guard<std::mutex> lock(routesMutex);
    
    // 将HTTP方法转换为大写
    std::string upperMethod = method;
    std::transform(upperMethod.begin(), upperMethod.end(), upperMethod.begin(), ::toupper);
    
    // 解析路由参数
    std::vector<std::string> paramNames;
    std::string pattern = path;
    
    // 将路由模式转换��正则表达式
    // 例如: /users/:id -> /users/([^/]+)
    std::regex paramRegex(":([^/]+)");
    std::smatch matches;
    std::string::const_iterator searchStart(path.cbegin());
    
    while (std::regex_search(searchStart, path.cend(), matches, paramRegex)) {
        paramNames.push_back(matches[1].str());
        pattern = std::regex_replace(pattern, 
                                   std::regex(":" + matches[1].str()),
                                   "([^/]+)");
        searchStart = matches.suffix().first;
    }
    
    // 创建新路由
    Route route;
    route.method = upperMethod;
    route.path = path;
    route.pattern = std::regex("^" + pattern + "$");
    route.paramNames = paramNames;
    route.controller = controller;
    route.handler = handler;
    
    routes_.push_back(route);
    
    // 日志记录
    auto logger = Logger::GetInstance();
    logger->logSystem(LogLevel::INFO, "Route registered: %s %s", method.c_str(), path.c_str());
}

bool Router::handleRequest(lpconnection_t pConn) {
    auto logger = Logger::GetInstance();
    
    try {
        // 获取请求信息
        auto& request = pConn->httpCtx->request;
        auto& response = pConn->httpCtx->response;
        
        // 在路由表中查找匹配的路由
        std::lock_guard<std::mutex> lock(routesMutex);
        
        // 获取请求方法并转换为大写
        std::string requestMethod = request.getMethod();
        std::transform(requestMethod.begin(), requestMethod.end(), 
                      requestMethod.begin(), ::toupper);
        
        std::string requestPath = request.getPath();
        
        // 记录请求日志
        logger->logSystem(LogLevel::INFO, "Handling request: %s %s",
                         requestMethod.c_str(), requestPath.c_str());
        
        for (const auto& route : routes_) {
            // 检查HTTP方法是否匹配
            if (route.method != requestMethod) continue;
            
            // 检查路径是否匹配
            RouteParams params;
            if (matchRoute(route, requestPath, params)) {
                // 执行全局中间件
                if (!executeMiddleware(globalMiddlewares_, request, response)) {
                    logger->logSystem(LogLevel::INFO, "Request stopped by global middleware");
                    return true;
                }
                
                // 执行路由中间件
                if (!executeMiddleware(route.middlewares, request, response)) {
                    logger->logSystem(LogLevel::INFO, "Request stopped by route middleware");
                    return true;
                }
                
                // 调用处理器
                try {
                    route.handler(request, response, params);
                    logger->logSystem(LogLevel::INFO, "Request handled successfully");
                } catch (const std::exception& e) {
                    logger->logSystem(LogLevel::ERROR, "Handler error: %s", e.what());
                    throw; // 重新抛出异常，让外层错误处理来处理
                }
                return true;
            }
        }
        
        // 没有找到匹配的路由，设置404响应
        logger->logSystem(LogLevel::ERROR, "No route found for: %s %s",
                         requestMethod.c_str(), requestPath.c_str());
        
        response.setStatus(404, "Not Found");
        response.setHeader("Content-Type", "application/json");
        json error = {
            {"success", false},
            {"code", 404},
            {"message", "Route not found"},
            {"data", nullptr}
        };
        response.setBody(error.dump());
        return true;
        
    } catch (const std::exception& e) {
        // 记录错误并设置500响应
        logger->logSystem(LogLevel::ERROR, "Error handling request: %s", e.what());
        
        json error = {
            {"success", false},
            {"code", 500},
            {"message", "Internal Server Error"},
            {"data", nullptr}
        };
        
        pConn->httpCtx->response.setStatus(500, "Internal Server Error");
        pConn->httpCtx->response.setHeader("Content-Type", "application/json");
        pConn->httpCtx->response.setBody(error.dump());
        return false;
    }
}

bool Router::matchRoute(const Route& route, const std::string& path, RouteParams& params) {
    std::smatch matches;
    if (std::regex_match(path, matches, route.pattern)) {
        // 第一个匹配项是整个字符串，跳过
        for (size_t i = 1; i < matches.size() && i-1 < route.paramNames.size(); ++i) {
            params[route.paramNames[i-1]] = matches[i].str();
        }
        return true;
    }
    return false;
}

bool Router::executeMiddleware(const std::vector<std::shared_ptr<Middleware>>& middlewares,
                             http::HttpRequest& request,
                             http::HttpResponse& response) {
    for (const auto& middleware : middlewares) {
        if (!middleware->handle(request, response)) {
            return false;
        }
    }
    return true;
}

void Router::use(std::shared_ptr<Middleware> middleware) {
    globalMiddlewares_.push_back(middleware);
    
    auto logger = Logger::GetInstance();
    logger->logSystem(LogLevel::INFO, "Global middleware added");
}

void Router::cleanup() {
    std::lock_guard<std::mutex> lock(routesMutex);
    routes_.clear();
    controllers_.clear();
    globalMiddlewares_.clear();
    
    auto logger = Logger::GetInstance();
    logger->logSystem(LogLevel::INFO, "Router cleaned up");
}

size_t Router::getRouteCount() const {
    std::lock_guard<std::mutex> lock(routesMutex);
    return routes_.size();
}

// RouteGroup implementation
Router::RouteGroup::RouteGroup(Router* router, const std::string& prefix)
    : router_(router), prefix_(prefix) {
    auto logger = Logger::GetInstance();
    logger->logSystem(LogLevel::INFO, "Route group created with prefix: %s", prefix.c_str());
}

void Router::RouteGroup::addRoute(const std::string& method,
                                const std::string& path,
                                const std::shared_ptr<BaseController>& controller,
                                const ControllerHandler& handler) {
    // 组合完整路径
    std::string fullPath = prefix_;
    if (!path.empty() && path[0] != '/') {
        fullPath += '/';
    }
    fullPath += path;
    
    // 创建新的处理器，包装中间件
    auto wrappedHandler = [this, handler](http::HttpRequest& req,
                                        http::HttpResponse& res,
                                        const RouteParams& params) {
        // 执行组中间件
        if (!router_->executeMiddleware(middlewares_, req, res)) {
            return;
        }
        // 执行原始处理器
        handler(req, res, params);
    };
    
    router_->addRoute(method, fullPath, controller, wrappedHandler);
}

void Router::RouteGroup::use(std::shared_ptr<Middleware> middleware) {
    middlewares_.push_back(middleware);
    
    auto logger = Logger::GetInstance();
    logger->logSystem(LogLevel::INFO, "Middleware added to route group: %s", prefix_.c_str());
}