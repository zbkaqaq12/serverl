#pragma once
#include <iostream>
#include <fstream>
#include <unistd.h>
#include <string>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <memory>
#include "global.h"

/**
 * @class Daemon
 * @brief �ػ������࣬���ڽ�����תΪ�ػ����̡�
 * 
 * ����ͨ�� `fork()`��`setsid()` ���ض����׼��������� `/dev/null` ��ʵ�ֽ�����תΪ�ػ����̡�
 */
class Daemon {
public:
    /**
     * @brief ���캯������ʼ���ػ�������Ϣ��
     * 
     * @param pid �ӽ��̵� PID��
     * @param ppid �����̵� PID��
     */
    Daemon(pid_t pid, pid_t ppid)
        : pid_(pid), ppid_(ppid), fd_(-1) {}

    /**
     * @brief ��ʼ���ػ����̣����������ӽ��̡���������ն˺��ض�������
     * 
     * �����ӽ��̲���������նˣ��ɹ��󽫱�׼���롢����ʹ����ض��� `/dev/null`��
     * 
     * @return ���� 1 ��ʾ�����̣����� 0 ��ʾ�ӽ��̣����� -1 ��ʾ��ʼ��ʧ�ܡ�
     */
    int init() {
        // ��һ�� fork() �����ӽ���
        pid_t pid = fork();
        if (pid == -1) {
            logError("fork()ʧ��");
            return -1;  // ��� fork() ʧ�ܣ����ش���
        }

        if (pid > 0) {
            return 1;  // ������ֱ���˳�
        }

        // �ӽ���
        if (setsid() == -1) {
            globallogger->flog(LogLevel::ERROR, "setsid()ʧ��");
            return -1;  // ��� setsid() ʧ�ܣ����ش���
        }

        umask(0);  // �����ļ���������Ϊ0��ȷ���ļ�Ȩ�޲�������

        // �� /dev/null ���ض����׼��
        if (!openNullDevice()) {
            return -1;  // ����� /dev/null ʧ�ܣ����ش���
        }

        return 0;  // �ӽ��̳ɹ�������ִ��
    }

private:
    pid_t pid_;  ///< �ӽ��� PID
    pid_t ppid_;  ///< ������ PID
    int fd_;  ///< /dev/null ���ļ�������
    /**
     * @brief ������־���������
     * 
     * @param message ������Ϣ��
     */
    void logError(const std::string& message) const {
        globallogger->flog(LogLevel::EMERG, message.c_str());
    }

    /**
     * @brief �� `/dev/null` �豸���ض����׼���롢��׼�������׼��������
     * 
     * @return ����ɹ����� true�����򷵻� false��
     */
    bool openNullDevice() {
        fd_ = open("/dev/null", O_RDWR);
        if (fd_ == -1) {
            globallogger->flog(LogLevel::ERROR, "open(\"/dev/null\")ʧ��");
            return false;
        }

        // ����׼���롢��׼����ͱ�׼�����ض��� /dev/null
        if (!redirectStream(STDIN_FILENO) || !redirectStream(STDOUT_FILENO) || !redirectStream(STDERR_FILENO)) {
            return false;
        }

        return true;
    }

    /**
     * @brief ����׼���ض��� `/dev/null`��
     * 
     * @param stream Ҫ�ض�������������� `STDIN_FILENO`��`STDOUT_FILENO` �� `STDERR_FILENO`��
     * 
     * @return ����ɹ����� true�����򷵻� false��
     */
    bool redirectStream(int stream) {
        if (dup2(fd_, stream) == -1) {
            globallogger->flog(LogLevel::ERROR, "dup2()ʧ�ܣ�����%s", std::to_string(stream));
            return false;
        }
        return true;
    }
};
