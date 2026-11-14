#ifndef ROUTER_H
#define ROUTER_H

#include <string>
#include <mysql/mysql.h>
#include <nlohmann/json.hpp>

// 声明全局数据库连接（在 todo-server.cpp 中定义）
extern MYSQL* g_conn;

// 声明路由处理函数，新增了 token 参数
std::string handle_router(const std::string& method, 
                          const std::string& path, 
                          const std::string& body, 
                          const std::string& token);

#endif