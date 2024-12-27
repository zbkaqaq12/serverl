#pragma once
#include<iostream>
#include<vector>
#include <string>
#include <unordered_map>
#include"tinyxml2.h"

/**
 * @sa _CConfItem
 * @brief 配置信息的结构体，存储配置项名称和内容
 */
struct CConfItem {
	std::string ItemName;     // 配置项名称
	std::string ItemContent;  // 配置项内容
	std::string Section;      // 配置项所属区段
};

/**
 * @sa Config
 * @brief 配置管理类，提供加载配置文件和获取配置项的功能
 */
class Config
{
private:
	Config();
	static Config* m_instance;
	std::unordered_map<std::string, CConfItem> m_ConfigMap;  // 使用哈希表存储配置
	std::string m_configFile;
public:
	~Config();

public:
	static Config* GetInstance()
	{
		if (m_instance == nullptr)
		{
			if (m_instance == nullptr)
			{
				m_instance = new Config();
				static Garhuishou c1;
			}
		}
		return m_instance;
	}

	class Garhuishou  //内部类，用于释放对象
	{
	public:
		~Garhuishou()
		{
			if (Config::m_instance)
			{
				delete Config::m_instance;
				 Config::m_instance = nullptr;
			}
		}
	};


public:
	// 基本配置操作
	bool Load(const char* pconfName);
	bool Reload();  // 重新加载配置
	
	// 获取配置项
	const char* GetString(const char* p_itemname);
	int GetIntDefault(const char* p_itemname, const int def);
	bool GetBool(const char* p_itemname, bool def = false);
	
	// 按区段获取配置
	std::vector<CConfItem> GetSection(const std::string& section);
	
	// 检查配置项是否存在
	bool Exists(const char* p_itemname) const;
};