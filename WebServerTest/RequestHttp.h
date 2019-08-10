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

enum ConnectionState{   //ö������
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
//std::enable_shared_from_this����һ������ȫ���������������std::shared_ptrʵ��
class RequestHttp : public std::enable_shared_from_this<RequestHttp>{
private:
	int fd;                          //�ļ�������
	HttpMethod method;               //��ǵ�ǰ���ӵ����󷽷�
	HttpVersion HTTPversion;         //��ǵ�ǰ���ӵ�HTTP�汾��
	int now_read_pos;                //��ǰ��ȡλ�õ�ƫ����
	ConnectionState connectionState; //����״̬
	ProcessState state_process;      //��¼����
	ParseState state_parse;          //��¼����״̬
	bool error_;
	bool keep_Alive;

	std::string input_buffer;
	std::string output_buffer;

	std::string path;
	std::string file_name;        //���������������Դ���ļ�����

	std::unordered_map<std::string, std::string> headers;  //��������ͷ����ͷ���ֶ����Լ���Ӧ�ķ���
	std::weak_ptr<TimeNode> timer;  //timer��ʱ��
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
	void HandleEvent();  //�ڲ����зַ�
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
