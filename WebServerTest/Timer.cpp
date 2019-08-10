#include "Timer.h"

TimeNode::TimeNode(std::shared_ptr<RequestHttp> req_data, size_t time):
    request_data(req_data), IsDeleted(false) {
        //std::time_t now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
        //std::chrono::system_clock::now()获取从1970-1-1 00:00:00到闲杂的时间
        //
        expire_time = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() ) + time;
}

TimeNode::~TimeNode(){
    if(request_data){
        //request_data->关闭。
    }
}

void TimeNode::updataExpireTime(size_t time){  //更新过期时间
    expire_time = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() ) + time;
}

void TimeNode::ClearRequestData(){
    request_data.reset();
    this->setDeleted();
}

void TimeNode::setDeleted(){
    IsDeleted = true;
}

bool TimeNode::isValid(){   //是否有效
    std::time_t now = std::chrono::system_clock::to_time_t( std::chrono::system_clock::now() );
    if(now < expire_time){  //未过期返回true
        return true;
    }
    setDeleted();   //过期之后置为可删除。
    return false;
}

bool TimeNode::isDeleted(){
    return IsDeleted;
}

size_t TimeNode::GetExpireTime(){       //获取过期时间
    return expire_time;
}

TimeManager::TimeManager(){}

TimeManager::~TimeManager(){}

void TimeManager::AddTime(std::shared_ptr<RequestHttp> request_data, size_t time){
    std::shared_ptr<TimeNode> temp(new TimeNode(request_data, time));
    //request_data->LineTimer()  //RequestHttp中的指针指向新构建的TimeNode对象
    std::unique_lock<mutex> lck(mu);
    TimeNodeQueue.emplace( std::move(temp) ); //
}

