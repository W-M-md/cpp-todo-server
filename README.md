# Cpp-Todo-Server (高并发待办事项后端)

## 📖 项目简介
基于 Linux epoll + 单 Reactor 多线程模型的高并发待办事项（Todo）后端服务。支持用户注册、登录、Token 鉴权以及多用户待办事项的完整 CRUD（增删改查），实现了严格的数据隔离与安全防护。

## 🛠 技术栈
- **网络层**：C++11, epoll (ET 模式), 非阻塞 I/O, 自研线程池 (Reactor 模式)
- **数据层**：MySQL (C API), SQL 注入防御, 互斥锁并发控制
- **业务层**：RESTful API 设计, nlohmann/json 解析, Token 无状态鉴权
- **工程化**：CMake 构建, spdlog 分级日志 (终端+文件双通道)

## 🧠 核心架构与亮点设计
- **网络与业务解耦**：主线程只负责 `epoll_wait` 和收发数据，拼包完整后投递至线程池，工作线程负责业务解析与查库，避免 I/O 阻塞主事件循环。
- **安全防御（防注入与越权）**：
  - 所有数据库操作均使用 `mysql_real_escape_string` 进行转义，彻底杜绝 SQL 注入。
  - 在 SQL 层面强制加上 `AND user_id = ?` 约束，结合 Token 鉴权，彻底杜绝水平越权漏洞。
- **错误脱敏与日志隔离**：对客户端仅返回通用错误提示（如“服务器内部错误”），真实的数据库异常（`mysql_error`）通过 `spdlog` 记入本地日志文件（`logs/server.log`），防止敏感信息泄露。

## 📋 API 接口文档
| 接口 | 方法 | 描述 | 需要 Token |
| :--- | :--- | :--- | :--- |
| `/api/register` | POST | 用户注册 | 否 |
| `/api/login` | POST | 用户登录获取 Token | 否 |
| `/api/todos` | GET | 获取当前用户待办列表 | 是 |
| `/api/todos` | POST | 新增待办事项 | 是 |
| `/api/todos/{id}` | PUT | 修改待办状态 | 是 |
| `/api/todos/{id}` | DELETE | 删除待办事项 | 是 |

## 🚀 编译与运行
1. 依赖安装：`sudo apt install libmysqlclient-dev libspdlog-dev libfmt-dev`
2. 编译：
   ```bash
   mkdir build && cd build
   cmake .. && make -j4
