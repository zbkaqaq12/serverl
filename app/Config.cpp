#include "Config.h"

// 静态成员赋值
Config* Config::m_instance = NULL;

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
    std::vector<LPCConfItem>::iterator pos;
    for (pos = m_ConfigItemList.begin(); pos != m_ConfigItemList.end(); ++pos)
    {
        delete (*pos); // 删除配置项内容
    }
    m_ConfigItemList.clear(); // 清空配置项列表
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
    tinyxml2::XMLDocument xmlDoc;

    // 加载 XML 配置文件
    tinyxml2::XMLError eResult = xmlDoc.LoadFile(pconfName);
    if (eResult != tinyxml2::XML_SUCCESS) {
        std::cerr << "Failed to load config file: " << pconfName << std::endl;
        return false;
    }

    // 获取 XML 根节点
    tinyxml2::XMLElement* rootElement = xmlDoc.RootElement();
    if (rootElement == nullptr) {
        std::cerr << "no root element found in the xml file: " << pconfName << std::endl;
        return false;
    }

    m_ConfigItemList.clear(); // 清理之前的配置项

    // 遍历一级元素（如 Log, Proc, Net 等）
    for (tinyxml2::XMLElement* section = rootElement->FirstChildElement(); section; section = section->NextSiblingElement()) {
        // 获取一级元素的名称（类别）
        const char* sectionName = section->Value();
        if (!sectionName) continue;

        // 遍历该分类下的二级元素（实际的配置项）
        for (tinyxml2::XMLElement* item = section->FirstChildElement(); item; item = item->NextSiblingElement()) {
            // 获取每个配置项的名称和值
            const char* itemName = item->Value();
            const char* itemValue = item->GetText();
            if (itemName != nullptr && itemValue != nullptr) {
                LPCConfItem confItem = new CConfItem;
                confItem->ItemName = std::string(itemName);
                confItem->ItemContent = itemValue;
                m_ConfigItemList.push_back(confItem); // 将配置项添加到列表中
            }
        }
    }
    return true; // 配置文件加载解析完成成功
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
    for (const auto& item : m_ConfigItemList) {
        if (item->ItemName == p_itemname) {
            return item->ItemContent.c_str(); // 返回配置项内容
        }
    }
    return nullptr; // 如果没有找到对应配置项，返回空指针
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
    const char* itemValue = GetString(p_itemname); // 获取配置项字符串值
    if (itemValue != nullptr) {
        return std::stoi(itemValue); // 将字符串转换为整数并返回
    }
    return def; // 如果没有找到配置项，返回默认值
}
