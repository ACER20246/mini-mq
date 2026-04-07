#include "../../include/mq/net.hpp"
#include <iostream>
#include <cerrno>
#include <csignal>
#include <unistd.h>

#include <sys/socket.h>
#include <cstring>

static volatile std::sig_atomic_t g_stop=0;//全局变量，标志是否停止服务器

static void on_sigint(int){
    g_stop=1;//接收到SIGINT信号时，设置停止标志
}
int main(int argc,char *argv[]){
    std::signal(SIGINT,on_sigint);//注册SIGINT信号处理函数

    mq::HostPort hp= mq::parse_host_port(argc,argv,mq::HostPort{"0.0.0.0",9092});//解析命令行参数，获取主机和端口信息
    int listen_fd=mq::create_listen_socket(hp.host,hp.port);//创建监听套接字
    if(listen_fd==-1){
        std::cerr<<"Failed to create listen socket: "<<std::strerror(errno)<<std::endl;
        return 1;
    }
    
    std::cout<<"Broker is listening on "<<hp.host<<":"<<hp.port<<std::endl;
    while(!g_stop){
        sockaddr_storage ss{};//定义一个sockaddr_storage结构体来存储客户端地址信息
        socklen_t ss_len=sizeof(ss);
        int cfd=::accept(listen_fd,reinterpret_cast<sockaddr*>(&ss),&ss_len);//接受客户端连接
        if(cfd<0){
            if(errno==EINTR){
                std::cerr<<"broker:accept failed: "<<std::strerror(errno)<<std::endl;
                continue;//如果接受连接失败且错误是EINTR，继续接受下一个连接
            }
            break;//其他错误则退出循环
        }

        char buf[4096];
        ssize_t r =::read(cfd,buf,sizeof(buf));//从客户端套接字读取数据
        if(r>0){
            std::cout<<"Received "<<r<<" bytes from client"<<std::endl;
            //这里可以添加处理客户端请求的逻辑
        }
        const char ok[]="OK";
        if(::write(cfd,ok,sizeof(ok)-1)<0){
            std::cerr<<"Failed to write response to client: "<<std::strerror(errno)<<std::endl;
        }
        ::close(cfd);//关闭客户端套接字
    }
    ::close(listen_fd);//关闭监听套接字
    std::cout<<"Broker stopped"<<std::endl;
    return 0;
}