#include "RequestHttp.h"
#include <unordered_map>
#include <fcntl.h>
#include <sys/stat.h>
#include <queue>
#include <cstdlib>
#include <iostream>

#include <opencv2/opencv.hpp>
#include <opencv2/imgcodecs/imgcodecs.hpp>
#include <opencv2/highgui/highgui.hpp>
using namespace cv;

using namespace std;

std::unordered_map<std::string, std::string> mime;
std::once_flag flag;
extern std::shared_ptr<TimeNode> PushTimeNodeToQueue(std::shared_ptr<RequestHttp> request_data, size_t time);

void MimeInit(){
	mime[".html"] = "text/html";
	mime[".avi"] = "video/x-msvideo";
	mime[".bmp"] = "image/bmp";
	mime[".c"] = "text/plain";
	mime[".doc"] = "application/msword";
	mime[".gif"] = "image/gif";
	mime[".gz"] = "application/x-gzip";
	mime[".htm"] = "text/html";
	mime[".ico"] = "image/x-icon";
	mime[".jpg"] = "image/jpeg";
	mime[".png"] = "image/png";
	mime[".txt"] = "text/plain";
	mime[".mp3"] = "audio/mp3";
	mime["default"] = "text/html";
}
std::string getMime(const std::string& suffix){
	std::call_once(flag, MimeInit);
	if (mime.find(suffix) == mime.end())
		return mime["default"];
	else
		return mime[suffix];
}


RequestHttp::RequestHttp(int _fd, std::shared_ptr<Epoll> epoll):
	fd(_fd), epoll_ptr(epoll),
	method(METHOD_GET),  //初始默认设置为GET
	HTTPversion(HTTP_11), //初始默认设置为HTTP1.1
	now_read_pos(0),
	connectionState(H_CONNECTED),
	state_process(STATE_PARSE_URI),
	state_parse(H_START),
	keep_Alive(false),
	HandlerRead(true),
	HandlerWrite(false),
	_events(0),
	error_(false)
{
}
RequestHttp::~RequestHttp(){
    close(fd);    //因文件描述符没有关闭，导致该连接未关闭。对方一直阻塞在read上，无法继续往下进行。
}

void RequestHttp::AddTimer(std::shared_ptr<TimeNode> mtimer){
	timer = mtimer;
}

void RequestHttp::reset(){
	std::weak_ptr<TimeNode> timer;  //timer定时器
	std::shared_ptr<Epoll> epoll_ptr;

	path.clear();
	file_name.clear();
	now_read_pos = 0;
	state_process = STATE_PARSE_URI;
	state_parse = H_START;
	keep_Alive = false;
    HandlerRead = false;
	HandlerWrite = false;
	headers.clear();
	if (timer.lock()){  //lock函数返回weak_ptr的一个shared_ptr
		std::shared_ptr<TimeNode> temp(timer.lock());
		temp->ClearRequestData();
		timer.reset();  //reset函数将weak_ptr的指向置为空。
	}
}
void RequestHttp::seperateTimer(){  //分离定时器
	if (timer.lock()){
		std::shared_ptr<TimeNode> temp(timer.lock());
        temp->ClearRequestData();
		timer.reset();  //reset函数将weak_ptr的指向置为空。
	}
}
int RequestHttp::getFd(){
	return fd;
}
void RequestHttp::setFd(int _fd){
	fd = _fd;
}
void RequestHttp::setEvent(__uint32_t event){
	_events = event;
}
void RequestHttp::HandleEvent(){

}
void RequestHttp::setHandlerRead(){
    HandlerRead = true;
}
void RequestHttp::setHandlerWrite(){
    HandlerWrite = true;
}

bool RequestHttp::getHandlerRead(){
    return HandlerRead;
}
bool RequestHttp::getHandlerWrite(){
    return HandlerWrite;
}

