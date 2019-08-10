//Lchen  cl-work@qq.com
#ifndef HTTPDATA_H
#define HTTPDATA_H

#pragma once
#include <string>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <sys/mman.h>
#include "Epoll.h"
#include "Timer.h"
#include "Util.h"
#include <fstream>

const int MAX_BUFF = 4096;
const int EPOLL_WAIT_TIME = 500;

enum ConnectionState{   //枚举类型
	H_CONNECTED = 0,
	H_DISCONNECTING,
	H_DISCONNECTED
};
enum HttpMethod{
	METHOD_POST = 1,
	METHOD_GET,
	METHOD_HEAD
};
enum HttpVersion{
	HTTP_10 = 1,
	HTTP_11
};
enum ProcessState{
	STATE_PARSE_URI = 1,
	STATE_PARSE_HEADERS,
	STATE_RECV_BODY,
	STATE_ANALYSIS,
	STATE_FINISH
};
enum URIState{
	PARSE_URI_AGAIN = 1,
	PARSE_URI_ERROR,
	PARSE_URI_SUCCESS,
};
enum HeaderState{
	PARSE_HEADER_SUCCESS = 1,
	PARSE_HEADER_AGAIN,
	PARSE_HEADER_ERROR
};
enum AnalysisState{
	ANALYSIS_SUCCESS = 1,
	ANALYSIS_ERROR
};

enum ParseState{
	H_START = 0,
	H_KEY,
	H_COLON,
	H_SPACES_AFTER_COLON,
	H_VALUE,
	H_CR,
	H_LF,
	H_END_CR,
	H_END_LF
};
const int DEFAULT_KEEP_ALIVE_TIME = 10 * 60; //s // *1000; // ms
const int DEFAULT_EXPIRED_TIME = 300; //s

class Epoll;
class TimeNode;
class RequestHttp;
//std::enable_shared_from_this能让一个对象安全地生成其他额外的std::shared_ptr实例
class RequestHttp : public std::enable_shared_from_this<RequestHttp>{
private:
	int fd;                          //文件描述符
	HttpMethod method;               //标记当前连接的请求方法
	HttpVersion HTTPversion;         //标记当前连接的HTTP版本号
	int now_read_pos;                //当前读取位置的偏移量
	ConnectionState connectionState; //连接状态
	ProcessState state_process;      //记录处理
	ParseState state_parse;          //记录解析状态
	bool error_;
	bool keep_Alive;

	std::string input_buffer;
	std::string output_buffer;

	std::string path;
	std::string file_name;        //请求行中请求的资源的文件名。

	std::unordered_map<std::string, std::string> headers;  //保存请求头部的头部字段名以及对应的方法
	std::weak_ptr<TimeNode> timer;  //timer定时器
	std::shared_ptr<Epoll> epoll_ptr;
	__uint32_t _events;

	bool HandlerRead;
	bool HandlerWrite;

public:
	RequestHttp(int _fd, std::shared_ptr<Epoll> epoll);
	~RequestHttp();
	void AddTimer(std::shared_ptr<TimeNode> mtimer);
	void reset();
	void seperateTimer();
	int getFd();
	void setFd(int _fd);
	void setEvent(__uint32_t event);
	void HandleEvent();  //内部进行分发
    void setHandlerRead();
    void setHandlerWrite();
    bool getHandlerRead();
    bool getHandlerWrite();

	void handleRead();
	void handleWrite();
	void handleConn();

private:

	void handleError(int fd, int err_num, std::string short_msg);
	URIState ParseURI();
	HeaderState ParseHeaders();
	AnalysisState AnalysisRequest();
};


#endif
