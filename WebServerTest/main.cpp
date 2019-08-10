#pragma once
#include <iostream>
#include "RequestHttp.h"
#include "Epoll.h"
#include "Util.h"

using namespace std;


int main(){


    std::shared_ptr<Epoll> epoll(new Epoll);
    int ListenNum = 4096;
    epoll->initEpoll(ListenNum);
    int listen_fd = epoll->initServer("127.0.0.1",8080);
    std::shared_ptr<ThreadPool> thread_pool(new ThreadPool);
    thread_pool->InitializerPool(4);
    epoll->doEpoll(thread_pool, listen_fd, ListenNum);
    //epoll->doEpoll(listen_fd, ListenNum, -1, thread_pool);


	return 0;
}

