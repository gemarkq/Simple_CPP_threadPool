#ifndef __THREAD_POOL_H__
#define __THREAD_POOL_H__

#include <thread>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <vector>
#include <atomic>
#include <iostream>


class ThreadPool {
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    static ThreadPool& instance() {
        static ThreadPool ins;
        return ins;
    }

    using Task = std::packaged_task<void()>;

    template <class F, class... Args>
    auto commit(F&& f, Args... args) -> std::future<decltype(f(args...))> {
        using RetType = decltype(f(args...));
        if (stop_) {
            return std::future<RetType>{};
        }

        auto task = std::make_shared<std::packaged_task<RetType>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...));

        std::future<RetType> ret = task.get_future();
        {
            std::lock_guard<std::mutex> lock(cv_mt_);
            tasks_.emplace_back([task]{ (*task)(); });
        }
        cv_lock_.notify_one();
        return ret;
    }

    int idle_thread_count() const {
        return thread_num;
    }

    ~ThreadPool() {
        stop();
    }


private:
    ThreadPool(unsigned int num = 5) : stop_(false) {
        if (num < 1) {
            thread_num = 1;
        } else {
            thread_num = num;
        }
        start();
    }

    void start() {
        for (int i = 0; i < thread_num; i++) {
            pool_.emplace_back([this](){
                while(!this->stop_.load()) {
                    // do task
                    Task task;
                    {
                        std::unique_lock<std::mutex> cv_mt(cv_mt_);
                        this->cv_lock_.wait(cv_mt, [this] {
                            return this->stop_.load() || !this->tasks_.empty();
                        });
                        if (tasks_.empty()) {
                            return;
                        }
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    this->thread_num--;
                    task();
                    this->thread_num++;
                }
            });
        }
    }

    void stop() {
        // stop
        stop_.store(true);
        cv_lock_.notify_all();
        for (auto& td : pool_) {
            if(td.joinable()) {
                std::cout << "join thread: " << td.get_id() << std::endl;
                td.join();
            }
        }
    }


    int thread_num = 0;
    std::atomic<int> stop_;
    std::vector<std::thread> pool_;
    std::mutex cv_mt_;
    std::condition_variable cv_lock_;
    std::queue<Task> tasks_;
};

#endif