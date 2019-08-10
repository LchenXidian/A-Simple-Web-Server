#include "Util.h"
/***************************************************************
字节流套接字（TCP套接字）上的read和write函数所表现的行为不同于通常的文件I/O。
字节流套接字调用read或write输入或输出的字节数可能比请求的数量少，然而这并不是出错的状态。
这是因为内核中用于套接字的缓冲区可能达到了极限。
***************************************************************/

/***************************************************************
size_t一般用来表示一种计数，比如有多少东西被拷贝等。例如：sizeof操作符的结果类型是size_t，
该类型保证能容纳实现所建立的最大对象的字节大小。
它的意义大致是“适于计量内存中可容纳的数据项目个数的无符号整数类型”。
所以，它在数组下标和内存管理函数之类的地方广泛使用。
而ssize_t:这个数据类型用来表示可以被执行读写操作的数据块的大小.
它和size_t类似,但必需是signed.意即：它表示的是sign size_t类型的。
***************************************************************/

const int BUFF_MAX = 4096;

ssize_t readn(int fd, void *buff, size_t n){
    size_t nleft = n;
    ssize_t nread = 0;
    ssize_t readSum = 0;
    char *ptr = (char*)buff;
    while (nleft > 0){
        if ((nread = read(fd, ptr, nleft)) < 0){
            if (errno == EINTR)   //被信号中断，重读
                nread = 0;        //重读
            else if (errno == EAGAIN){
                //表示write本来是非阻塞情况，现在没有数据可读
                //这时就会置全局变量errno为EAGINA,表示可以再次进行读操作。
                return readSum;
            }
            else{ //出错
                return -1;
            }
        }
        else if (nread == 0) //读到末尾EOF
            break;
        readSum += nread;
        nleft -= nread;
        ptr += nread;
    }
    return readSum;
}
ssize_t readn(int fd, std::string& buffer, bool& flag){
    ssize_t nread = 0;
    ssize_t readSum = 0;
    while (true){
        char buff[BUFF_MAX];
        if ((nread = read(fd, buff, BUFF_MAX)) < 0){
            if (errno == EINTR) continue;
            else if (errno == EAGAIN)   return readSum;
            else{
                perror("Read error:");
                return -1;
            }
        }
        else if (nread == 0){   //读到末尾
            flag = true;
            break;
        }
        readSum += nread;
        buffer += std::string(buff, buff + nread);
    }
    return readSum;
}

ssize_t readn(int fd, std::string &buffer){
    ssize_t nread = 0;
    ssize_t readSum = 0;
    while (true){
        char buff[BUFF_MAX];
        if ((nread = read(fd, buff, BUFF_MAX)) < 0){
            if (errno == EINTR) continue;
            else if (errno == EAGAIN)   return readSum;
            else{
                perror("Read error");
                return -1;
            }
        }
        else if (nread == 0)    break;
        readSum += nread;
        buffer += std::string(buff, buff + nread);
    }
    return readSum;
}

ssize_t writen(int fd, void *buff, size_t n){
    size_t nleft = n;
    ssize_t nwritten = 0;
    ssize_t writeSum = 0;
    const char *ptr = (char*)buff;
    while (nleft > 0){
        if ((nwritten = write(fd, ptr, nleft)) <= 0){
            if (nwritten < 0){
                if (errno == EINTR){
                    nwritten = 0;
                    continue;
                }
                else if(errno == EAGAIN){ //被中断，重写
                    return writeSum;
                }
                else  //出错
                    return -1;
            }
        }
        writeSum += nwritten;
        nleft -= nwritten;
        ptr += nwritten;
    }
    return writeSum;
}

ssize_t writen(int fd, std::string& wbuff){
    size_t nleft = wbuff.size();
    ssize_t nwritten = 0;
    ssize_t writeSum = 0;
    const char *ptr = wbuff.c_str();
    while (nleft > 0){
        if ((nwritten = write(fd, ptr, nleft)) <= 0){
            if (nwritten < 0){
                if (errno == EINTR){
                    nwritten = 0;
                    continue;
                }
                else if (errno == EAGAIN)
                    break;
                else
                    return -1;
            }
        }
        writeSum += nwritten;
        nleft -= nwritten;
        ptr += nwritten;
    }
    if (writeSum == static_cast<ssize_t>(wbuff.size()))
        wbuff.clear();
    else
        wbuff = wbuff.substr(writeSum);  //get from position writeSum to the end
    return writeSum;
}

void SIGPIPE_HANDLER(){       //注册管道破裂信号处理函数
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = SIG_IGN;  //默认信号处理函数设为忽略
    sa.sa_flags = 0;          //可设参数0
    if(sigaction(SIGPIPE, &sa, NULL)) //sigaction函数检查或修改与指定信号相关联的处理动作
        return;
    //第一个参数指出要捕获的信号类型，第二个参数指定新的信号处理方式，第三个参数输出先前信号的处理方式（如果不为NULL的话）
}

int setNonBlocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);  //通过fcntl可以改变已打开的文件性质
    //F_GETFL获取文件打开方式的标志
    if(flags < 0){
        perror("Fcntl F_GETFL error:");
        return -1;
    }

    flags |= O_NONBLOCK;                 //设置为非阻塞模式
    if(fcntl(fd, F_SETFL, flags) < 0){   //设置fd文件打开方式为flags指定的方式
        perror("Fcntl F_SETFL error:");
        return -1;
    }
    return 0;
}

void MyHandler(std::shared_ptr<void> req){
    //cout << std::this_thread::get_id() << ":" ;
    std::shared_ptr<RequestHttp> request = std::static_pointer_cast<RequestHttp>(req);
    //cout << request->getHandlerRead() << " " << request->getHandlerWrite() << endl;
    if(request->getHandlerWrite()){
        //std::cout << "Write" << endl;
        request->handleWrite();
    }
    else if(request->getHandlerRead()){
        //std::cout << "Read" << endl;
        request->handleRead();
    }
    request->handleConn();
}

void MyHandler_Test(std::shared_ptr<void> req){
    std::shared_ptr<RequestHttp> request = std::static_pointer_cast<RequestHttp>(req);
    //cout << "==========" << request->getFd() << "==========" << endl;
}


