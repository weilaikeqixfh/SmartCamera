#ifndef VIDEO_H
#define VIDEO_H
//定义视频端口号的端口号和地址

#define VIDEOPORT 8080
#define VIDEOADDR "127.0.0.1"

//公网服务器的端口号和地址
#define SERVER_PORT  8000
#define SERVER_ADDR "8.130.10.145"

#define BUFLEN  100*1024
void *send_video_data(void *arg);


#endif
