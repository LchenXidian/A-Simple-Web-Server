#ifndef _EPOLL_H
#define _EPOLL_H
#pragma once

#include <string>
#include <iostream>
#include <sys/epoll.h>
#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>
#include <stdarg.h>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include "Timer.h"
#include "RequestHttp.h"
#include "ThreadPool.h"

using namespace std;

const int MaxEvents = 65535;
const int TIME_OUT = 300;

class RequestHttp;
class Epoll: public std::enable_shared_from_this<Epoll>
{
private:
    int epollfd;
    int listen_sockfd;
    std::unordered_map<int, std::shared_ptr<RequestHttp> > fd_to_request;
    struct epoll_event* events;

public:
    Epoll(){}
    ~Epoll(){
        if(events != nullptr){
            delete[] events;
        }
        close(listen_sockfd);
    }

    void initEpoll(int maxEvents = MaxEvents);
    int initServer(std::string hostName, int port, int socketType = SOCK_STREAM);
    int connectServer(std::string hostName, int port, int socketType = SOCK_STREAM);
    int socketBind(std::string hostName, int port, int socketType = SOCK_STREAM);
    std::string getIpByHost(std::string hostName, int port);
    bool add_Event(int fd, __uint32_t state, std::shared_ptr<RequestHttp> request);
    bool remove_Event(int fd, __uint32_t state);
    bool modify_Event(int fd, __uint32_t state, std::shared_ptr<RequestHttp> request);
    void handleAccpet(int fd, int listenfd);
    void setNonBlocking(int fd);
    void doEpoll(std::shared_ptr<ThreadPool> thread_pool, int listen_fd, int maxEvents, int timeout = -1);
    void doEpoll(int n, int maxEvents, int timed_out, std::shared_ptr<ThreadPool> thread_pool);


    void AcceptConnection(int listen_fd, int epollfd);
    std::vector<std::shared_ptr<RequestHttp>> getEventsRequest(int listen_fd, int events_num);
};



#endif // _EPOLL_H
