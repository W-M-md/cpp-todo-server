#include <iostream>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_map>
#include <string>
#include <mysql/mysql.h>
#include "router.h"
#include "ThreadPool.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <cstring>
#include <cerrno>


MYSQL* g_conn;
void set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {

    //初始化日志系统
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    //日志文件放在 logs/server.log, 每个文件最大 5MB ，最多保留 3 个
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>("logs/server.log", 1024 * 1024 * 5, 3);

    spdlog::set_default_logger(std::make_shared<spdlog::logger>("multi", spdlog::sinks_init_list{console_sink, file_sink}));
    spdlog::set_level(spdlog::level::info); //设置日志级别为 info
    spdlog::flush_on(spdlog::level::info);  //遇到 info 级别立即刷新到磁盘

    spdlog::info("======== 服务器正在启动 ========");
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1) { spdlog::error("socket 失败: {}", strerror(errno)); return 1;}

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    set_nonblocking(listen_fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8888);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) == -1) { spdlog::error("bind 失败：{}", strerror(errno)); return 0;}

    if (listen(listen_fd, 5) == -1) { spdlog::error("listen 失败：{}", strerror(errno)); return 0; }

    int epfd = epoll_create(1);
    if (epfd == -1) { spdlog::error("epoll_create 失败: {}", strerror(errno)); return 1; }

    epoll_event ev{};
    ev.data.fd = listen_fd;
    ev.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, listen_fd, &ev) == -1) { spdlog::error("epoll_ctl 失败: {}", strerror(errno)); return 1; }

    const int MAX_EVENTS = 64;
    epoll_event events[MAX_EVENTS];

    //存储 recv 到的数据
    std::unordered_map<int, std::string> buffers;

    MYSQL* conn = mysql_init(nullptr);
    if (conn == nullptr) {
        spdlog::error("MySQL 初始化失败");
        return 1;
    }

    g_conn = conn;

    if (!mysql_real_connect(conn, "127.0.0.1", "root", "123456", "todo_db", 3306, nullptr, 0)) {
        spdlog::error("MySQL 连接失败：{}", mysql_error(conn));
        return 1;
    }
    spdlog::info("MySQL 连接成功");

    ThreadPool pool(4);
    spdlog::info("线程池启动成功，工作线程数: 4");
    
    while (true) {
        int cnt = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (cnt == -1) { 
            if (errno == EINTR) {
                continue;
            }
            spdlog::error("epoll_wait 失败: {}", strerror(errno));
            return 1;
        }

        for (size_t i = 0; i < cnt; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev_flags = events[i].events;

            if (ev_flags &(EPOLLHUP | EPOLLERR)) {
                spdlog::warn("客户端异常，fd = {}", fd);
                epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                close(fd);
                continue;
            }

            if (fd == listen_fd) {
                while (true) {
                    int client_fd = accept(fd, nullptr, nullptr);
                    if (client_fd == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        spdlog::error("accept 失败: {}", strerror(errno));
                        break;
                    }
                    
                    set_nonblocking(client_fd);

                    ev.data.fd = client_fd;
                    ev.events = EPOLLIN | EPOLLET;

                    epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &ev);
                }
                continue;
            }

            if (ev_flags & EPOLLIN) {
                char buffer[4096];

                //非阻塞模式，循环读取保证请求完整
                while (true) {
                    int n = recv(fd, buffer, sizeof(buffer) - 1, 0);
                    if (n == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break;
                        }
                        spdlog::error("recv 失败: {}", strerror(errno));
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        break;
                    }
                    if ( n == 0) {
                        spdlog::info("客户端正常断开：fd = {}", fd);
                        buffers[fd].erase();
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                        close(fd);
                        break;
                    }
                    if (n > 0) {
                        buffer[n] = '\0';

                        buffers[fd].append(buffer, n);

                        std::string& request = buffers[fd];
                        size_t header_end = request.find("\r\n\r\n");
                        if (header_end != std::string::npos) {
                            size_t Content_Length = 0;
                            size_t pos = request.find("Content-Length: ");
                            if (pos != std::string::npos) {
                                size_t end = request.find("\r\n", pos);
                                std::string s = "Content-Length: ";
                                std::string cl_pos = request.substr(pos + s.size(), end - pos - s.size());
                                
                                try {
                                    Content_Length = std::stol(cl_pos);
                                } catch(...) {
                                    buffers[fd].erase();
                                    close(fd);
                                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);

                                    break;
                                }
                            }        
                            
                            size_t total_expected = Content_Length + header_end + 4;

                            if (request.size() >= total_expected) {
                                std::string complate_request = request.substr(0, total_expected);
                                
                                size_t space1 = complate_request.find(" ");
                                if (space1 != std::string::npos) {
                                    size_t space2 = complate_request.find(" ", space1 + 1);
                                    if (space2 != std::string::npos) {
                                        std::string path = complate_request.substr(space1 + 1, space2 - space1 - 1);
                                        //提取请求
                                        std::string method = complate_request.substr(0, space1);
                                        std::string body_str = complate_request.substr(header_end + 4);
                                        
                                        // 提取 Token
                                        std::string token;
                                        std::string auth_prefix = "Authorization: Bearer "; 
                                        size_t auth_pos = complate_request.find(auth_prefix);
                                        if (auth_pos != std::string::npos) {
                                            size_t start_pos = auth_pos + auth_prefix.length();
                                            size_t auth_end = complate_request.find("\r\n", start_pos);
                                            if (auth_end == std::string::npos) {
                                                token = complate_request.substr(start_pos);
                                            } else {
                                                token = complate_request.substr(start_pos, auth_end - start_pos);
                                            }
                                        }
                                        
                                        // 从主线程剥离，取消监听并清空 buffer
                                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
                                        buffers[fd].erase();
                                        
                                        // 丢进线程池
                                        pool.Task([fd, method, path, body_str, token](){
                                            std::string body_response = handle_router(method, path, body_str, token);

                                            std::string response = 
                                            "HTTP/1.1 200 OK\r\n"
                                            "Content-Type: application/json; charset=utf-8\r\n"
                                            "Content-Length: " + std::to_string(body_response.size()) + "\r\n"
                                            "Connection: close\r\n" 
                                            "\r\n" + body_response;

                                            int total_sent = 0;
                                            while (total_sent < response.length()) {
                                                int n = send(fd, response.c_str() + total_sent, response.length() - total_sent, 0);
                                                if (n > 0) {
                                                    total_sent += n;
                                                } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                                                    continue;
                                                } else {
                                                    break;
                                                }
                                            }

                                            close(fd);
                                        });
                                        break;
                                    }
                                }
                            }
                            
                        }
                        continue;
                    }
                }                
            }
        }
    }

    mysql_close(conn);
}