void RequestHttp::handleRead(){
	//__uint32_t events_ = _events;
	do{
		bool flag = false;
		int read_num = readn(fd, input_buffer, flag);
		if (connectionState == H_DISCONNECTING){  //正在关闭链接
			input_buffer.clear();
			break;
		}
		if (read_num < 0){
			perror("read error");
			error_ = true;
			handleError(fd, 400, "Bad Request");
			break;
		}
		else if (read_num == 0 && flag){
			// 有请求出现但读不到数据，按照对端已经关闭处理
			connectionState = H_DISCONNECTING;
			if (read_num == 0){
				break;
			}
		}
		//ofstream log;
		//log.open("Log",ios::app);
		//log << input_buffer << endl;
		//log.close();
		if (state_process == STATE_PARSE_URI){  //解析请求行
			URIState flag = this->ParseURI();
			if (flag == PARSE_URI_AGAIN)  //读到的数据中未解析到完整的行
				break;
			else if (flag == PARSE_URI_ERROR){
				perror("ParseURI error");
				input_buffer.clear();
				error_ = true;
				handleError(fd, 400, "Bad Request");
				cout << "ParseURI Error" << endl;
				break;
			}
			else
				state_process = STATE_PARSE_HEADERS;  //URI解析完成，标记下一步需要进行的解析工作
		}
		if (state_process == STATE_PARSE_HEADERS){
			HeaderState flag = this->ParseHeaders();  //解析请求头部
			if (flag == PARSE_HEADER_AGAIN)
				break;
			else if (flag == PARSE_HEADER_ERROR){
				perror("ParseHeaders error");
				error_ = true;
				handleError(fd, 400, "Bad Request");
				cout << "ParseHeaders Error" << endl;
				break;
			}
			if (method == METHOD_POST){   // POST方法准备
				state_process = STATE_RECV_BODY;
			}
			else{
				state_process = STATE_ANALYSIS;
			}
		}
		if (state_process == STATE_RECV_BODY){
			int content_length = -1;  //正文长度
			if (headers.find("Content-length") != headers.end()){  //headers是一个map
				content_length = stoi(headers["Content-length"]);  //找到了。
			}
			else{  //没有找到
				error_ = true;
				handleError(fd, 400, "Bad Request: Lack of argument (Content-length)");  //错误处理
				break;
			}
			if (static_cast<int>(input_buffer.size()) < content_length)  //读到的数据长度小于给定的正文长度，说明未读完整。
				break;  //跳出，标记继续读。
			state_process = STATE_ANALYSIS;
		}
		if (state_process == STATE_ANALYSIS){
			AnalysisState flag = this->AnalysisRequest();  //解析请求行中的请求方法，并根据解析回送数据
			if (flag == ANALYSIS_SUCCESS){
				state_process = STATE_FINISH;
				break;
			}
			else{
				error_ = true;
				break;
			}
		}
	} while (false);
	if (!error_){   //未设置error标志
		if (output_buffer.size() > 0){  //输出缓冲区有数据。
			handleWrite();
		}
		if (!error_ && state_process == STATE_FINISH){   //未设置error标志且state_process状态标识连接处理已经完成
			this->reset();  //重置
			if (input_buffer.size() > 0){
				if (connectionState != H_DISCONNECTING)  //正在断开
					handleRead();
			}

		}
		else if (!error_ && connectionState != H_DISCONNECTED)  //断开的
			_events |= EPOLLIN;
	}
}

void RequestHttp::handleWrite(){
	if (!error_ ){  //H_DISCONNECTED已经关闭
		if (writen(fd, output_buffer) < 0){
			perror("writen error");
			_events = 0;
			error_ = true;
		}
		else if (output_buffer.size() > 0)
			_events |= EPOLLOUT;
	}
}

void RequestHttp::handleConn(){
    if(!error_){
        if(_events != 0){
            int timeout = DEFAULT_EXPIRED_TIME;
			if (keep_Alive)
				timeout = DEFAULT_KEEP_ALIVE_TIME;
            HandlerRead = false;
            HandlerWrite = false;
            PushTimeNodeToQueue(shared_from_this(), timeout);  /////

			if ((_events & EPOLLIN) && (_events & EPOLLOUT)){
				_events = __uint32_t(0);
				_events |= EPOLLOUT;
			}
			_events |= (EPOLLET | EPOLLONESHOT);
            __uint32_t events_ = _events;
            _events = 0;
            if( epoll_ptr->modify_Event(fd, events_, shared_from_this()) == false){
                perror("Request Handle Conn Events Mod Event Error");
            }
        }
        else if (keep_Alive){
			_events |= (EPOLLIN | EPOLLET | EPOLLONESHOT);
			int timeout = DEFAULT_KEEP_ALIVE_TIME;

            HandlerRead = false;
            HandlerWrite = false;
			PushTimeNodeToQueue(shared_from_this(), timeout);  /////
			__int32_t events_ = _events;
			_events = 0;
			//loop_->updatePoller(channel_, timeout);
			//epoll_ptr->modify_Event(fd, events_, shared_from_this(), timeout); //shared_from_this()返回一个当前类的shared_ptr
			if( epoll_ptr->modify_Event(fd, events_, shared_from_this()) == false){
                perror("Request Handle Conn Keep Alive Mod Event Error");
            }
        }
    }
}

