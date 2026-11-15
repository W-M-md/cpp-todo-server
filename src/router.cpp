#include "../include/router.h"
#include <iostream>
#include <shared_mutex>
#include <random>
#include <unordered_map>
#include <spdlog/spdlog.h> // ⭐ 引入日志库
#include "ConnectionPool.h"

using json = nlohmann::json;

std::shared_mutex token_mtx;
// 全局 Token 缓存：Token 字符串 -> UserId
std::unordered_map<std::string, int> token_cache;

// 生成 32 位随机 Hex 字符串作为 Token
std::string generate_token() {
    static std::mt19937 gen(std::random_device{}());
    static std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    std::string token = "";
    for (int i = 0; i < 32; ++i) token += hex[dis(gen)];
    return token;
}

std::string handle_router(const std::string& method, 
                          const std::string& path, 
                          const std::string& body, 
                          const std::string& token) {
    

    json response_json;

    // ==========================================
    // 1. 登录接口（无需 Token）
    // ==========================================
    if (method == "POST" && path == "/api/login") {
        try {
            json json_body = json::parse(body);
            // 注意：默认值给空字符串，而不是空格
            std::string username = json_body.value("username", "");
            std::string password = json_body.value("password", "");

            if (username.empty() || password.empty()) {
                response_json["code"] = 400;
                response_json["msg"] = "用户名或密码不能为空";
                return response_json.dump();
            }

            auto conn = ConnectionPool::getInstance().getConnection();

            // 防 SQL 注入
            char escaped_user[256];
            char escaped_pass[256];
            mysql_real_escape_string(conn.get(), escaped_user, username.c_str(), username.length());
            mysql_real_escape_string(conn.get(), escaped_pass, password.c_str(), password.length());

            std::string sql = "SELECT id FROM users WHERE username = '" + 
                              std::string(escaped_user) + "' AND password = '" + 
                              std::string(escaped_pass) + "'"; 

            if (mysql_query(conn.get(), sql.c_str()) != 0) {
                spdlog::error("登录查询数据库失败: {}", mysql_error(conn.get()));
                response_json["code"] = 500;
                response_json["msg"] = "服务器内部错误"; // ⭐ 错误脱敏
            } else {
                MYSQL_RES* result = mysql_store_result(conn.get());
                if (result == nullptr) {
                    spdlog::error("获取结果集失败: {}", mysql_error(conn.get()));
                    response_json["code"] = 500;
                    response_json["msg"] = "服务器内部错误";
                } else {
                    MYSQL_ROW row = mysql_fetch_row(result);
                    if (row != nullptr) {
                        int user_id = std::stoi(row[0]);
                        
                        std::string new_token = generate_token();
                        // 生成 Token 并缓存
                        {
                            std::lock_guard<std::shared_mutex> lock(token_mtx);
                            token_cache[new_token] = user_id;
                        }

                        spdlog::info("用户 [{}] 登录成功，分配 Token: {}", username, new_token);
                        response_json["code"] = 200;
                        response_json["msg"] = "登录成功";
                        response_json["token"] = new_token;
                    } else {
                        spdlog::warn("用户 [{}] 登录失败：密码错误", username);
                        response_json["code"] = 401;
                        response_json["msg"] = "用户名或密码错误";
                    }
                    mysql_free_result(result); // ⭐ 极其重要，防止内存泄漏
                }
            }
        } catch(...) {
            spdlog::warn("登录请求 JSON 解析失败");
            response_json["code"] = 400;
            response_json["msg"] = "请求参数格式错误";
        }
        return response_json.dump();
    }

    // ==========================================
    // 2. 拦截器：除注册和登录外，必须校验 Token
    // ==========================================

    // ==========================================
    // 2. 拦截器：除注册和登录外，必须校验 Token
    // ==========================================
    int current_user_id = 0;
    if (path != "/api/register" && path != "/api/login") {
        std::shared_lock<std::shared_mutex> lock(token_mtx);
        auto it = token_cache.find(token);
        if (token.empty() || it == token_cache.end()) {
            spdlog::warn("拦截非法请求，Token 无效或缺失。请求路径: {}", path);
            response_json["code"] = 401;
            response_json["msg"] = "未授权，请先登录";
            return response_json.dump();
        }
        current_user_id = it->second; // ⭐ 一次加锁，搞定两件事
    }
    
    // ==========================================
    // 3. 注册接口
    // ==========================================
    if (method == "POST" && path == "/api/register") {
        try {
            json json_body = json::parse(body);
            std::string username = json_body.value("username", "");
            std::string password = json_body.value("password", "");

            if (username.empty() || password.empty()) {
                response_json["code"] = 400;
                response_json["msg"] = "用户名或密码不能为空";
                return response_json.dump();
            }
            auto conn = ConnectionPool::getInstance().getConnection();

            char escaped_user[256];
            char escaped_pass[256];
            mysql_real_escape_string(conn.get(), escaped_user, username.c_str(), username.length());
            mysql_real_escape_string(conn.get(), escaped_pass, password.c_str(), password.length());

            std::string sql = "INSERT INTO users(username, password) VALUES('" + 
                              std::string(escaped_user) + "', '" + std::string(escaped_pass) + "')";  

            if (mysql_query(conn.get(), sql.c_str()) != 0) {
                spdlog::error("注册插入失败: {}", mysql_error(conn.get()));
                response_json["code"] = 500;
                response_json["msg"] = "注册失败，用户名可能已存在";
            } else {
                spdlog::info("新用户注册成功: {}", username);
                response_json["code"] = 200;
                response_json["msg"] = "注册成功";
            }
        } catch(...) {
            response_json["code"] = 400;
            response_json["msg"] = "请求参数格式错误";
        }
        return response_json.dump();
    }

    // ==========================================
    // 4. 新增待办
    // ==========================================
    if (method == "POST" && path == "/api/todos") {
        try {
            json json_body = json::parse(body);
            std::string title = json_body.value("title", "");

            if (title.empty()) {
                response_json["code"] = 400;
                response_json["msg"] = "标题不能为空";
                return response_json.dump();
            }

            auto conn = ConnectionPool::getInstance().getConnection();

            char escaped_title[512];
            mysql_real_escape_string(conn.get(), escaped_title, title.c_str(), title.length());

            // ⭐ 使用动态的 current_user_id 绑定数据
            std::string sql = "INSERT INTO todos(user_id, title) VALUES(" + 
                              std::to_string(current_user_id) + ", '" + std::string(escaped_title) + "')";

            if (mysql_query(conn.get(), sql.c_str()) != 0) {
                spdlog::error("添加待办失败: {}", mysql_error(conn.get()));
                response_json["code"] = 500;
                response_json["msg"] = "服务器内部错误";
            } else {
                response_json["code"] = 200;
                response_json["msg"] = "添加成功";
            }
        } catch(...) {
            response_json["code"] = 400;
            response_json["msg"] = "请求参数格式错误";
        }
        return response_json.dump();
    }

    // ==========================================
    // 5. 查询待办列表
    // ==========================================
    if (method == "GET" && path == "/api/todos") {
        auto conn = ConnectionPool::getInstance().getConnection();
        // ⭐ 强加 WHERE user_id 过滤，实现数据隔离
        std::string sql = "SELECT id, title, status, create_time FROM todos WHERE user_id = " + std::to_string(current_user_id);

        if (mysql_query(conn.get(), sql.c_str()) != 0) {
            spdlog::error("查询待办失败: {}", mysql_error(conn.get()));
            response_json["code"] = 500;
            response_json["msg"] = "服务器内部错误";
            return response_json.dump();
        }

        MYSQL_RES* result = mysql_store_result(conn.get());
        if (result == nullptr) {
            spdlog::error("获取结果集失败: {}", mysql_error(conn.get()));
            response_json["code"] = 500;
            response_json["msg"] = "服务器内部错误";
            return response_json.dump();
        }

        json todos_array = json::array();
        MYSQL_ROW row;
        while((row = mysql_fetch_row(result)) != nullptr) {
            json item;
            item["id"] = std::stoi(row[0]);
            item["title"] = row[1];
            item["status"] = std::stoi(row[2]);
            item["create_time"] = row[3];
            todos_array.push_back(item);
        }
        mysql_free_result(result);

        response_json["code"] = 200;
        response_json["msg"] = "success";
        response_json["data"] = todos_array;
        return response_json.dump();
    }

    // ==========================================
    // 6. 删除待办
    // ==========================================
    if (method == "DELETE" && path.find("/api/todos/") == 0) {
        try {
            std::string s = "/api/todos/";
            std::string id_str = path.substr(s.length());
            int id = std::stoi(id_str);

            auto conn = ConnectionPool::getInstance().getConnection();

            // ⭐ 强加 AND user_id，防止水平越权
            std::string sql = "DELETE FROM todos WHERE id = " + std::to_string(id) +
                              " AND user_id = " + std::to_string(current_user_id);

            if (mysql_query(conn.get(), sql.c_str()) != 0) {
                spdlog::error("删除待办失败: {}", mysql_error(conn.get()));
                response_json["code"] = 500;
                response_json["msg"] = "服务器内部错误";
            } else {
                // 检查是否真的删除了数据
                if (mysql_affected_rows(conn.get()) > 0) {
                    response_json["code"] = 200;
                    response_json["msg"] = "删除成功";
                } else {
                    response_json["code"] = 404;
                    response_json["msg"] = "待办不存在或无权删除";
                }
            }
        } catch(...) {
            response_json["code"] = 400;
            response_json["msg"] = "ID 格式错误";
        }
        return response_json.dump();
    }

    // ==========================================
    // 7. 更新待办
    // ==========================================
    if (method == "PUT" && path.find("/api/todos/") == 0) {
        try {
            std::string s = "/api/todos/";
            std::string id_str = path.substr(s.length());
            int id = std::stoi(id_str);

            json json_body = json::parse(body);
            int status = json_body.value("status", 0);

            auto conn = ConnectionPool::getInstance().getConnection();

            // ⭐ 强加 AND user_id，防止水平越权
            std::string sql = "UPDATE todos SET status = " + std::to_string(status) + 
                              " WHERE id = " + std::to_string(id) + 
                              " AND user_id = " + std::to_string(current_user_id);

            if (mysql_query(conn.get(), sql.c_str()) != 0) {
                spdlog::error("更新待办失败: {}", mysql_error(conn.get()));
                response_json["code"] = 500;
                response_json["msg"] = "服务器内部错误";
            } else {
                if (mysql_affected_rows(conn.get()) > 0) {
                    response_json["code"] = 200;
                    response_json["msg"] = "更新成功";
                } else {
                    response_json["code"] = 404;
                    response_json["msg"] = "待办不存在或无权修改";
                }
            }
        } catch(...) {
            response_json["code"] = 400;
            response_json["msg"] = "ID 或 JSON 格式错误";
        }
        return response_json.dump();
    }

    // ==========================================
    // 8. 兜底 404
    // ==========================================
    response_json["code"] = 404;
    response_json["msg"] = "Not Found";
    return response_json.dump();
}