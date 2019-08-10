#include "Epoll.h"

/*
epoll_event *Epoll::events;
std::unordered_map<int, std::shared_ptr<RequestHttp>> Epoll::fd_to_request;
int Epoll::epollfd = 0;
*/


typedef std::shared_ptr<TimeNode> Node;
struct TimerCmpare{   //priority_queue的比较函数
    bool operator()(Node& p, Node& q) const{
        return p->GetExpireTime() > q->GetExpireTime();
    }
};

std::priority_queue<Node, std::deque<Node>, TimerCmpare> TimerNodeQueue;
std::mutex time_mutex;
std::shared_ptr<TimeNode> PushTimeNodeToQueue(std::shared_ptr<RequestHttp> request_data, size_t time){
    std::shared_ptr<TimeNode> temp(new TimeNode(request_data, time));
    std::unique_lock<mutex> lck(time_mutex);
    TimerNodeQueue.push(temp); //
    return temp;
}

void Handle_Expired_Event(){
    while(!TimerNodeQueue.empty()){
        std::shared_ptr<TimeNode> temp = TimerNodeQueue.top();
        if(temp->isDeleted()){  //检查是否已经被标记为删除
            //标记为能够被删除
            TimerNodeQueue.pop();
        }
        else if(temp->isValid() == false){  //检查是否过期
            TimerNodeQueue.pop();
        }
        else
            break;
    }
}

void Epoll::initEpoll(int maxEvents) {
    epollfd = epoll_create(maxEvents);  //若不设的话有默认值65535
	if (epollfd < 0) {
		perror("epoll create error:");
		exit(-1);
	}
	events = new struct epoll_event[maxEvents];
}

int Epoll::initServer(std::string hostName, int port, int socketType) {
	int listen_sock = socketBind(hostName, port, socketType);
	listen_sockfd = listen_sock;
	setNonBlocking(listen_sock);

	std::shared_ptr<RequestHttp> request(new RequestHttp(listen_sock, shared_from_this() ));
	if (!add_Event(listen_sock, EPOLLIN | EPOLLET, request)) {
        perror("addEvent fail error:");
		exit(-1);
	}
	return listen_sock;
}

int Epoll::socketBind(std::string hostName, int port, int socketType) {
    //创建socket
    //绑定bing
    //开启监听listen
	int listenfd;
	struct sockaddr_in servaddr;
	listenfd = socket(AF_INET, socketType, 0);
	if (listenfd < -1) {
        perror("socket error:");
		exit(-1);
	}
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;  //IPv4
	std::string ip = getIpByHost(hostName, port);
	inet_pton(AF_INET, ip.c_str(), &servaddr.sin_addr);
	//servaddr.sin_addr.s_addr = inet_addr(hostName.c_str());
	servaddr.sin_port = htons(port);  //返回网络字节序的值
	if (bind(listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr)) == -1) {
        perror("bind error:");
		exit(-1);
	}
	//cout << servaddr.sin_addr.s_addr << " " << servaddr.sin_port << " " << port << endl;
	listen(listenfd, 4096);   //第二个参数是监听队列的长度
	return listenfd;
}

void Epoll::setNonBlocking(int fd) {
	int flags, s;
	flags = fcntl(fd, F_GETFL);
	if (flags < 0) {
        perror("fcntl F_GETFL error:");
		exit(-1);
	}

	flags |= O_NONBLOCK;
	s = fcntl(fd, F_SETFL, flags);  //设置文件打开方式为flag指定方式
	if (s < 0) {
        perror("fcntl F_SETFL error:");
		exit(-1);
	}
}

void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET)
		return &(((struct sockaddr_in*) sa)->sin_addr);
	return &(((struct sockaddr_in6*) sa)->sin6_addr);
}
std::string Epoll::getIpByHost(std::string hostName, int port) {
	struct addrinfo hints;
	struct addrinfo *result;

	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
    string p = to_string(port);
	int rt = getaddrinfo(hostName.c_str(), p.c_str(), &hints, &result);
	if (rt != 0) {
		perror("getaddrinfo error 1:");
        exit(-1);
	}
	if (result == NULL) {
        perror("getaddrinfo error 2:");
        exit(-1);
	}

	char ip[INET6_ADDRSTRLEN];
	inet_ntop(result->ai_family, get_in_addr(result->ai_addr), ip, sizeof(ip));
	freeaddrinfo(result);

	return ip;
}

void Epoll::doEpoll(std::shared_ptr<ThreadPool> thread_pool, int listen_fd, int maxEvents, int timeout){
    //struct epoll_event* events = (epoll_event*)calloc(maxEvents, sizeof(struct epoll_event));
    //cout << "Waiting..." << endl;
	while (true) {
        //cout << "Waiting..." << endl;
		int nfds = epoll_wait(epollfd, events, maxEvents, timeout);
		if (nfds < 0) {
            perror("epoll_wait error:");
            delete[] events;
            exit(-1);
		}
		std::vector<std::shared_ptr<RequestHttp> > request = getEventsRequest(listen_fd, nfds);
		//cout << nfds << endl;
		if(request.size() > 0){
            for(auto& p : request){
                //cout << p->getFd() << endl;
                thread_pool->enqueue(MyHandler, p);
            }
		}
		Handle_Expired_Event();
		//cout << "=============================" << nfds << "=================================" << endl;
	}
	delete[] events;
}
//测试
void Epoll::doEpoll(int n, int maxEvents, int timed_out, std::shared_ptr<ThreadPool> thread_pool){

	while (true) {
        int nfds = epoll_wait(epollfd, events, maxEvents, timed_out);
		if (nfds < 0) {
            perror("epoll_wait error:");
            free(events);
            exit(-1);
		}
		std::vector<std::shared_ptr<RequestHttp> > request = getEventsRequest(n, nfds);
		if(request.size() > 0){
            for(auto& p:request){
                thread_pool->enqueue(MyHandler_Test, p);
            }
		}
	}
	free(events);
}

