# A-Simple-Web-Server

使用C++11编写的简单Web服务器，能够解析Get、Head请求。

  Ubuntu 16.04 LTS

  G++ 4.8.5

  OpenCV 3.4.6

服务器默认配置IP地址及端口号：127.0.0.1 8080. 可在main.cpp中进行调增。


##技术要点：

1.使用Epoll边沿触发的IO多路复用技术，非阻塞IO

2.使用线程池避免线程频繁创建销毁的开销，充分利用多核CPU

3.主线程负责Accept请求，以轮流算法分发给子线程进行IO处理，锁的争用只会出现在主线程和某一特定线程中（主线程向任务队列中添加数据时）

4.使用了C++11的thread、mutex、unique_lock和conditional_variable等多线程处理类

5.使用了只能指针等RAII(资源获取即初始化)减少内存泄漏的可能。

注：C++标准保证任何情况下，已构造的对象最终会销毁，即它的析构函数最终会被调用



使用开源测试工具Webbench在本机进行回显测试，测试结果在Test Result中


感谢作者Linyacool, https://github.com/linyacool/WebServer
本代码进行了多处参考。
