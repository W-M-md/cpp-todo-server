#include "ThreadPool.h"

ThreadPool::ThreadPool(int threads) : stop(false) {
    for (int i = 0; i < threads; ++i) {
        // 创建线程，并把 lambda 函数作为线程的执行体
        workers.emplace_back([this]() {
            // ⭐ 注意：必须有 while(true) 循环，否则线程执行完一个任务就退出了！
            while (true) {
                std::function<void()> task;
                {
                    // 1. 加锁保护任务队列
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    
                    // 2. 如果队列为空且没被通知停止，线程就阻塞在这里等待
                    condition.wait(lock, [this]() { 
                        return stop || !this->tasks.empty(); 
                    });

                    // 3. 如果收到停止信号且队列为空，线程退出
                    if (stop && this->tasks.empty()) return;

                    // 4. 取出任务
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                } // ⭐ 锁作用域结束，释放锁！

                // 5. 执行任务（在无锁状态下执行，保证高并发）
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    // 唤醒所有正在等待的线程
    condition.notify_all();

    // 等待所有线程安全退出
    for (auto& thread : workers) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void ThreadPool::Task(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        tasks.push(std::move(task));
    }
    // 放入任务后，唤醒一个正在睡眠的线程起来干活
    condition.notify_one();
}