#include "Util.h"
/***************************************************************
�ֽ����׽��֣�TCP�׽��֣��ϵ�read��write���������ֵ���Ϊ��ͬ��ͨ�����ļ�I/O��
�ֽ����׽��ֵ���read��write�����������ֽ������ܱ�����������٣�Ȼ���Ⲣ���ǳ����״̬��
������Ϊ�ں��������׽��ֵĻ��������ܴﵽ�˼��ޡ�
***************************************************************/

/***************************************************************
size_tһ��������ʾһ�ּ����������ж��ٶ����������ȡ����磺sizeof�������Ľ��������size_t��
�����ͱ�֤������ʵ������������������ֽڴ�С��
������������ǡ����ڼ����ڴ��п����ɵ�������Ŀ�������޷����������͡���
���ԣ����������±���ڴ������֮��ĵط��㷺ʹ�á�
��ssize_t:�����������������ʾ���Ա�ִ�ж�д���������ݿ�Ĵ�С.
����size_t����,��������signed.�⼴������ʾ����sign size_t���͵ġ�
***************************************************************/

const int BUFF_MAX = 4096;

ssize_t readn(int fd, void *buff, size_t n){
    size_t nleft = n;
    ssize_t nread = 0;
    ssize_t readSum = 0;
    char *ptr = (char*)buff;
    while (nleft > 0){
        if ((nread = read(fd, ptr, nleft)) < 0){
            if (errno == EINTR)   //���ź��жϣ��ض�
                nread = 0;        //�ض�
            else if (errno == EAGAIN){
                //��ʾwrite�����Ƿ��������������û�����ݿɶ�
                //��ʱ�ͻ���ȫ�ֱ���errnoΪEAGINA,��ʾ�����ٴν��ж�������
                return readSum;
            }
            else{ //����
                return -1;
            }
        }
        else if (nread == 0) //����ĩβEOF
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
        else if (nread == 0){   //����ĩβ
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
                else if(errno == EAGAIN){ //���жϣ���д
                    return writeSum;
                }
                else  //����
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

void SIGPIPE_HANDLER(){       //ע��ܵ������źŴ�����
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = SIG_IGN;  //Ĭ���źŴ�������Ϊ����
    sa.sa_flags = 0;          //�������0
    if(sigaction(SIGPIPE, &sa, NULL)) //sigaction���������޸���ָ���ź�������Ĵ�����
        return;
    //��һ������ָ��Ҫ������ź����ͣ��ڶ�������ָ���µ��źŴ���ʽ�����������������ǰ�źŵĴ���ʽ�������ΪNULL�Ļ���
}

int setNonBlocking(int fd){
    int flags = fcntl(fd, F_GETFL, 0);  //ͨ��fcntl���Ըı��Ѵ򿪵��ļ�����
    //F_GETFL��ȡ�ļ��򿪷�ʽ�ı�־
    if(flags < 0){
        perror("Fcntl F_GETFL error:");
        return -1;
    }

    flags |= O_NONBLOCK;                 //����Ϊ������ģʽ
    if(fcntl(fd, F_SETFL, flags) < 0){   //����fd�ļ��򿪷�ʽΪflagsָ���ķ�ʽ
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


