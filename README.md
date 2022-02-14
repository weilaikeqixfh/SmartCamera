[https://note.youdao.com/yws/public/resource/54a9c9bb8a43bd08bcbe7f358f2ee50c/xmlnote/WEBRESOURCE80a336c013b4d7a3576fc8e521bc45ee/5](https://note.youdao.com/yws/public/resource/54a9c9bb8a43bd08bcbe7f358f2ee50c/xmlnote/WEBRESOURCE80a336c013b4d7a3576fc8e521bc45ee/5)

1、摄像头启动连接服务器，发送相关信息
{"cmd":"info","devicedid":"0001"}
公网服务器需要链表记录摄像头发送的fd和deviceid;
同时返回给摄像头相关的端口信息
{"cmd":"port_info","port":"9000"}
并且要创建一个udp socket;

2、摄像头客户端创建udp socket :向公网服务器发送视频数据；
    （1）摄像头客户端创建tcp socket 向mjpg-streamer请求数据
    （2）通过udp socket 转发 到公网服务器视频数据；（大量数据）

    通过一个线程：
    获取的mjp-streamer数据；
    1.客户端需要发送请求 GET /?action=stream ;
    2.客户端需要发送验证信息，发送<=2字节可以跳过
    3.mjpj-streamer返回头部信息;客户端需要接收
    4.发送头部信息 循环 客户端接收
    5.发送一帧数据 循环 客户端接收
    6.发送尾部信息 循环 客户端接收

3、公网服务器收到视频数据之后；读取视频数据，并且开辟多个内存，每块内存保存视频数据

4、APP上线，发起TCP连接；发送获取视频数据的请求
{"cmd":"get_video_data","appid":"1000","devicedid":"0001"}
公网服务器收到数据，同时创建UDP socket，同时返回端口信息
{"cmd":"reply_port_info","port":"9001"}

APP 收到返回信息之后，创建udp socket 发送任意一个信息；
（服务器不能直接发送视频信息给客户端；因为recv函数的限制）

公网服务器收到信息,返回视频数据（从内存读取，并发送）（涉及到两个socket操作，需要加锁）

5、ＡＰＰ控制信息摄像头旋转
ＡＰＰ　发送数据：｛"cmd":"control","action":"left","appid":"1000","deviceid":"0001"}
服务器收到数据：解析deviceid
遍历链表，找到对应摄像头的fd,向fd发送数据；
摄像头收到数据；解析action，并且做出相应

