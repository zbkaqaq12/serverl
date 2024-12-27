#include "http/parser.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace http {

/**
 * @brief 构造函数.
 */
HttpParser::HttpParser() {
    reset();
}

/**
 * @brief 初始化
 */
void HttpParser::reset() {
    state_ = ParseState::START;
    method_ = HttpMethod::UNKNOWN;
    uri_.clear();
    version_ = HttpVersion::UNKNOWN;
    headers_.clear();
    body_.clear();
    currentHeaderKey_.clear();
    contentLength_ = 0;
    hasContentLength_ = false;
}

/**
 * @brief 解析http请求.
 * 
 * @param data 数据
 * @param len 数据长度
 * @return 解析结果
 */ 
bool HttpParser::parse(const char* data, size_t len) {
    const char* p = data;
    const char* end = data + len;
    
    while (p < end) {
        switch (state_) {
            case ParseState::START:
            case ParseState::METHOD:
            case ParseState::URI:
            case ParseState::VERSION: {
                const char* lineEnd = std::find(p, end, '\r');
                if (lineEnd + 1 >= end || *(lineEnd + 1) != '\n') {
                    return false;  // 需要更多数据
                }
                if (!parseRequestLine(p, lineEnd)) {
                    state_ = ParseState::ERROR;
                    return false;
                }
                p = lineEnd + 2;
                state_ = ParseState::HEADER_KEY;
                break;
            }
            
            case ParseState::HEADER_KEY:
            case ParseState::HEADER_VALUE: {
                const char* lineEnd = std::find(p, end, '\r');
                if (lineEnd + 1 >= end || *(lineEnd + 1) != '\n') {
                    return false;  // 需要更多数据
                }
                if (p == lineEnd) {  // 空行，表示头部结束
                    p = lineEnd + 2;
                    if (hasContentLength_ && contentLength_ > 0) {
                        state_ = ParseState::BODY;
                    } else {
                        state_ = ParseState::COMPLETE;
                        return true;
                    }
                } else {
                    if (!parseHeaders(p, lineEnd)) {
                        state_ = ParseState::ERROR;
                        return false;
                    }
                    p = lineEnd + 2;
                }
                break;
            }
            
            case ParseState::BODY: {
                size_t remainingLength = end - p;
                if (remainingLength >= contentLength_) {
                    body_.assign(p, p + contentLength_);
                    state_ = ParseState::COMPLETE;
                    return true;
                } else {
                    return false;  // 需要更多数据
                }
            }
            
            default:
                return false;
        }
    }
    
    return state_ == ParseState::COMPLETE;
}

/**
 * @brief 解析请求行.
 * 
 * @param begin 开始位置
 * @param end 结束位置
 * @return 解析结果
 */
bool HttpParser::parseRequestLine(const char* begin, const char* end) {
    // 解析方法
    const char* p = begin;
    const char* space = std::find(p, end, ' ');
    if (space == end) return false;
    
    method_ = parseMethod(std::string(p, space));
    if (method_ == HttpMethod::UNKNOWN) return false;
    
    // 解析URI
    p = space + 1;
    space = std::find(p, end, ' ');
    if (space == end) return false;
    
    uri_.assign(p, space);
    if (uri_.empty()) return false;
    
    // 解析HTTP版本
    p = space + 1;
    version_ = parseVersion(std::string(p, end));
    return version_ != HttpVersion::UNKNOWN;
}

bool HttpParser::parseHeaders(const char* begin, const char* end) {
    const char* p = begin;
    const char* colon = std::find(p, end, ':');
    if (colon == end) return false;
    
    std::string key(p, colon);
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    
    p = colon + 1;
    while (p < end && isSpace(*p)) ++p;
    
    std::string value(p, end);
    headers_[key] = value;
    
    if (key == "content-length") {
        contentLength_ = std::stoull(value);
        hasContentLength_ = true;
    }
    
    return true;
}

/**
 * @brief 解析方法.
 * 
 * @param method 方法
 * @return 解析结果
 */
HttpMethod HttpParser::parseMethod(const std::string& method) {
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "HEAD") return HttpMethod::HEAD;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    return HttpMethod::UNKNOWN;
}

/**
 * @brief 解析版本.
 * 
 * @param version 版本
 * @return 解析结果
 */
