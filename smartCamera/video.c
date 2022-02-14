#include <stdio.h>
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "video.h"

void *send_video_data(void *arg)
{
	//接收端口号·
	int server_port=*(int *)arg; 

	//创建tcp socket
	int video_sockfd = socket(AF_INET,SOCK_STREAM,0);
		if(video_sockfd ==-1)
		{
			perror("socket error");
			exit(1);
		}

	struct sockaddr_in video_addr;
	bzero(&video_addr ,sizeof(video_addr));
	video_addr.sin_family =AF_INET ;
	video_addr.sin_port =htons(VIDEOPORT);
        inet_pton(AF_INET,VIDEOADDR,&video_addr.sin_addr);


	//发起连接
	if(connect(video_sockfd,(struct sockaddr*)&video_addr,sizeof(video_addr))<0)
	{
		perror("connect error");
		exit(1);
	}
	//连接成功打印一个方便调试
	printf("connect to mjpg-streamer success");

	//创建udp socket 发送视频数据；
	int server_sockfd=socket(AF_INET,SOCK_DGRAM,0);
       if(server_sockfd ==-1)
       {
	       perror("udp socket error ");
	       exit(1);
       }	       
       struct sockaddr_in server_addr;
       bzero(&server_addr ,sizeof(server_addr));
       server_addr.sin_family=AF_INET;
       server_addr.sin_port=htons(server_port);
       inet_pton(AF_INET,SERVER_ADDR,&server_addr.sin_addr);

	//发送post;
       char *buf = (char *)malloc(BUFLEN);
       memset(buf,0,BUFLEN);
       strcpy(buf,"GET /?action=stream\n");
       if(send(video_sockfd,buf,strlen(buf),0)<0)
       {
	       perror("send post");
	       exit(1);
       }

	//发送userinfo;任意两个字节
	if(send(video_sockfd,"f\n",2,0)<0)
	{
		perror("send userinfo");
		exit(1);
	}
	memset(buf,0,BUFLEN);
	if(recv(video_sockfd,buf,BUFLEN,0)<0)
	{
		perror("recv head info");

	}
	int recv_size;
	char *begin, *end;
	char cont_len[10]={0};
	while(1)
	{
		//接收头部信息
		memset(buf,0,BUFLEN);
		recv_size=recv(video_sockfd,buf,BUFLEN,0);
		if(recv_size=-1)
		{
			perror("recv");

		}
		//解析获取的信息
		if(strstr(buf,"Content-Length"))
		{
			begin=strstr(buf,"Content-Length");
			end=strstr(buf,"X-Timestamp");
			memcpy(cont_len,begin+16 ,end-2-begin-16);
			printf("recv head Content-Length=%d\n",atoi(cont_len));
		}
		else
		{
			continue;
		}

		memset(buf,0, BUFLEN);
		//接收帧数据
		recv_size=recv(video_sockfd,buf,atoi(cont_len),0);
		printf("recv picture data %d\n",recv_size);
		//发送给公网服务器数据（udp协议）
		if(sendto(server_sockfd,buf,recv_size,0,(struct sockaddr *)&server_addr,sizeof(server_addr))<0)
		{
			perror("sendto server error");

		}

		//接收尾部信息
		memset(buf,0,BUFLEN);
		recv_size=recv(video_sockfd,buf,1024,0);
		

	}

}
