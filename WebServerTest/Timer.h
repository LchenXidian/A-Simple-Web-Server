#ifndef TIMER_H
#define TIMER_H

#pragma once
#include <memory>
#include <queue>
#include "RequestHttp.h"
#include <vector>
#include <chrono>

class RequestHttp;
class Epoll;
class TimeNode{
public:
    TimeNode(std::shared_ptr<RequestHttp> req_data, size_t time);
    ~TimeNode();

    void updataExpireTime(size_t time);  //更新过期时间
    void ClearRequestData();
    void setDeleted();
    bool isValid();                      //判断是否过期
    bool isDeleted();
    size_t GetExpireTime();              //获取过期时间

private:
    bool IsDeleted;
    size_t expire_time;
    std::shared_ptr<RequestHttp> request_data;
};



struct timerCmp{   //priority_queue的比较函数
    bool operator()(std::shared_ptr<TimeNode> &p, std::shared_ptr<TimeNode> &q) const{
        return p->GetExpireTime() > q->GetExpireTime();
    }
};

class TimeManager{
public:
    TimeManager();
    ~TimeManager();
    void AddTime(std::shared_ptr<RequestHttp> request_data, size_t time);
    void Handler_Expired_Event();

private:
    std::mutex mu;
    typedef std::shared_ptr<TimeNode> T;
    std::priority_queue<T, std::deque<T>, timerCmp> TimeNodeQueue;

};


#endif // TIMER_H

