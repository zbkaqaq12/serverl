#include"CSocket.h"
#include"global.h"
#include <sys/ioctl.h> //ioctl
#include <arpa/inet.h>

/**
 * @brief 将套接字地址转换为字符串表示
 * @details 该函数负责将传入的 `sockaddr` 地址转换为字符串格式，支持IPv4地址，并可选择是否包含端口信息。
 *
 * @param sa 套接字地址
 * @param port 是否包含端口信息的标志，1表示包含端口，0表示不包含
 * @param text 用于存储转换结果的缓冲区
 * @param len 缓冲区的最大长度
 *
 * @return 返回转换后的字符串长度
 */
size_t CSocket::sock_ntop(sockaddr* sa, int port, u_char* text, size_t len)
{
	if (sa->sa_family == AF_INET)  // 处理IPv4地址
	{
		struct sockaddr_in* sin = (struct sockaddr_in*)sa;
		u_char* p = (u_char*)&sin->sin_addr;

		// 如果需要端口信息
		if (port) {
			// 将IPv4地址和端口号格式化成字符串
			return snprintf((char*)text, len, "%u.%u.%u.%u:%d",
				(unsigned char)p[0], (unsigned char)p[1],
				(unsigned char)p[2], (unsigned char)p[3],
				ntohs(sin->sin_port));
		}
		else {
			// 仅格式化IPv4地址
			return snprintf((char*)text, len, "%u.%u.%u.%u",
				(unsigned char)p[0], (unsigned char)p[1],
				(unsigned char)p[2], (unsigned char)p[3]);
		}

		return (p - text);  // 返回格式化后的字符串长度
	}

	return 0;  // 非IPv4地址时返回0
}