URIState RequestHttp::ParseURI(){   //解析URI
    //cout << "ParseURI" << endl;

	string &str = input_buffer;
	string cop = str;
	//客户端请求消息分为：请求行、请求头部、请求数据
	//其中请求行和请求头部之间以换行符分隔。换行符'\n'　回车符'\r'

	auto pos = str.find('\r', now_read_pos);  //\r代表回车。读到完整的请求行再开始解析请求
	if (pos < 0){
		return PARSE_URI_AGAIN;
	}
	//请求行中：请求方法 URL 协议版本\r\n
	//如；GET /hello.txt HTTP/1.1
	string request_line = str.substr(0, pos);   //截取请求行部分
	if (str.size() > pos + 1)                   //存在请求头部。
		str = str.substr(pos + 1);
	else
		str.clear();
	//更新偏移以及Method标识
	auto posGet = request_line.find("GET");
	auto posPost = request_line.find("POST");
	auto posHead = request_line.find("HEAD");

	if (posGet != request_line.npos){  //找到了
		pos = posGet;                  //更新偏移量
		method = METHOD_GET;
	}
	else if (posPost != request_line.npos){
		pos = posPost;
		method = METHOD_POST;
	}
	else if (posHead != request_line.npos){
		pos = posHead;
		method = METHOD_HEAD;
	}
	else{
		return PARSE_URI_ERROR;
	}

	// 查找URL. 确定请求的文件 filename
	//cout << request_line << endl;
	pos = request_line.find("/", pos);  //从字符串request_line的下标pos开始查找。
	//cout << pos << endl;
	if (pos == request_line.npos){  //没找到
        //cout << "Not find /." << endl;
		file_name = "index.html";
		HTTPversion = HTTP_11;
		return PARSE_URI_SUCCESS;
	}
	else{   //GET后面跟的是一个URL地址，表示通过GET方法获取指定URL的文件。
		size_t _pos = request_line.find(' ', pos);
		if (_pos < 0)
			return PARSE_URI_ERROR;
		else{
			if (_pos - pos > 1){
				file_name = request_line.substr(pos + 1, _pos - pos - 1);  //提取文件名
				auto p = file_name.find('?');
				if (p != file_name.npos){
					file_name = file_name.substr(0, p);
				}
			}
			else{
				file_name = "index.html";
			}
		}
		pos = _pos;
	}
	//#ifndef DEBUG
    //cout << file_name << endl;
    //#endif // DEBUG
	pos = request_line.find("/", pos);   // HTTP版本号
	if (pos < 0)
		return PARSE_URI_ERROR;
	else{
		if (request_line.size() - pos <= 3) //超过正常的请求行长度。
			return PARSE_URI_ERROR;
		else{
			string ver = request_line.substr(pos + 1, 3);  //提取版本号信息
			if (ver == "1.0")
				HTTPversion = HTTP_10;
			else if (ver == "1.1")
				HTTPversion = HTTP_11;
			else
				return PARSE_URI_ERROR;
		}
	}
	return PARSE_URI_SUCCESS;
}

