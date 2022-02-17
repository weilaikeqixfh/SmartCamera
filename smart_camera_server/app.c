#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>           
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <json/json.h>
#include <pthread.h>
#include "camera.h"
#include <unistd.h>
#include "node.h"

extern int port;
extern pthread_mutex_t mutex;
extern char *pic_data;
extern int pic_length;
extern Node *head;

void *app_video_data(void *arg)
{
	//创建udp socket
	int sockfd = socket(AF_INET,SOCK_DGRAM,0);
	if(-1==sockfd)
	{
		perror("app udp socket error");
		return NULL;
	}

	struct sockaddr_in server_addr;
	bzero(&server_addr,sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port -1);
	inet_pton(AF_INET,"172.19.215.163",&server_addr.sin_addr);
	if(bind(sockfd,(struct sockaddr *)&server_addr,sizeof(server_addr))<0)
	{
		perror("bind error app aliserver");
		return NULL;
	}

	//获取app发送过来的数据
	char * buf = (char *)malloc(BUFLEN);
	struct sockaddr_in app_addr;
	int length = sizeof(app_addr);
	recvfrom(sockfd,buf,16,0,(struct sockaddr *)&app_addr,(socklen_t *)&length);
	while(1)
	{
		//共享内存上锁
		pthread_mutex_lock(&mutex);
		memcpy(buf,pic_data,pic_length);
		pthread_mutex_unlock(&mutex);

		if(sendto(sockfd, buf,pic_length,0,(struct sockaddr*)&app_addr,length)<0)
		{
			perror("send picture to app error");
		}
			usleep(40000);
	}

}


void app_send_video_data(int fd)
{
	//启动线程 创建udp socket
	pthread_t tid;
	pthread_create(&tid, NULL, app_video_data,NULL);
	
	//返回端口信息，json类型
	struct json_object *obj = json_object_new_object();
	json_object_object_add(obj,"cmd",json_object_new_string("reply_port_info"));
	json_object_object_add(obj,"port",json_object_new_int(port));
	const char *s = json_object_to_json_string(obj);

	port++;
	if(send(fd,s,strlen(s),0)<0)
	{
		perror("send to app error");
		return ;
	}

}

void app_send_control_info(struct json_object *obj)
{
	struct json_object *val;
	json_object_object_get_ex(obj,"deviceid",&val);
	const char *deviceid = json_object_get_string(val);

	int fd = TraverseLink(deviceid);
	if(-1 == fd)
	{
		printf("摄像头不存在\n");
		return ;
	}
	
	//服务器将接收到的json格式控制命令解析后转发到对应的摄像头；
	const char *s = json_object_to_json_string(obj);
	if(send(fd,s,strlen(s),0))
	{
		printf("服务器转发控制指令失败\n");
	}
}
