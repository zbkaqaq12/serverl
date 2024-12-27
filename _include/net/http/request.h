// _include/net/http/request.h
#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include "parser.h"
#include "json.hpp"

using json = nlohmann::json;

namespace http {

class HttpRequest {
public:
         // 将HTTP方法枚举转换为字符串
    static std::string methodToString(HttpMethod method) {
        switch (method) {
            case HttpMethod::GET:     return "GET";
            case HttpMethod::POST:    return "POST";
            case HttpMethod::PUT:     return "PUT";
            case HttpMethod::DELETE:  return "DELETE";
            case HttpMethod::HEAD:    return "HEAD";
            case HttpMethod::OPTIONS: return "OPTIONS";
            default:                  return "UNKNOWN";
        }
    }

    // 将HTTP版本枚举转换为字符串
    static std::string versionToString(HttpVersion version) {
        switch (version) {
            case HttpVersion::HTTP_10: return "HTTP/1.0";
            case HttpVersion::HTTP_11: return "HTTP/1.1";
            default:                   return "UNKNOWN";
        }
    }

    // 添加从 Parser 构造的方法
    void fromParser(const HttpParser& parser) {
        method_ = methodToString(parser.getMethod());
        path_ = parser.getPath();
        query_ = parser.getUri();  // 获取完整URI
        version_ = versionToString(parser.getVersion());
        headers_ = std::unordered_map<std::string, std::string>(
            parser.getHeaders().begin(), 
            parser.getHeaders().end()
        );
        body_ = parser.getBody();
        queryParams_ = std::unordered_map<std::string, std::string>(
            parser.getQueryParams().begin(), 
            parser.getQueryParams().end()
        );
    }


    // 获取请求信息
    const std::string& getMethod() const { return method_; }
    const std::string& getPath() const { return path_; }
    const std::string& getQuery() const { return query_; }
    const std::string& getVersion() const { return version_; }
    const std::string& getBody() const { return body_; }
    
    // 获取请求头
    std::string getHeader(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& getHeaders() const { return headers_; }
    
    // 获取查询参数
    std::string getQueryParam(const std::string& key) const;
    
    // 清理请求
    void clear();
    
    // 设置请求信息（由Parser调用）
    void setMethod(const std::string& method) { method_ = method; }
    void setPath(const std::string& path) { path_ = path; }
    void setQuery(const std::string& query) { query_ = query; }
    void setVersion(const std::string& version) { version_ = version; }
    void setBody(const std::string& body) { body_ = body; }
    void addHeader(const std::string& key, const std::string& value);

    // 添加属性存储方法
    void setAttribute(const std::string& key, const json& value) {
        attributes_[key] = value;
    }
    
    json getAttribute(const std::string& key) const {
        auto it = attributes_.find(key);
        return it != attributes_.end() ? it->second : json();
    }

private:
    std::string method_;
    std::string path_;
    std::string query_;
    std::string version_;
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> queryParams_;
    std::unordered_map<std::string, json> attributes_;  // 用于存储请求属性
};

} // namespace http