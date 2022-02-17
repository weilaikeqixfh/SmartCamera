#include <stdio.h>
#include<stdlib.h>
#include<string.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h> 
#include <json/json.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/epoll.h> 
#include <errno.h>
#include "node.h"
#include "camera.h"
#include "app.h"
#include "epoll.h"
#define SERVERPORT 8000
#define MAX_EVENT_NUMBER 1024

Node *head = NULL;
int port=9000;

int pic_length;
pthread_mutex_t mutex;
char *pic_data ;

void *ClientHandler(void *arg)
{
	int fd=*(int *)arg;
	int recv_size;
	char buf[256]={0};
	while(1)
	{
		recv_size=recv(fd,buf,sizeof(buf),0);
		if(recv_size<0)
		{
			perror("error");
		}
		else if(recv_size==0)
		{
			printf("客户端%d 异常下线\n",fd);
			close(fd);
			break;
		}

		printf("TCP收到数据 %s \n", buf);
		struct json_object *obj=json_tokener_parse(buf);
		struct json_object *val;
		json_object_object_get_ex(obj,"cmd",&val);
		const char *cmd = (const char*)json_object_get_string(val);

		if(!strcmp(cmd,"info"))
		{
			camera_online(obj,fd);
		}

		else if(!strcmp(cmd,"get_video_data"))
			{
				app_send_video_data(fd);
			}
			else if(!strcmp(cmd,"control"))
			{
				printf("控制命令\n");
				app_send_control_info(obj);
			}
		//{
		//	app_send_video_data(fd);			
	//	}
		
	//	else if(!strcmp(cmd,"control"))
	//	{
	//		printf("控制命令\n");
	//		app_send_control_info(obj,fd);
	//	}
	}
}

int main()
{
	InitLink(&head);
	pic_data = (char *)malloc(BUFLEN);
	pthread_mutex_init(&mutex,NULL);
	int sockfd=socket (AF_INET,SOCK_STREAM ,0);
	if(-1==sockfd )
	{
		perror("socket");
		exit(1);
	}
	int opt = 1;//地址复用；
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));

	
	struct sockaddr_in server_addr;
	server_addr.sin_family=AF_INET;
	server_addr.sin_port=htons(SERVERPORT);
	inet_pton(AF_INET,"172.19.215.163",&server_addr.sin_addr);
	if(bind(sockfd, (struct sockaddr *)&server_addr,sizeof(server_addr))!=0)
	{
		perror("bind error");
		exit(1);
	}
	
	if(listen(sockfd,10)!=0)
	{
		perror("listen error");
		exit(1);
	}
	
	struct sockaddr_in client_addr;
	socklen_t length=sizeof(client_addr);

	//创建epoll内核事件表
        int epollfd=epoll_create(20);
	// 用于存储epoll事件表中就绪事件的event数组
        struct epoll_event events[MAX_EVENT_NUMBER];
	//主线程往epoll内核事件表中注册监听socket事件，当listen到新的客户连接时，listenfd变为就绪事件;listenfd上不能注册EPOLLONESHOT事件，否则后续的客户连接请求不能触发listenfd上的EPOLLIN事件 
        addfd(epollfd, sockfd, false);

	while(1)
	{
		int ret=epoll_wait(epollfd,events,MAX_EVENT_NUMBER,-1);
		 if(ret<0)
                	{printf("epoll failure\n");break;}
		
		for(int i=0;i<ret;i++)
	        {int epollsockfd = events[i].data.fd;// 事件表中就绪的socket文件描述符
			if(epollsockfd == sockfd)//如果就绪的是监听套接字，监听到新用户连接}
			{int fd=accept(sockfd,(struct sockaddr *)&client_addr ,&length);
				if(fd==-1){perror("accept error");}
        		addfd(epollfd, fd, false);
			}
			 else{
			printf("创建线程\n");
			pthread_t tid;
			pthread_create(&tid,NULL,ClientHandler,(void*)&epollsockfd);
			pthread_detach(tid);
			usleep(10000);
			 }
		

		}
	}

	return 0;

}