bool Epoll::add_Event(int fd, __uint32_t state, std::shared_ptr<RequestHttp> request) {
	struct epoll_event event;
	event.data.fd = fd;
	event.events = state;
	int rt = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	if(rt < 0){
        perror("epoll add error");
        return false;
	}
	fd_to_request[fd] = request;
	return true;
}

bool Epoll::remove_Event(int fd, __uint32_t state) {
	struct epoll_event event;
	event.data.fd = fd;
	event.events = state;
	int rt = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &event);
	if(rt < 0){
        perror("epoll del error");
        return false;
	}
	auto p = fd_to_request.find(fd);    //map成员函数
	if(p != fd_to_request.end()){       //找到了
        fd_to_request.erase(p);         //移除迭代器指定的元素
	}
	return true;
}

bool Epoll::modify_Event(int fd, __uint32_t state, std::shared_ptr<RequestHttp> request) {
	struct epoll_event event;
	event.data.fd = fd;
	event.events = state;
	int rt = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
	if(rt < 0){
        perror("epoll mod error");
        return false;
	}
	fd_to_request[fd] = request;
	return true;
}

void Epoll::AcceptConnection(int listen_fd, int epollfd){
    struct sockaddr_in client_addr;
    memset(&client_addr, 0, sizeof(struct sockaddr_in));
    /***********************************************
    此处有两点调试了好久。特此记录！！！
    1.socklen_t client_addr_len = sizeof(client_addr);  赋值大小。原来赋值为0或1出现accept到全0地址，无端口号的情况。
    2.while((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0){} 此处将accept放入到while
    循环里，结果能够正常accept到所有的连接。
    之前是直接使用if((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0){}则出现了accept到的
    连接数目和请求连接数目不匹配问题。
    ***********************************************/
    socklen_t client_addr_len = sizeof(client_addr);
    int accept_fd = 0;
    while((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0){
        char* str = inet_ntoa(client_addr.sin_addr);
        ntohs(client_addr.sin_port);
        //cout << accept_fd << " " << str << " " << ntohs(client_addr.sin_port) << endl;
        if (accept_fd >= MaxEvents){  //限制最大并发连接数
            close(accept_fd);
            return;
        }
        setNonBlocking(accept_fd);   //将改描述符设为非阻塞模式

        std::shared_ptr<RequestHttp> current_request(new RequestHttp(accept_fd, shared_from_this() ));  //创建与该文件描述符对应的RequestHttp对象
        //设置文件描述符可读，边缘触发(Edge Triggered)模式，保证一个socket连接在任一时刻只被一个线程处理
        __uint32_t accept_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
        //EPOLLET读取数据后，再次调用epoll_wait已经不是就绪状态了。
        add_Event(accept_fd, accept_event, current_request);
        // 新增时间信息
        //PushTimeNodeToQueue(current_request, TIME_OUT);
        current_request->AddTimer( PushTimeNodeToQueue(current_request, TIME_OUT) );  //将指向该时间对象的指针加入到对应的处理对象。

    }
    //if(accept_fd < 0){
    //    perror("Accept error");
    //    return;
    //}
    //timer_manager.addTimer(req_info, TIMER_TIME_OUT);
    //PushTimeNodeToQueue(current_request, TIME_OUT);
    //std::shared_ptr<TimeNode> timer(new TimeNode(current_request, TIME_OUT));
    //{
    //   mutex mu;
    //  lock_guard<mutex> lck(mu);   //加锁是因为要防止
    //   MyTimeQueue.AddTime(timer,); //时间队列。根据过期时间排序
    //}
}

std::vector<std::shared_ptr<RequestHttp>> Epoll::getEventsRequest(int listen_fd, int events_num){
    std::vector<std::shared_ptr<RequestHttp> > request_data;
    for(int i = 0; i < events_num; ++i){
        int fd = events[i].data.fd;
        // 有事件发生的描述符为监听描述符
        if(fd == listen_fd){   //有新的连接
            AcceptConnection(listen_fd, epollfd);
        }
        else if (fd < 3){  //012号文件描述符分别为标准输入、标准输出和标准错误
            std::cout << "fd < 3" << std::endl;;
            break;
        }
        else{
            //std::cout << "Has Added" << std::endl;
                //之前已经添加的文件描述符
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)){ //给一个已经关闭的socket写时会发生EPOLLERR
                std::cout << "error event\n" << std::endl;
                auto p = fd_to_request.find(fd);
                if(p != fd_to_request.end()){  //找到了，不为空
                    fd_to_request[fd]->seperateTimer();  //将RequestHttp中指向定时器的指针置为空
                }
                fd_to_request.erase(p);
                continue;
            }
            // 将请求任务加入到线程池中
            // 加入线程池之前将Timer和request分离
            std::shared_ptr<RequestHttp> current_request = fd_to_request[fd];
            //根据文件描述符找到相应的RequestHttp对象指针

            if (current_request){
                if ((events[i].events & EPOLLIN) || (events[i].events & EPOLLPRI)){            //已连接用户接受到数据
                    //std::cout << "Event IN" << std::endl;
                    current_request->setHandlerRead();      //将可读标志位置位
                }
                if (events[i].events & EPOLLOUT){           //已连接用户有数据要发送
                    //std::cout << "Event OUT" << std::endl;
                    current_request->setHandlerRead();      //将可写标志位置位
                }
                current_request->seperateTimer();     //将对应的RequestHttp对象计时器置为空
                request_data.push_back(current_request);
                fd_to_request[fd].reset();
            }
        }
    }
    return request_data;
}

