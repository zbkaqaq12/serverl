#pragma once

#include <string>
#include <map>
#include <memory>
#include <vector>
#include <functional>

namespace http {

// HTTP 方法枚举
enum class HttpMethod {
    GET,
    POST,
    PUT,
    DELETE,
    HEAD,
    OPTIONS,
    UNKNOWN
};

// HTTP 版本枚举
enum class HttpVersion {
    HTTP_10,
    HTTP_11,
    UNKNOWN
};

// 解析状态
enum class ParseState {
    START,
    METHOD,
    URI,
    VERSION,
    HEADER_KEY,
    HEADER_VALUE,
    BODY,
    COMPLETE,
    ERROR
};

class HttpParser {
public:
    HttpParser();
    ~HttpParser() = default;

    // 解析缓冲区数据
    bool parse(const char* data, size_t len);

    // 获取解析结果
    HttpMethod getMethod() const { return method_; }
    const std::string& getUri() const { return uri_; }
    HttpVersion getVersion() const { return version_; }
    const std::map<std::string, std::string>& getHeaders() const { return headers_; }
    const std::string& getBody() const { return body_; }
    bool isComplete() const { return state_ == ParseState::COMPLETE; }
    
    // 重置解析器状态
    void reset();

    // 新增方法
    const std::map<std::string, std::string>& getQueryParams() const { return queryParams_; }
    std::string getQueryParam(const std::string& name) const;
    std::string getPath() const { return path_; }  // 不含查询参数的URI
    
    // 错误处理
    std::string getLastError() const { return lastError_; }
    bool hasError() const { return !lastError_.empty(); }

private:
    // 解析辅助函数
    bool parseRequestLine(const char* begin, const char* end);
    bool parseHeaders(const char* begin, const char* end);
    bool parseBody(const char* begin, const char* end);
    
    // 工具函数
    static HttpMethod parseMethod(const std::string& method);
    static HttpVersion parseVersion(const std::string& version);
    static bool isSpace(char c) { return c == ' ' || c == '\t'; }
    static bool isEndOfLine(const char* p) { return *p == '\r' && *(p + 1) == '\n'; }

    // 解析状态
    ParseState state_{ParseState::START};
    
    // 解析结果
    HttpMethod method_{HttpMethod::UNKNOWN};
    std::string uri_;
    HttpVersion version_{HttpVersion::UNKNOWN};
    std::map<std::string, std::string> headers_;
    std::string body_;
    
    // 临时缓存
    std::string currentHeaderKey_;
    size_t contentLength_{0};
    bool hasContentLength_{false};

    // 新增成员
    std::string path_;                                    // URI中的路径部分
    std::map<std::string, std::string> queryParams_;     // 查询参数
    std::string lastError_;                              // 最后的错误信息
    bool chunked_{false};                                // 是否为分块传输
    std::string chunkBuffer_;                            // 分块传输缓冲区

    // 新增辅助方法
    bool parseUri(const std::string& uri);
    bool parseQueryString(const std::string& query);
    bool validateHeaders();
    bool parseChunkedBody(const char* begin, const char* end);
    void setError(const std::string& error);
};

} // namespace http