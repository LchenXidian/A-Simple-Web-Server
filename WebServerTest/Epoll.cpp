#include "Epoll.h"

/*
epoll_event *Epoll::events;
std::unordered_map<int, std::shared_ptr<RequestHttp>> Epoll::fd_to_request;
int Epoll::epollfd = 0;
*/


typedef std::shared_ptr<TimeNode> Node;
struct TimerCmpare{   //priority_queue�ıȽϺ���
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
        if(temp->isDeleted()){  //����Ƿ��Ѿ������Ϊɾ��
            //���Ϊ�ܹ���ɾ��
            TimerNodeQueue.pop();
        }
        else if(temp->isValid() == false){  //����Ƿ����
            TimerNodeQueue.pop();
        }
        else
            break;
    }
}

void Epoll::initEpoll(int maxEvents) {
    epollfd = epoll_create(maxEvents);  //������Ļ���Ĭ��ֵ65535
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
    //����socket
    //��bing
    //��������listen
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
	servaddr.sin_port = htons(port);  //���������ֽ����ֵ
	if (bind(listenfd, (struct sockaddr*) &servaddr, sizeof(servaddr)) == -1) {
        perror("bind error:");
		exit(-1);
	}
	//cout << servaddr.sin_addr.s_addr << " " << servaddr.sin_port << " " << port << endl;
	listen(listenfd, 4096);   //�ڶ��������Ǽ������еĳ���
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
	s = fcntl(fd, F_SETFL, flags);  //�����ļ��򿪷�ʽΪflagָ����ʽ
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
//����
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
	auto p = fd_to_request.find(fd);    //map��Ա����
	if(p != fd_to_request.end()){       //�ҵ���
        fd_to_request.erase(p);         //�Ƴ�������ָ����Ԫ��
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
    �˴�����������˺þá��ش˼�¼������
    1.socklen_t client_addr_len = sizeof(client_addr);  ��ֵ��С��ԭ����ֵΪ0��1����accept��ȫ0��ַ���޶˿ںŵ������
    2.while((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0){} �˴���accept���뵽while
    ѭ�������ܹ�����accept�����е����ӡ�
    ֮ǰ��ֱ��ʹ��if((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0){}�������accept����
    ������Ŀ������������Ŀ��ƥ�����⡣
    ***********************************************/
    socklen_t client_addr_len = sizeof(client_addr);
    int accept_fd = 0;
    while((accept_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_addr_len)) > 0){
        char* str = inet_ntoa(client_addr.sin_addr);
        ntohs(client_addr.sin_port);
        //cout << accept_fd << " " << str << " " << ntohs(client_addr.sin_port) << endl;
        if (accept_fd >= MaxEvents){  //������󲢷�������
            close(accept_fd);
            return;
        }
        setNonBlocking(accept_fd);   //������������Ϊ������ģʽ

        std::shared_ptr<RequestHttp> current_request(new RequestHttp(accept_fd, shared_from_this() ));  //��������ļ���������Ӧ��RequestHttp����
        //�����ļ��������ɶ�����Ե����(Edge Triggered)ģʽ����֤һ��socket��������һʱ��ֻ��һ���̴߳���
        __uint32_t accept_event = EPOLLIN | EPOLLET | EPOLLONESHOT;
        //EPOLLET��ȡ���ݺ��ٴε���epoll_wait�Ѿ����Ǿ���״̬�ˡ�
        add_Event(accept_fd, accept_event, current_request);
        // ����ʱ����Ϣ
        //PushTimeNodeToQueue(current_request, TIME_OUT);
        current_request->AddTimer( PushTimeNodeToQueue(current_request, TIME_OUT) );  //��ָ���ʱ������ָ����뵽��Ӧ�Ĵ������

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
    //  lock_guard<mutex> lck(mu);   //��������ΪҪ��ֹ
    //   MyTimeQueue.AddTime(timer,); //ʱ����С����ݹ���ʱ������
    //}
}

std::vector<std::shared_ptr<RequestHttp>> Epoll::getEventsRequest(int listen_fd, int events_num){
    std::vector<std::shared_ptr<RequestHttp> > request_data;
    for(int i = 0; i < events_num; ++i){
        int fd = events[i].data.fd;
        // ���¼�������������Ϊ����������
        if(fd == listen_fd){   //���µ�����
            AcceptConnection(listen_fd, epollfd);
        }
        else if (fd < 3){  //012���ļ��������ֱ�Ϊ��׼���롢��׼����ͱ�׼����
            std::cout << "fd < 3" << std::endl;;
            break;
        }
        else{
            //std::cout << "Has Added" << std::endl;
                //֮ǰ�Ѿ���ӵ��ļ�������
            if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP)){ //��һ���Ѿ��رյ�socketдʱ�ᷢ��EPOLLERR
                std::cout << "error event\n" << std::endl;
                auto p = fd_to_request.find(fd);
                if(p != fd_to_request.end()){  //�ҵ��ˣ���Ϊ��
                    fd_to_request[fd]->seperateTimer();  //��RequestHttp��ָ��ʱ����ָ����Ϊ��
                }
                fd_to_request.erase(p);
                continue;
            }
            // ������������뵽�̳߳���
            // �����̳߳�֮ǰ��Timer��request����
            std::shared_ptr<RequestHttp> current_request = fd_to_request[fd];
            //�����ļ��������ҵ���Ӧ��RequestHttp����ָ��

            if (current_request){
                if ((events[i].events & EPOLLIN) || (events[i].events & EPOLLPRI)){            //�������û����ܵ�����
                    //std::cout << "Event IN" << std::endl;
                    current_request->setHandlerRead();      //���ɶ���־λ��λ
                }
                if (events[i].events & EPOLLOUT){           //�������û�������Ҫ����
                    //std::cout << "Event OUT" << std::endl;
                    current_request->setHandlerRead();      //����д��־λ��λ
                }
                current_request->seperateTimer();     //����Ӧ��RequestHttp�����ʱ����Ϊ��
                request_data.push_back(current_request);
                fd_to_request[fd].reset();
            }
        }
    }
    return request_data;
}

