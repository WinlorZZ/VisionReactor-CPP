#pragma once

#include <vector>
#include <thread>
#include <queue>
#include <functional> // function 包含 std::function 和 std::bind 等工具
#include <mutex>
#include <condition_variable>
#include <future> // 包含 std::future 和 std::packaged_task 等工具

class ThreadPool
{
public:
    ThreadPool(size_t threads);
    ~ThreadPool();
    template<class F, class... Args>
    auto add(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type>;
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mutex;
    std::condition_variable cv;
    bool stop;
};

inline ThreadPool::ThreadPool(size_t threads)
{
    stop = false;
    // 创建指定数量的工作线程
    for (size_t i = 0; i < threads; i++)
    {
        workers.emplace_back(
            [this]
            {
                while (true)
                {
                    std::function<void()> task;
                    // 在临界区内进行任务队列的访问和修改，并在获得任务后执行任务
                    {                                                   // 临界区
                        std::unique_lock<std::mutex> lock(this->mutex); // 加锁保护任务队列
                        // 等待任务队列非空或线程池停止
                        this->cv.wait(lock, [this]()
                                      { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty())
                            return; // 如果线程池停止且没有任务，退出线程
                        // 从任务队列中取出一个任务
                        task = std::move(this->tasks.front());
                        this->tasks.pop(); // 从队列中移除任务\
                        // 临界区结束，自动解锁
                    }
                    // 执行任务，std::function对象的调用运算符被重载，可以直接调用对象来执行任务
                    task();
                }
            } // 整段代码是一个lambda表达式，直接返回一个新元素function<void()>对象，并将其添加到workers向量中
        );
    }
}

// 线程池的析构函数，负责停止所有线程并清理资源
inline ThreadPool::~ThreadPool()
{
    {
        // 临界区，保护stop标志和任务队列
        // 加锁保护stop标志和任务队列，确保线程安全地修改这些共享资源
        std::unique_lock<std::mutex> lock(mutex);
        stop = true; // 设置停止标志，通知所有线程停止工作
    }
    // 通知所有工作线程退出循环
    cv.notify_all();
    // 等待所有工作线程结束
    for (std::thread &worker : workers)
    {                  // worker是workers向量中的每个线程对象的引用
        worker.join(); // join()函数等待线程完成，确保所有线程在析构函数返回之前都已正确退出
    }
}

// 添加任务的模版函数
template <class F, class... Args>
auto ThreadPool::add(F &&f, Args &&...args) -> std::future<typename std::result_of<F(Args...)>::type>
{
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(mutex);

        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task]()
                      { (*task)(); });
    }
    cv.notify_one();
    return res;
}