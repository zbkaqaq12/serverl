// net/http/response.cpp
#include "http/response.h"


namespace http {

void HttpResponse::setStatus(int code, const std::string& message) {
    statusCode_ = code;
    if (!message.empty()) {
        statusMessage_ = message;
    } else {
        // 设置默认状态消息
        switch (code) {
            case 200: statusMessage_ = "OK"; break;
            case 201: statusMessage_ = "Created"; break;
            case 204: statusMessage_ = "No Content"; break;
            case 400: statusMessage_ = "Bad Request"; break;
            case 401: statusMessage_ = "Unauthorized"; break;
            case 403: statusMessage_ = "Forbidden"; break;
            case 404: statusMessage_ = "Not Found"; break;
            case 500: statusMessage_ = "Internal Server Error"; break;
            case 502: statusMessage_ = "Bad Gateway"; break;
            case 503: statusMessage_ = "Service Unavailable"; break;
            default: statusMessage_ = "Unknown"; break;
        }
    }
}

void HttpResponse::setHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpResponse::setBody(const std::string& body) {
    body_ = body;
    // 自动设置 Content-Length
    setHeader("Content-Length", std::to_string(body.length()));
}

void HttpResponse::setContentType(const std::string& contentType) {
    setHeader("Content-Type", contentType);
}

// 修改为返回构造好的字符串
std::string HttpResponse::serialize() const {
    // 构造状态行
    std::string response = "HTTP/1.1 " + 
                          std::to_string(statusCode_) + " " + 
                              statusMessage_ + "\r\n";
        
        // 添加响应头
        for (const auto& header : headers_) {
            response += header.first + ": " + header.second + "\r\n";
        }
        
        // 添加空行和响应体
        response += "\r\n" + body_;
        
        return response;
    }

void HttpResponse::clear() {
    statusCode_ = 200;
    statusMessage_ = "OK";
    headers_.clear();
    body_.clear();
}

void HttpResponse::json(const std::string& jsonStr) {
    setContentType("application/json");
    setBody(jsonStr);
}

void HttpResponse::text(const std::string& text) {
    setContentType("text/plain");
    setBody(text);
}

void HttpResponse::html(const std::string& html) {
    setContentType("text/html");
    setBody(html);
}

} // namespace http