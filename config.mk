#定义项目编译的根目录,通过export把某个变量声明为全局的[其他文件中可以用]，这里获取当前这个文件所在的路径作为根目录；
export BUILD_ROOT = $(shell pwd)

#定义头文件的路径变量
export INCLUDE_PATH = $(BUILD_ROOT)/_include \
                     $(BUILD_ROOT)/_include/base \
                     $(BUILD_ROOT)/_include/core \
                     $(BUILD_ROOT)/_include/net \
                     $(BUILD_ROOT)/_include/net/http \
                     $(BUILD_ROOT)/_include/util \
                     $(BUILD_ROOT)/_include/proc \
                     $(BUILD_ROOT)/_include/signal \
                     /usr/include/

#定义我们要编译的目录
BUILD_DIR = $(BUILD_ROOT)/signal/\
           $(BUILD_ROOT)/proc/\
           $(BUILD_ROOT)/net/\
           $(BUILD_ROOT)/net/http/\
           $(BUILD_ROOT)/misc/\
           $(BUILD_ROOT)/misc/base/\
           $(BUILD_ROOT)/misc/json/\
           $(BUILD_ROOT)/misc/validator/\
           $(BUILD_ROOT)/logic/\
           $(BUILD_ROOT)/logic/controller/\
           $(BUILD_ROOT)/logic/service/\
           $(BUILD_ROOT)/logic/model/\
           $(BUILD_ROOT)/app/

#编译时是否生成调试信息。GNU调试器可以利用该信息
export DEBUG = true

# 定义常用的编译选项
export CXXFLAGS = -Wall -Wextra -I$(INCLUDE_PATH)

# 如果是调试版本，添加-g选项
ifeq ($(DEBUG),true)
    export CXXFLAGS += -g
endif

# 定义链接选项
export LDFLAGS = -lpthread -lcrypto -lssl

