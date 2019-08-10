//Lchen  cl-work@qq.com
#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#pragma once
#include <functional>
#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <queue>
#include <future>
#include <sstream>
#include <iostream>

const size_t MAX_THREADS = 128;

struct ThreadPoolTask{
	std::function<void(std::shared_ptr<void>)> fun;
	std::shared_ptr<void> args;
};
class ThreadPool{
private:
    //using Task = std::function<void()>;
    typedef std::vector<std::thread> Threads;
	std::queue<ThreadPoolTask> _tasks;  //任务队列
    Threads workers;                    //线程池
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::atomic<bool> stoped;           //是否关闭提交

public:
    ThreadPool():stoped(false){}
	~ThreadPool();
    bool InitializerPool(size_t _thread_count);
	int enqueue(std::function<void(std::shared_ptr<void>)> fun, std::shared_ptr<void> args);
};

#endif // THREAD_POOL_H
