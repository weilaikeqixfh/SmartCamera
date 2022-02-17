#include "epoll.h"
//将文件描述符设置为非阻塞
int setnonblocking(int fd)
{
	int old_option = fcntl(fd,F_GETFL);
        int new_option = old_option | O_NONBLOCK;
        fcntl(fd,F_SETFL,new_option);
        return old_option;//返回文件描述符旧的状态，以便日后恢复该状态；

}
//向epoll中添加文件描述符
int addfd(int epollfd, int fd, bool one_shot)
{
	struct epoll_event event;
	event.data.fd=fd;
	event.events = EPOLLIN | EPOLLRDHUP;
	if(one_shot)
        {event.events|=EPOLLONESHOT;}   
        epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);
        setnonblocking(fd);
	return 0;
}

//从epoll中删除文件描述符
int removefd(int epollfd, int fd)
{
	epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
	close(fd);
	return 0;

}

// 修改描述符状态;注意要重置EPOLLONESHOT事件，确保下一次可读时epoll能被触发
int modfd(int epoll_fd, int fd, int ev)
{
    struct epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLRDHUP | EPOLLONESHOT;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
    return 0;
    
}