HeaderState RequestHttp::ParseHeaders(){  //解析请求头部
	string &str = input_buffer;
	int key_start = -1, key_end = -1, value_start = -1, value_end = -1;
	//头部字段名范围以及值范围
	//key_start头部字段名起点，key_end头部字段名终点
	int now_read_line_begin = 0;
	bool notFinish = true;
	size_t i = 0;
	//请求头部示例：Keep-Alive：300
	//              Connection：keep - alive
	for (; i < str.size() && notFinish; ++i){
		switch (state_parse){
			case H_START:{   //标识开始
					if (str[i] == '\n' || str[i] == '\r')  //回车或换行符表明当前请求头部的一行已经处理完成。
						break;
					state_parse = H_KEY;
					key_start = i;
					now_read_line_begin = i;
					break;
				}
			case H_KEY:{     //首先解析头部字段名
					if (str[i] == ':'){  //遇到冒号说明头部字段名已经取完
						key_end = i;
						if (key_end - key_start <= 0)
							return PARSE_HEADER_ERROR;
						state_parse = H_COLON;  //解析完头部字段名后下一个是冒号
					}
					else if (str[i] == '\n' || str[i] == '\r')
						return PARSE_HEADER_ERROR;
					break;
				}
			case H_COLON:{  //冒号
					if (str[i] == ' '){
						state_parse = H_SPACES_AFTER_COLON;  //冒号后是空格
					}
					else
						return PARSE_HEADER_ERROR;
					break;
				}
			case H_SPACES_AFTER_COLON:{    //冒号后是空格
					state_parse = H_VALUE;
					value_start = i;
					break;
				}
			case H_VALUE:{
					if (str[i] == '\r'){  //到达回车符，说明值域已经解析完。
						state_parse = H_CR;
						value_end = i;
						if (value_end - value_start <= 0)
							return PARSE_HEADER_ERROR;
					}
					else if (i - value_start > 255)
						return PARSE_HEADER_ERROR;
					break;
				}
			case H_CR:{
					if (str[i] == '\n'){
						state_parse = H_LF;
						string key(str.begin() + key_start, str.begin() + key_end); //提取出头部字段名
						string value(str.begin() + value_start, str.begin() + value_end); //提取出字段值
						headers[key] = value;   //将头部字段名和与之对应的字段值加入到map中
						now_read_line_begin = i;  //更新偏移
					}
					else
						return PARSE_HEADER_ERROR;
					break;
				}
			case H_LF:{
					//因请求头部和请求数据之间增加了一个回车符和换行符。前面的一行已经解析完。
					//下一行如果是新的数据的话应该是正常的值。
					//详细见链接www.runoob.com/http/http-messages.html的请求消息图
					if (str[i] == '\r'){
						state_parse = H_END_CR;
					}
					else{  //这是新的一行
						key_start = i;   //更新头部字段名的起始指针
						state_parse = H_KEY;  //更新解析状态
					}
					break;
				}
			case H_END_CR:{   //请求头部已经解析完成。只剩下请求头部和请求数据之间的回车符和换行符。
					if (str[i] == '\n'){   //回车符在H_LF中已经被解析，在H_END_CR只剩下换行符。
						state_parse = H_END_LF;
					}
					else
						return PARSE_HEADER_ERROR;
					break;
				}
			case H_END_LF:{
					notFinish = false;
					key_start = i;
					now_read_line_begin = i;
					break;
				}
		}
	}
	if (state_parse == H_END_LF){  //正常请求完。标识置为H_END_LF
		str = str.substr(i);    //去掉请求头部，保留请求数据
		return PARSE_HEADER_SUCCESS;
	}
	str = str.substr(now_read_line_begin);
	return PARSE_HEADER_AGAIN;  //再次解析请求头部
}

