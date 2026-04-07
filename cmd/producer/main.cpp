#include "../../include/mq/net.hpp"
#include <iostream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <string>

static std::string parse_message(int argc, char *argv[]) {
    for(int i=1;i<argc;i++){
        std::string a= argv[i];
        if(a=="-message" && i+1<argc){
            return argv[i+1];//解析命令行参数，获取消息内容
        }
    }
    return "Hello, World!"; // 默认消息
}
int main(int argc,char *argv[]){
    mq::HostPort hp= mq::parse_host_port(argc,argv,mq::HostPort{"0.0.0.0",9092});//解析命令行参数，获取主机和端口信息
    std::string message=parse_message(argc,argv);//解析命令行参数，获取消息内容
    int fd=mq::connect_socket(hp.host,hp.port);//连接到指定的主机和端口
    if(fd<0){
        std::cerr<<"Failed to connect to broker: "<<std::strerror(errno)<<std::endl;
        return 1;
    }
    if(mq::write_n(fd,message.data(),message.size())<0){
        std::cerr<<"Failed to send message to broker: "<<std::strerror(errno)<<std::endl;
        ::close(fd);
        return 1;
    }

    char buf[128];//缓冲区用于接收来自服务器的响应
    ssize_t r=mq::read_n(fd,buf,sizeof(buf));//从服务器套接字读取响应数据
    if(r>0){
        std::cout<<"producer: received: ";
        std::cout.write(buf,r);//将接收到的数据写入标准输出
    }else if(r==0){
        std::cout<<"producer:server closed"<<"\n";//服务器关闭连接
    }else{
        std::cerr<<"Failed to read response from broker: "<<std::strerror(errno)<<std::endl;
    }
    ::close(fd);
    return 0;
}