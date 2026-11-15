#ifndef CONNECTIONPOOL_H
#define CONNECTIONPOOL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <chrono>

class ConnectionPool {
    public:
    //单例模式，获取全局唯一连接池实例
    static ConnectionPool& getInstance();

    //初始化连接池
    void init(const std::string& host, const std::string& user, const std::string& password,
    const std::string& dbname, int port, int poolSize) ;

    //借出一个连接
    std::shared_ptr<MYSQL> getConnection();

    ~ConnectionPool();

    private:

    ConnectionPool() = default;
    ConnectionPool(const ConnectionPool& other) = delete;
    ConnectionPool& operator=(const ConnectionPool& other) = delete;
    
    //归还给池子
    void returnConnection(MYSQL* conn);

    std::queue<MYSQL*> connections;
    std::mutex mtx;
    std::condition_variable cv;
    int max_size;
};

#endif