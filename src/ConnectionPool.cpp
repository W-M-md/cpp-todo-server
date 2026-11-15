#include "ConnectionPool.h"
#include <iostream>
#include <spdlog/spdlog.h>

ConnectionPool& ConnectionPool::getInstance() {
    static ConnectionPool instance;
    return instance;
}

void ConnectionPool::init(const std::string& host, const std::string& user, 
const std::string& password, const std::string& db_name, int port, int poolSize) 
{
    std::lock_guard<std::mutex> lock(mtx);
    max_size = poolSize;

    for (int i = 0; i < poolSize; ++i) {
        MYSQL* conn = mysql_init(nullptr);
        if (!mysql_real_connect(conn, host.c_str(), user.c_str(), password.c_str(), 
        db_name.c_str(), port, nullptr, 0)) {
            spdlog::error("连接池初始化失败：{}", mysql_error(conn));
            continue;
        }
        connections.push(conn);
    }
    spdlog::info("连接池初始化成功，创建了 {} 个连接", connections.size());
}

std::shared_ptr<MYSQL> ConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this]() { return !connections.empty(); });

    MYSQL* conn = connections.front();
    connections.pop();

    // //尝试 ping 一下，如果连接断开，重连
    // if (mysql_ping(conn) != 0) {
    //     spdlog::warn("数据库连接断开，尝试重连....");
    //     mysql_close(conn);

    //     //待补充（真实的重连逻辑）

    // }

    return std::shared_ptr<MYSQL>(conn, [this](MYSQL* c) {
        this->returnConnection(c);
    });
}

void ConnectionPool::returnConnection(MYSQL* conn) {
    std::lock_guard<std::mutex> lock(mtx);
    connections.push(conn);
    cv.notify_one();
}

ConnectionPool::~ConnectionPool() {
    while (!connections.empty()) {
        MYSQL* conn = connections.front();
        connections.pop();
        mysql_close(conn);
    }
}