#ifndef EPILL_H
#define EPOLL_H
#include <stdbool.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
//将文件描述符设置为非阻塞
int setnonblocking(int fd);
//向epoll中添加文件描述符
int addfd(int epollfd, int fd, bool one_shot);

//从epoll中删除文件描述符
int removefd(int epollfd, int fd);
// 修改描述符状态;注意要重置EPOLLONESHOT事件，确保下一次可读时epoll能被触发
int modfd(int epoll_fd, int fd, int ev);


#endif
