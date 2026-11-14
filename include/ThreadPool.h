#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>

class ThreadPool {
public:
    ThreadPool(int threads);
    ~ThreadPool();

    // 将任务添加到队列
    void Task(std::function<void()> task);

private:
    // 工作线程数组
    std::vector<std::thread> workers;
    // 任务队列
    std::queue<std::function<void()>> tasks;
    // 互斥锁与条件变量
    std::mutex queue_mutex;
    std::condition_variable condition;
    // 停止标志
    bool stop;
};

#endif