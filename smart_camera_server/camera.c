#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include  <sys/types.h>   
#include <sys/socket.h>
#include <arpa/inet.h>
#include <json/json.h>
#include <pthread.h>
#include "node.h"
#include "camera.h"

extern int port;
extern pthread_mutex_t mutex;
extern char *pic_data;
extern int pic_length;

//解析json数据得到deviceid;
//操作链表，插入deviceid和fd;
//返回摄像头端口信息
void camera_online(struct json_object *json,int fd)
{
	printf("进入摄像头上线处理程序\n");
	struct json_object* val;
	//json_bool json_object_object_get_ex(struct json_object* obj, const char *key, struct json_object **value);
	json_object_object_get_ex(json, "deviceid", &val);
	const char *dev_id = json_object_get_string(val);
	InsertLink(dev_id,fd);

	struct json_object *obj =json_object_new_object();
	json_object_object_add(obj,"cmd",json_object_new_string("port_info"));
	json_object_object_add(obj,"port",json_object_new_int(port));
	port++;

	const char *s = json_object_to_json_string(obj);
	if(send(fd,s,strlen(s),0)<0)
	{
		perror("send to camera error");
		exit(1);
	}

	//同时启动udp socket,接收摄像头数据
	int sockfd = socket(AF_INET,SOCK_DGRAM,0);
	if(-1==sockfd)
	{
		perror("server udp socket error");
		exit(1);
	}

	struct sockaddr_in server_udp_addr;
	memset(&server_udp_addr,0,sizeof(server_udp_addr));
	server_udp_addr.sin_family = AF_INET;
	server_udp_addr.sin_port = htons(port-1);
	inet_pton(sockfd,"172.19.215.163",&server_udp_addr.sin_addr.s_addr);
	//绑定
	if(bind(sockfd,(struct sockaddr *)&server_udp_addr,sizeof(server_udp_addr))<0)
	{
		perror("bind error");
		exit(1);
	}

	struct sockaddr_in camera_addr;
	int length = sizeof(camera_addr);
	int recv_size;
	char *buf = (char *)malloc(BUFLEN);
	
	printf("准备接收视频数据\n");
	while(1)
	{
		memset(buf,0,BUFLEN);
		recv_size = recvfrom(sockfd,buf,BUFLEN,0,(struct sockaddr *)&camera_addr,(socklen_t *)&length);
		if(recv_size<=0)
		{
			perror("recvfrom camera error");
			exit(1);
		}

		printf("收到一张照片 %d\n",recv_size);
		pthread_mutex_lock(&mutex);
		bzero(pic_data,BUFLEN);
		memcpy(pic_data,buf,recv_size);
		pic_length = recv_size;
		pthread_mutex_unlock(&mutex);
	}

}