HttpVersion HttpParser::parseVersion(const std::string& version) {
    if (version == "HTTP/1.0") return HttpVersion::HTTP_10;
    if (version == "HTTP/1.1") return HttpVersion::HTTP_11;
    return HttpVersion::UNKNOWN;
}

/**
 * @brief 获取查询参数.
 * 
 * @param name 参数名
 * @return 参数值
 */
std::string HttpParser::getQueryParam(const std::string& name) const {
    auto it = queryParams_.find(name);
    return it != queryParams_.end() ? it->second : "";
}

/**
 * @brief 解析URI.
 * 
 * @param uri URI
 * @return 解析结果
 */
bool HttpParser::parseUri(const std::string& uri) {
    auto questionPos = uri.find('?');
    if (questionPos == std::string::npos) {
        path_ = uri;
        return true;
    }

    path_ = uri.substr(0, questionPos);
    return parseQueryString(uri.substr(questionPos + 1));
}

/**
 * @brief 解析查询字符串.
 * 
 * @param query 查询字符串
 * @return 解析结果
 */
bool HttpParser::parseQueryString(const std::string& query) {
    std::string key, value;
    bool inKey = true;
    
    for (size_t i = 0; i < query.length(); ++i) {
        char c = query[i];
        if (c == '=') {
            inKey = false;
            continue;
        }
        if (c == '&') {
            if (!key.empty()) {
                queryParams_[key] = value;
            }
            key.clear();
            value.clear();
            inKey = true;
            continue;
        }
        
        // URL解码
        if (c == '%' && i + 2 < query.length()) {
            int hex1 = std::isxdigit(query[i + 1]) ? std::tolower(query[i + 1]) : -1;
            int hex2 = std::isxdigit(query[i + 2]) ? std::tolower(query[i + 2]) : -1;
            if (hex1 != -1 && hex2 != -1) {
                char decodedChar = (hex1 >= 'a' ? hex1 - 'a' + 10 : hex1 - '0') * 16 +
                                 (hex2 >= 'a' ? hex2 - 'a' + 10 : hex2 - '0');
                (inKey ? key : value) += decodedChar;
                i += 2;
                continue;
            }
        }
        
        (inKey ? key : value) += c;
    }
    
    if (!key.empty()) {
        queryParams_[key] = value;
    }
    
    return true;
}

/**
 * @brief 验证头部.
 * 
 * @return 验证结果
 */
bool HttpParser::validateHeaders() {
    // 检查必要的头部
    if (headers_.find("host") == headers_.end() && version_ == HttpVersion::HTTP_11) {
        setError("Missing Host header in HTTP/1.1 request");
        return false;
    }

    // 检查Transfer-Encoding
    auto it = headers_.find("transfer-encoding");
    if (it != headers_.end()) {
        if (it->second == "chunked") {
            chunked_ = true;
            if (hasContentLength_) {
                setError("Both Transfer-Encoding and Content-Length present");
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief 解析分块传输的body.
 * 
 * @param begin 开始位置
 * @param end 结束位置
 * @return 解析结果
 */
bool HttpParser::parseChunkedBody(const char* begin, const char* end) {
    while (begin < end) {
        // 如果还没有chunk size，先读取size
        if (contentLength_ == 0) {
            const char* lineEnd = std::find(begin, end, '\r');
            if (lineEnd + 1 >= end || *(lineEnd + 1) != '\n') {
                return false;  // 需要更多数据
            }
            
            // 解析chunk size
            std::string sizeStr(begin, lineEnd);
            char* sizeEnd;
            contentLength_ = std::strtoul(sizeStr.c_str(), &sizeEnd, 16);
            
            if (contentLength_ == 0) {
                state_ = ParseState::COMPLETE;
                return true;
            }
            
            begin = lineEnd + 2;
        }
        
        // 读取chunk数据
        size_t remainingLength = end - begin;
        if (remainingLength < contentLength_ + 2) {  // +2 for CRLF
            return false;  // 需要更多数据
        }
        
        body_.append(begin, contentLength_);
        begin += contentLength_ + 2;  // 跳过CRLF
        contentLength_ = 0;  // 重置，准备读取下一个chunk
    }
    
    return true;
}

/**
 * @brief 设置错误.
 * 
 * @param error 错误信息
 */
void HttpParser::setError(const std::string& error) {
    lastError_ = error;
    state_ = ParseState::ERROR;
}

} // namespace http
