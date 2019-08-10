#include "ThreadPool.h"


ThreadPool::~ThreadPool(){               //析构函数
	{   //{}限定锁的范围
		std::unique_lock<std::mutex> lock(m_mutex);
		stoped = true;       //置为true.  关闭提交
		//抢到锁才可以运行，抢到锁说明线程池中的线程都未持有锁。
		//如果直接使用原子操作的话可能此时有的线程
		//stoped.store(true, std::memory_order_seq_cst);  //置为true.  关闭提交
	}
	m_cond.notify_all();     //唤醒所有
	for (std::thread& worker : workers)
		worker.join();       //等待任务结束
}

bool ThreadPool::InitializerPool(size_t _thread_count){
    if( !workers.empty() )   return true;  //已经初始化过。
	if (_thread_count > MAX_THREADS){
		throw std::runtime_error("Thread Num is too large.");  //运行时错误。
	}
    for(size_t i = 0; i < _thread_count; ++i){
        auto th = [this]() {   //lambda表达式
            for (;;){
				ThreadPoolTask task;
                {   //大括号即控制lock的时间
                    std::unique_lock<std::mutex> lock(this->m_mutex);
                    //和条件变量配合使用，必须使用可以手工控制加解锁的锁。因此使用unique_lock.
                    auto pred = [this] ()->bool{ return this->stoped || !this->_tasks.empty(); };
                    //当stoped为false或任务队列tasks为空时返回false.阻塞
                    //任务队列不为空说明需要继续进行处理工作。
                    //ThreadPool析构时stoped置为true
                    this->m_cond.wait(lock, pred);         //shipment为false才会阻塞当前线程
                    if (this->stoped)   return;            //stoped为true. 说明关闭提交，线程需返回。
                    if (this->_tasks.empty()) continue;    //任务队列为空.

                    //move进行右值引用，转移所有权。
                    task = std::move(this->_tasks.front());  //取队首task.
                    this->_tasks.pop();                      //将刚取到的任务出队
               }
               //task(); //实例化该仿函数。执行
				(task.fun)(task.args);
            }
        };
        workers.emplace_back(std::thread(th));
    }
    return !workers.empty();
}

int ThreadPool::enqueue(std::function<void(std::shared_ptr<void>)> fun, std::shared_ptr<void> args){
	if (workers.empty())	throw std::runtime_error("ThreadPool not initialized yet.");
	{
		std::unique_lock<std::mutex> lck(m_mutex);
		ThreadPoolTask temp;
		temp.fun = fun;
		temp.args = args;
		_tasks.emplace(temp);  //emplace没有临时变量产生。参数自动构造queue内部的对象
	}
	m_cond.notify_one();
	return 0;
}
