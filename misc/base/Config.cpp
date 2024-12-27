#include "Config.h"

// 静态成员赋值
Config* Config::m_instance = nullptr;

/**
 * @brief 构造函数，初始化配置器实例
 */
Config::Config()
{
}

/**
 * @brief 析构函数，释放配置器相关内存并清空配置项列表
 */
Config::~Config()
{
    m_ConfigMap.clear();
}

/**
 * @brief 加载配置文件并解析所有配置项
 *
 * 该函数会解析指定的 XML 配置文件，读取文件中的各个配置项，并将其存储在 `m_ConfigItemList` 中
 *
 * @param pconfName 配置文件的路径名
 * @return bool 如果加载成功返回 true，否则返回 false
 */
bool Config::Load(const char* pconfName)
{
    if (pconfName == nullptr) return false;
    m_configFile = pconfName;
    tinyxml2::XMLDocument xmlDoc;

    // 加载 XML 配置文件
   
    if ( xmlDoc.LoadFile(pconfName) != tinyxml2::XML_SUCCESS) {
        std::cerr << "Failed to load config file: " << pconfName << std::endl;
        return false;
    }

    // 获取 XML 根节点
    tinyxml2::XMLElement* rootElement = xmlDoc.RootElement();
    if (!rootElement) {
        std::cerr << "no root element found in the xml file: " << pconfName << std::endl;
        return false;
    }

    m_ConfigMap.clear(); // 清理之前的配置项

    // 遍历一级元素（如 Log, Proc, Net 等）
    // 遍历配置节点
    for (tinyxml2::XMLElement* section = rootElement->FirstChildElement(); 
         section; 
         section = section->NextSiblingElement()) 
    {       
        // 获取一级元素的名称（类别）
        const char* sectionName = section->Value();
        if (!sectionName) continue;

        // 遍历该分类下的二级元素（实际的配置项）
         for (tinyxml2::XMLElement* item = section->FirstChildElement(); 
             item; 
             item = item->NextSiblingElement()) 
        {   
            // 获取每个配置项的名称和值
            const char* itemName = item->Value();
            const char* itemValue = item->GetText();
            if (itemName  && itemValue ) {
                CConfItem confItem;
                confItem.Section = sectionName;
                confItem.ItemName = itemName;
                confItem.ItemContent = itemValue;
                // 使用完整路径作为键 (section.item)
                std::string fullKey = std::string(sectionName) + "." + itemName;
                m_ConfigMap[fullKey] = confItem;
            }

        }

    }
    return true; // 配置文件加载解析完成成功
}

/**
 * @brief 重新加载配置文件
 * 
 * 该函数会重新加载配置文件，并更新 `m_ConfigMap` 中的配置项
 * 
 * @return bool 如果加载成功返回 true，否则返回 false
 */
bool Config::Reload()
{
    return !m_ConfigMap.empty() && Load(m_configFile.c_str());
}

/**
 * @brief 获取指定配置项的字符串内容
 *
 * 根据传入的配置项名称，返回对应的配置内容（字符串形式）。如果未找到对应的配置项，返回 nullptr
 *
 * @param p_itemname 配置项的名称
 * @return const char* 如果找到对应的配置项，返回其内容，否则返回 nullptr
 */
const char* Config::GetString(const char* p_itemname)
{
    auto it = m_ConfigMap.find(p_itemname);
    if (it != m_ConfigMap.end()) {
        return it->second.ItemContent.c_str();
    }
    return nullptr;
}

/**
 * @brief 获取指定配置项的整数值
 *
 * 根据传入的配置项名称，获取对应的整数值。如果配置项不存在，则返回提供的默认值
 *
 * @param p_itemname 配置项的名称
 * @param def 默认值，配置项不存在时返回该值
 * @return int 返回配置项的整数值或默认值
 */
int Config::GetIntDefault(const char* p_itemname, const int def)
{
    auto it = m_ConfigMap.find(p_itemname);
    if (it != m_ConfigMap.end()) {
        try {
            return std::stoi(it->second.ItemContent);
        } catch (...) {
            return def;
        }
    }
    return def;
}

/**
 * @brief 获取指定配置项的布尔值
 *
 * 根据传入的配置项名称，获取对应的布尔值。如果配置项不存在，则返回提供的默认值
 *
 * @param p_itemname 配置项的名称
 * @param def 默认值，配置项不存在时返回该值
 * @return bool 返回配置项的布尔值或默认值
 */
bool Config::GetBool(const char* p_itemname, bool def) {
    auto it = m_ConfigMap.find(p_itemname);
    if (it != m_ConfigMap.end()) {
        std::string value = it->second.ItemContent;
        return (value == "1" || value == "true" || value == "yes");
    }
    return def;
}

/**
 * @brief 获取指定区段的所有配置项
 *
 * 根据传入的区段名称，获取该区段下的所有配置项
 *
 * @param section 区段的名称
 * @return std::vector<CConfItem> 返回该区段下的所有配置项
 */
std::vector<CConfItem> Config::GetSection(const std::string& section) {
    std::vector<CConfItem> result;
    for (const auto& pair : m_ConfigMap) {
        if (pair.second.Section == section) {
            result.push_back(pair.second);
        }
    }
    return result;
} 

/**
 * @brief 检查指定配置项是否存在
 *
 * 根据传入的配置项名称，检查该配置项是否存在于 `m_ConfigMap` 中
 *
 * @param p_itemname 配置项的名称
 * @return bool 如果配置项存在返回 true，否则返回 false
 */
bool Config::Exists(const char* p_itemname) const {
    return m_ConfigMap.find(p_itemname) != m_ConfigMap.end();
}