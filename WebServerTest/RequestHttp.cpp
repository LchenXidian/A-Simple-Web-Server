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
	method(METHOD_GET),  //��ʼĬ������ΪGET
	HTTPversion(HTTP_11), //��ʼĬ������ΪHTTP1.1
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
    close(fd);    //���ļ�������û�йرգ����¸�����δ�رա��Է�һֱ������read�ϣ��޷��������½��С�
}

void RequestHttp::AddTimer(std::shared_ptr<TimeNode> mtimer){
	timer = mtimer;
}

void RequestHttp::reset(){
	std::weak_ptr<TimeNode> timer;  //timer��ʱ��
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
	if (timer.lock()){  //lock��������weak_ptr��һ��shared_ptr
		std::shared_ptr<TimeNode> temp(timer.lock());
		temp->ClearRequestData();
		timer.reset();  //reset������weak_ptr��ָ����Ϊ�ա�
	}
}
void RequestHttp::seperateTimer(){  //���붨ʱ��
	if (timer.lock()){
		std::shared_ptr<TimeNode> temp(timer.lock());
        temp->ClearRequestData();
		timer.reset();  //reset������weak_ptr��ָ����Ϊ�ա�
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
		if (connectionState == H_DISCONNECTING){  //���ڹر�����
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
			// ��������ֵ����������ݣ����նԶ��Ѿ��رմ���
			connectionState = H_DISCONNECTING;
			if (read_num == 0){
				break;
			}
		}
		//ofstream log;
		//log.open("Log",ios::app);
		//log << input_buffer << endl;
		//log.close();
		if (state_process == STATE_PARSE_URI){  //����������
			URIState flag = this->ParseURI();
			if (flag == PARSE_URI_AGAIN)  //������������δ��������������
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
				state_process = STATE_PARSE_HEADERS;  //URI������ɣ������һ����Ҫ���еĽ�������
		}
		if (state_process == STATE_PARSE_HEADERS){
			HeaderState flag = this->ParseHeaders();  //��������ͷ��
			if (flag == PARSE_HEADER_AGAIN)
				break;
			else if (flag == PARSE_HEADER_ERROR){
				perror("ParseHeaders error");
				error_ = true;
				handleError(fd, 400, "Bad Request");
				cout << "ParseHeaders Error" << endl;
				break;
			}
			if (method == METHOD_POST){   // POST����׼��
				state_process = STATE_RECV_BODY;
			}
			else{
				state_process = STATE_ANALYSIS;
			}
		}
		if (state_process == STATE_RECV_BODY){
			int content_length = -1;  //���ĳ���
			if (headers.find("Content-length") != headers.end()){  //headers��һ��map
				content_length = stoi(headers["Content-length"]);  //�ҵ��ˡ�
			}
			else{  //û���ҵ�
				error_ = true;
				handleError(fd, 400, "Bad Request: Lack of argument (Content-length)");  //������
				break;
			}
			if (static_cast<int>(input_buffer.size()) < content_length)  //���������ݳ���С�ڸ��������ĳ��ȣ�˵��δ��������
				break;  //��������Ǽ�������
			state_process = STATE_ANALYSIS;
		}
		if (state_process == STATE_ANALYSIS){
			AnalysisState flag = this->AnalysisRequest();  //�����������е����󷽷��������ݽ�����������
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
	if (!error_){   //δ����error��־
		if (output_buffer.size() > 0){  //��������������ݡ�
			handleWrite();
		}
		if (!error_ && state_process == STATE_FINISH){   //δ����error��־��state_process״̬��ʶ���Ӵ����Ѿ����
			this->reset();  //����
			if (input_buffer.size() > 0){
				if (connectionState != H_DISCONNECTING)  //���ڶϿ�
					handleRead();
			}

		}
		else if (!error_ && connectionState != H_DISCONNECTED)  //�Ͽ���
			_events |= EPOLLIN;
	}
}

void RequestHttp::handleWrite(){
	if (!error_ ){  //H_DISCONNECTED�Ѿ��ر�
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
			//epoll_ptr->modify_Event(fd, events_, shared_from_this(), timeout); //shared_from_this()����һ����ǰ���shared_ptr
			if( epoll_ptr->modify_Event(fd, events_, shared_from_this()) == false){
                perror("Request Handle Conn Keep Alive Mod Event Error");
            }
        }
    }
}

URIState RequestHttp::ParseURI(){   //����URI
    //cout << "ParseURI" << endl;

	string &str = input_buffer;
	string cop = str;
	//�ͻ���������Ϣ��Ϊ�������С�����ͷ������������
	//���������к�����ͷ��֮���Ի��з��ָ������з�'\n'���س���'\r'

	auto pos = str.find('\r', now_read_pos);  //\r����س��������������������ٿ�ʼ��������
	if (pos < 0){
		return PARSE_URI_AGAIN;
	}
	//�������У����󷽷� URL Э��汾\r\n
	//�磻GET /hello.txt HTTP/1.1
	string request_line = str.substr(0, pos);   //��ȡ�����в���
	if (str.size() > pos + 1)                   //��������ͷ����
		str = str.substr(pos + 1);
	else
		str.clear();
	//����ƫ���Լ�Method��ʶ
	auto posGet = request_line.find("GET");
	auto posPost = request_line.find("POST");
	auto posHead = request_line.find("HEAD");

	if (posGet != request_line.npos){  //�ҵ���
		pos = posGet;                  //����ƫ����
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

	// ����URL. ȷ��������ļ� filename
	//cout << request_line << endl;
	pos = request_line.find("/", pos);  //���ַ���request_line���±�pos��ʼ���ҡ�
	//cout << pos << endl;
	if (pos == request_line.npos){  //û�ҵ�
        //cout << "Not find /." << endl;
		file_name = "index.html";
		HTTPversion = HTTP_11;
		return PARSE_URI_SUCCESS;
	}
	else{   //GET���������һ��URL��ַ����ʾͨ��GET������ȡָ��URL���ļ���
		size_t _pos = request_line.find(' ', pos);
		if (_pos < 0)
			return PARSE_URI_ERROR;
		else{
			if (_pos - pos > 1){
				file_name = request_line.substr(pos + 1, _pos - pos - 1);  //��ȡ�ļ���
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
	pos = request_line.find("/", pos);   // HTTP�汾��
	if (pos < 0)
		return PARSE_URI_ERROR;
	else{
		if (request_line.size() - pos <= 3) //���������������г��ȡ�
			return PARSE_URI_ERROR;
		else{
			string ver = request_line.substr(pos + 1, 3);  //��ȡ�汾����Ϣ
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

HeaderState RequestHttp::ParseHeaders(){  //��������ͷ��
	string &str = input_buffer;
	int key_start = -1, key_end = -1, value_start = -1, value_end = -1;
	//ͷ���ֶ�����Χ�Լ�ֵ��Χ
	//key_startͷ���ֶ�����㣬key_endͷ���ֶ����յ�
	int now_read_line_begin = 0;
	bool notFinish = true;
	size_t i = 0;
	//����ͷ��ʾ����Keep-Alive��300
	//              Connection��keep - alive
	for (; i < str.size() && notFinish; ++i){
		switch (state_parse){
			case H_START:{   //��ʶ��ʼ
					if (str[i] == '\n' || str[i] == '\r')  //�س����з�������ǰ����ͷ����һ���Ѿ�������ɡ�
						break;
					state_parse = H_KEY;
					key_start = i;
					now_read_line_begin = i;
					break;
				}
			case H_KEY:{     //���Ƚ���ͷ���ֶ���
					if (str[i] == ':'){  //����ð��˵��ͷ���ֶ����Ѿ�ȡ��
						key_end = i;
						if (key_end - key_start <= 0)
							return PARSE_HEADER_ERROR;
						state_parse = H_COLON;  //������ͷ���ֶ�������һ����ð��
					}
					else if (str[i] == '\n' || str[i] == '\r')
						return PARSE_HEADER_ERROR;
					break;
				}
			case H_COLON:{  //ð��
					if (str[i] == ' '){
						state_parse = H_SPACES_AFTER_COLON;  //ð�ź��ǿո�
					}
					else
						return PARSE_HEADER_ERROR;
					break;
				}
			case H_SPACES_AFTER_COLON:{    //ð�ź��ǿո�
					state_parse = H_VALUE;
					value_start = i;
					break;
				}
			case H_VALUE:{
					if (str[i] == '\r'){  //����س�����˵��ֵ���Ѿ������ꡣ
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
						string key(str.begin() + key_start, str.begin() + key_end); //��ȡ��ͷ���ֶ���
						string value(str.begin() + value_start, str.begin() + value_end); //��ȡ���ֶ�ֵ
						headers[key] = value;   //��ͷ���ֶ�������֮��Ӧ���ֶ�ֵ���뵽map��
						now_read_line_begin = i;  //����ƫ��
					}
					else
						return PARSE_HEADER_ERROR;
					break;
				}
			case H_LF:{
					//������ͷ������������֮��������һ���س����ͻ��з���ǰ���һ���Ѿ������ꡣ
					//��һ��������µ����ݵĻ�Ӧ����������ֵ��
					//��ϸ������www.runoob.com/http/http-messages.html��������Ϣͼ
					if (str[i] == '\r'){
						state_parse = H_END_CR;
					}
					else{  //�����µ�һ��
						key_start = i;   //����ͷ���ֶ�������ʼָ��
						state_parse = H_KEY;  //���½���״̬
					}
					break;
				}
			case H_END_CR:{   //����ͷ���Ѿ�������ɡ�ֻʣ������ͷ������������֮��Ļس����ͻ��з���
					if (str[i] == '\n'){   //�س�����H_LF���Ѿ�����������H_END_CRֻʣ�»��з���
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
	if (state_parse == H_END_LF){  //���������ꡣ��ʶ��ΪH_END_LF
		str = str.substr(i);    //ȥ������ͷ����������������
		return PARSE_HEADER_SUCCESS;
	}
	str = str.substr(now_read_line_begin);
	return PARSE_HEADER_AGAIN;  //�ٴν�������ͷ��
}

AnalysisState RequestHttp::AnalysisRequest(){  //��������
	if (method == METHOD_POST){

		 string header;
		 header += string("HTTP/1.1 200 OK\r\n");
		 if(headers.find("Connection") != headers.end() && headers["Connection"] == "Keep-Alive"){
		     keep_Alive = true;
		     header += string("Connection: Keep-Alive\r\n") + "Keep-Alive: timeout=" + to_string(DEFAULT_KEEP_ALIVE_TIME) + "\r\n";
		 }
		 int length = stoi(headers["Content-length"]);
		 vector<uchar> data(input_buffer.begin(), input_buffer.begin() + length);
		 Mat src = imdecode(data, CV_LOAD_IMAGE_ANYDEPTH|CV_LOAD_IMAGE_ANYCOLOR);  //�����յ������ݽ���Ϊͼ���ʽ
		 imwrite("receive.jpg", src);
		 Mat result;
		 threshold(src, result, 80, 255, CV_THRESH_BINARY);  //��ֵ��
		 vector<unsigned char> data_encode;
		 imencode(".jpg", result, data_encode);
		 header += string("Content-length: ") + to_string(data_encode.size()) + "\r\n\r\n";
		 output_buffer += header + string(data_encode.begin(), data_encode.end());
		 input_buffer = input_buffer.substr(length);
		 return ANALYSIS_SUCCESS;

	}
	else if (method == METHOD_GET || method == METHOD_HEAD){
        //cout << "Analysis" << endl;
		//HTTP��Ӧ���Ĳ�����ɣ�״̬�С���Ϣ��ͷ�����к���Ӧ���ġ�
		string header;
		header += "HTTP/1.1 200 OK\r\n";  //״̬��
		if (headers.find("Connection") != headers.end() && (headers["Connection"] == "Keep-Alive" || headers["Connection"] == "keep-alive") ){
			//��������״̬�Ƿ�����KeepAlive.
			keep_Alive = true;
			header += string("Connection: Keep-Alive\r\n") + "Keep-Alive: timeout=" + to_string(DEFAULT_KEEP_ALIVE_TIME) + "\r\n";
		}
		auto dot_pos = file_name.find('.');  //��������ļ������Ƿ��е㡣�������һ����
		string filetype;  //�ļ����͡�
		if (dot_pos != file_name.npos)
			filetype = getMime(file_name.substr(dot_pos));  //�����ļ����ĺ�׺
		else
			filetype = getMime("default");
		//cout << filetype << endl;
        //cout << file_name << endl;
		if (file_name == "hello"){  //���Բ���
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
		if (stat(file_name.c_str(), &sb) < 0){   //�ļ�����Ӧ���ļ�������
		    //cout << "File stat error" << endl;
			header.clear();
			handleError(fd, 404, "Not Found!");
			return ANALYSIS_ERROR;
		}
		header += "Content-Type: " + filetype + "\r\n";                 //��������
		header += "Content-Length: " + to_string(sb.st_size) + "\r\n";  //������Ӧ�����ĳ���
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
		//��Ӧ����
		int return_fd = open(fname.c_str(), O_RDONLY, 0);
		if (return_fd < 0){
            cout << "Open Error:" << endl;
			output_buffer.clear();
			handleError(fd, 404, "Not Found!");
			return ANALYSIS_ERROR;
		}
		//˽��ӳ��
		char* mmapReturn_ptr = static_cast<char*>(mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, return_fd, 0)); //�����ļ�mmapӳ�䵽�ڴ�
		close(return_fd);  //mmap����Թص��ļ�������
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
	body_buff += "<html><title>��ҳ�治����</title>";
	body_buff += "<body bgcolor=\"ffffff\">";
	body_buff += to_string(err_num) + short_msg;
	body_buff += "<hr><em> Lchen Web Server Test</em>\n</body></html>";
	header_buff += "HTTP/1.1 " + to_string(err_num) + short_msg + "\r\n";
	header_buff += "Content-Type: text/html\r\n";
	header_buff += "Connection: Close\r\n";
	header_buff += "Content-Length: " + to_string(body_buff.size()) + "\r\n";
	header_buff += "Server: LinYa's Web Server\r\n";;
	header_buff += "\r\n";
	// ����������writen��������
	writen(fd, header_buff);
	writen(fd, body_buff);
}
