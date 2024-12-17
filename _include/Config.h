#pragma once
#include<iostream>
#include<vector>
#include"tinyxml2.h"

/**
 * @sa _CConfItem
 * @brief 配置信息的结构体，存储配置项名称和内容
 */
typedef struct _CConfItem
{
	std::string ItemName;
	std::string ItemContent;
}CConfItem, * LPCConfItem;

/**
 * @sa Config
 * @brief 配置管理类，提供加载配置文件和获取配置项的功能
 */
class Config
{
private:
	Config();
public:
	~Config();

private:
	static Config* m_instance;

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
				 Config::m_instance = NULL;
			}
		}
	};

public:
	bool Load(const char* pconfName);          //装载配置文件
	const char* GetString(const char* p_itemname);
	int  GetIntDefault(const char* p_itemname, const int def);

public:
	std::vector<LPCConfItem> m_ConfigItemList; //存储配置信息的列表
};