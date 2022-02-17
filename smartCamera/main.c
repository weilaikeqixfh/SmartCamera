#include <stdio.h>

#include <sys/types.h>          
 /*  See NOTES */
#include <sys/socket.h>
#include <stdlib.h>
#include <json/json.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>

#include "video.h"
#include "motor.h"



int main()
{
	//第一步：创建客户端socket；
        int sockfd;
        if((sockfd=socket(AF_INET,SOCK_STREAM,0))==-1)
        {
                perror("sockfd error");
                return -1;
        }           
        //第二步：向服务器发起连接请求
            
        struct sockaddr_in myserveraddr;
       // int port=atoi(argv[2]);
        bzero(&myserveraddr,sizeof(myserveraddr));
        myserveraddr.sin_family=AF_INET;
        myserveraddr.sin_port=htons(SERVER_PORT);
        inet_pton(AF_INET,SERVER_ADDR,&myserveraddr.sin_addr);
        if(connect(sockfd,(struct sockaddr *)&myserveraddr,sizeof(myserveraddr))!=0)
        {
                perror("connect error");
                return -1;
        }
	//创建json对象
	struct json_object *obj = json_object_new_object();
	json_object_object_add(obj,"cmd",json_object_new_string("info"));
	json_object_object_add(obj,"deviceid",json_object_new_string("0001"));//简化直接写出
	const char*s=(const char *)json_object_to_json_string(obj);
	if(send(sockfd,s,strlen(s),0)<0)
	{
		perror("send error");
		return -1;
	}
	
	//接收服务器的端口信息
	char buf[256]={0};
	if(recv(sockfd,buf,sizeof(buf),0)<0)
	{
		perror("recv error");
		return -1;
	}
	struct json_object *serverdata= json_tokener_parse(buf);
	
	struct json_object *cmd = json_object_new_object();
	struct json_object *port = json_object_new_object();

	int server_udp_port;
	cmd=json_object_object_get(serverdata,"cmd");
	port=json_object_object_get(serverdata,"port");
	if(!strcmp((const char*)json_object_get_string(cmd),"port_info"))
	{
		server_udp_port=json_object_get_int(port);
	}
	else
	{
		printf("recv error\n");
	}
	//创建一个线程，通过send_video_data 函数发送视频数据；该函数需要知道udp的端口号
	pthread_t tid;
       pthread_create(&tid,NULL,send_video_data,&server_udp_port );
	while(1)
	{
		bzero (buf,sizeof(buf));
		if(recv(sockfd,buf,sizeof(buf),0)<0)
		{
			perror("recv error\n");
		}
		serverdata =json_tokener_parse (buf);
		
	struct json_object *cmd = json_object_new_object();
	struct json_object *action = json_object_new_object();

	cmd=json_object_object_get(serverdata,"cmd");
	action=json_object_object_get(serverdata,"action");

	if(!strcmp(json_object_get_string(cmd),"control"))
	{
		const char *act = json_object_get_string(action);
	if(!strcmp(act,"left"))
	{
		//调动相应的函数让舵机转动起来
		motor_turn_left();
	}

	else if(!strcmp(act,"right"))
	{		
		motor_turn_right();
	}
      	else if(!strcmp(act,"up"))
	{	
		motor_turn_up();
	}
	else if(!strcmp(act,"down"))
	{	
		motor_turn_down();
	}
	}
	}
	return 0;

}
