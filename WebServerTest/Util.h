#ifndef __UTIL_H
#define __UTIL_H

#pragma once
#include <cstdlib>
#include <string>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include "RequestHttp.h"
#include <iostream>
#include "Timer.h"

ssize_t readn(int fd, void *buff, size_t n);
ssize_t readn(int fd, std::string &inBuffer, bool &zero);
ssize_t readn(int fd, std::string &inBuffer);
ssize_t writen(int fd, void *buff, size_t n);
ssize_t writen(int fd, std::string &sbuff);

void SIGPIPE_HANDLER();
int setNonBlocking(int fd);
void setSocketNodelay(int fd);
void setSocketNoLinger(int fd);
void shutDownWR(int fd);
int socket_bind_listen(int port);

void MyHandler(std::shared_ptr<void> req);
void MyHandler_Test(std::shared_ptr<void> req);



#endif // __UTIL_H
