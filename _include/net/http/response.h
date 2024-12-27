// _include/net/http/response.h
#pragma once

#include <string>
#include <unordered_map>

namespace http {

class HttpResponse {
public:
    // 设置响应信息
    void setStatus(int code, const std::string& message = "");
    void setHeader(const std::string& key, const std::string& value);
    void setBody(const std::string& body);
    void setContentType(const std::string& contentType);
    
    // 序列化响应，返回构造好的HTTP响应字符串
    std::string serialize() const;
    
    // 清理响应
    void clear();
    
    // 常用响应方法
    void json(const std::string& jsonStr);
    void text(const std::string& text);
    void html(const std::string& html);
    
    // 获取响应信息
    int getStatusCode() const { return statusCode_; }
    const std::string& getStatusMessage() const { return statusMessage_; }
    const std::string& getBody() const { return body_; }

private:
    int statusCode_{200};
    std::string statusMessage_{"OK"};
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

} // namespace http