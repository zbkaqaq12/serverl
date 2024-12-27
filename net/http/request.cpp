// net/http/request.cpp
#include "http/request.h"
#include <sstream>
#include <algorithm>

namespace http {

std::string HttpRequest::getHeader(const std::string& key) const {
    auto it = headers_.find(key);
    if (it != headers_.end()) {
        return it->second;
    }
    return "";
}

std::string HttpRequest::getQueryParam(const std::string& key) const {
    auto it = queryParams_.find(key);
    if (it != queryParams_.end()) {
        return it->second;
    }
    return "";
}

void HttpRequest::clear() {
    method_.clear();
    path_.clear();
    query_.clear();
    version_.clear();
    body_.clear();
    headers_.clear();
    queryParams_.clear();
}

void HttpRequest::addHeader(const std::string& key, const std::string& value) {
    // 将 header 名称转换为小写，以实现大小写不敏感
    std::string lowerKey = key;
    std::transform(lowerKey.begin(), lowerKey.end(), lowerKey.begin(), ::tolower);
    headers_[lowerKey] = value;
}

} // namespace http