AnalysisState RequestHttp::AnalysisRequest(){  //分析请求
	if (method == METHOD_POST){

		 string header;
		 header += string("HTTP/1.1 200 OK\r\n");
		 if(headers.find("Connection") != headers.end() && headers["Connection"] == "Keep-Alive"){
		     keep_Alive = true;
		     header += string("Connection: Keep-Alive\r\n") + "Keep-Alive: timeout=" + to_string(DEFAULT_KEEP_ALIVE_TIME) + "\r\n";
		 }
		 int length = stoi(headers["Content-length"]);
		 vector<uchar> data(input_buffer.begin(), input_buffer.begin() + length);
		 Mat src = imdecode(data, CV_LOAD_IMAGE_ANYDEPTH|CV_LOAD_IMAGE_ANYCOLOR);  //将接收到的数据解码为图像格式
		 imwrite("receive.jpg", src);
		 Mat result;
		 threshold(src, result, 80, 255, CV_THRESH_BINARY);  //二值化
		 vector<unsigned char> data_encode;
		 imencode(".jpg", result, data_encode);
		 header += string("Content-length: ") + to_string(data_encode.size()) + "\r\n\r\n";
		 output_buffer += header + string(data_encode.begin(), data_encode.end());
		 input_buffer = input_buffer.substr(length);
		 return ANALYSIS_SUCCESS;

	}
	else if (method == METHOD_GET || method == METHOD_HEAD){
        //cout << "Analysis" << endl;
		//HTTP响应由四部分组成：状态行、消息报头、空行和响应正文。
		string header;
		header += "HTTP/1.1 200 OK\r\n";  //状态行
		if (headers.find("Connection") != headers.end() && (headers["Connection"] == "Keep-Alive" || headers["Connection"] == "keep-alive") ){
			//解析连接状态是否开启了KeepAlive.
			keep_Alive = true;
			header += string("Connection: Keep-Alive\r\n") + "Keep-Alive: timeout=" + to_string(DEFAULT_KEEP_ALIVE_TIME) + "\r\n";
		}
		auto dot_pos = file_name.find('.');  //反向查找文件名中是否有点。查找最后一个点
		string filetype;  //文件类型。
		if (dot_pos != file_name.npos)
			filetype = getMime(file_name.substr(dot_pos));  //查找文件名的后缀
		else
			filetype = getMime("default");
		//cout << filetype << endl;
        //cout << file_name << endl;
		if (file_name == "hello"){  //回显测试
			output_buffer = "HTTP/1.1 200 OK\r\nContent-type: text/plain\r\n\r\nHello World Lchen";
			//int len = output_buffer.size();
			//cout << output_buffer << endl;
			//size_t send_len = writen(this->fd, output_buffer);
            //if(send_len != len){
            //    perror("Send hello failed:");
            //}
			return ANALYSIS_SUCCESS;
		}


		struct stat sb;
		string fname = "./"+file_name;
		if (stat(file_name.c_str(), &sb) < 0){   //文件名对应的文件不存在
		    //cout << "File stat error" << endl;
			header.clear();
			handleError(fd, 404, "Not Found!");
			return ANALYSIS_ERROR;
		}
		header += "Content-Type: " + filetype + "\r\n";                 //正文类型
		header += "Content-Length: " + to_string(sb.st_size) + "\r\n";  //回送相应的正文长度
		header += "Server: Lchen Web Server Test\r\n";

		header += "\r\n";
		output_buffer += header;

		if (method == METHOD_HEAD)
			return ANALYSIS_SUCCESS;
        //size_t send_len = writen(fd, output_buffer);
		//if(send_len != output_buffer.size()){
        //    perror("Send failed 1");
		//}

        //cout << fname << " " << sb.st_size <<endl;
		//响应正文
		int return_fd = open(fname.c_str(), O_RDONLY, 0);
		if (return_fd < 0){
            cout << "Open Error:" << endl;
			output_buffer.clear();
			handleError(fd, 404, "Not Found!");
			return ANALYSIS_ERROR;
		}
		//私有映射
		char* mmapReturn_ptr = static_cast<char*>(mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, return_fd, 0)); //将该文件mmap映射到内存
		close(return_fd);  //mmap后可以关掉文件描述符
		if (mmapReturn_ptr == nullptr){
		    //cout << "Mmap error" << endl;
			munmap(mmapReturn_ptr, sb.st_size);
			output_buffer.clear();
			handleError(fd, 404, "Not Found!");
			return ANALYSIS_ERROR;
		}
		output_buffer += string(mmapReturn_ptr, mmapReturn_ptr + sb.st_size);
		//send_len = writen(fd, mmapReturn_ptr, sb.st_size);
		//if(send_len != sb.st_size){
        //    perror("Send failed 2");
		//}
		//cout << "Output_buffer:" << output_buffer.size() << endl;
		munmap(mmapReturn_ptr, sb.st_size);

		return ANALYSIS_SUCCESS;
	}
	return ANALYSIS_ERROR;
}

void RequestHttp::handleError(int fd, int err_num, string short_msg){
	short_msg = " " + short_msg;
	char send_buff[4096];
	string body_buff, header_buff;
	body_buff += "<html><title>该页面不存在</title>";
	body_buff += "<body bgcolor=\"ffffff\">";
	body_buff += to_string(err_num) + short_msg;
	body_buff += "<hr><em> Lchen Web Server Test</em>\n</body></html>";
	header_buff += "HTTP/1.1 " + to_string(err_num) + short_msg + "\r\n";
	header_buff += "Content-Type: text/html\r\n";
	header_buff += "Connection: Close\r\n";
	header_buff += "Content-Length: " + to_string(body_buff.size()) + "\r\n";
	header_buff += "Server: LinYa's Web Server\r\n";;
	header_buff += "\r\n";
	// 错误处理不考虑writen不完的情况
	writen(fd, header_buff);
	writen(fd, body_buff);
}
