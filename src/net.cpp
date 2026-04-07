#include "../include/mq/net.hpp"

#include <cerrno>// for errno
#include <cstdlib>// for EXIT_FAILURE
#include <cstring>// for strerror
#include <iostream>

#include <sys/socket.h>// for socket functions
#include <unistd.h>// for close function
#include <netinet/in.h>// for sockaddr_in structure
#include <arpa/inet.h>
#include <sys/types.h>
//引入库文件，提供了网络编程所需的函数和类型定义，包含了套接字编程、错误处理和输入输出等功能

namespace mq
{
    ssize_t read_n(int fd,void*buf,size_t n){
        auto *p=static_cast<char*>(buf);// 将void指针转换为char指针，以便进行字节操作
        size_t left=n;// 初始化剩余字节数为n
        while(left>0){
            ssize_t r=::read(fd,p,left);// 从文件描述符fd中读取数据，将数据存储在p指向的缓冲区中，最多读取left字节
            if(r==0){
                return static_cast<ssize_t>(n-left);// 读取到文件末尾，返回已读取的字节数
            }
            if(r<0){
                if(errno==EINTR){
                    continue;// 如果读取被信号中断，继续尝试读取
                }
                return -1;// 发生错误，返回-1
            }
            p+=r;// 将指针移动到已读取数据的末尾
            left-=static_cast<size_t>(r);// 更新剩余字节数    
        }
        return static_cast<ssize_t>(n);// 成功读取n字节，返回n
    }

    ssize_t write_n(int fd,const void * buf,size_t n){
        auto *p=static_cast<const char*>(buf);// 将void指针转换为const char指针，以便进行字节操作
        size_t left=n;// 初始化剩余字节数为n
        while(left>0){
            ssize_t w=::write(fd,p,left);// 向文件描述符fd中写入
            if(w<0){
                if(errno==EINTR){
                    continue;// 如果写入被信号中断，继续尝试写入
                }
                return -1;// 发生错误，返回-1
            }
            p+=w;// 将指针移动到已写入数据的末尾
            left-=static_cast<size_t>(w);// 更新剩余字节数
        }
        return static_cast<ssize_t>(n);// 直接返回n，表示成功写入n字节
    }  

    HostPort parse_host_port(int argc,char *argv[],HostPort defaults){
        HostPort hp=defaults;//初始化HostPort结构体为默认值

        for(int i=1;i<argc;i++){
            std::string a=argv[i];// 将命令行参数转换为std::string类型，便于处理
            if(a=="--host"&&i+1<argc){
                hp.host=argv[++i];// 如果参数是--host，且后面还有参数，则将下一个参数作为主机地址
            }else if(a=="--port"&&i+1<argc){
                long v=std::strtol(argv[++i],nullptr,10);// 如果参数是--port，且后面还有参数，则将下一个参数转换为整数作为端口号
                if(v>0&&v<=65535){
                    hp.port=static_cast<uint16_t>(v);// 如果端口号在有效范围
                }
            }
        }
        return hp;// 返回解析后的HostPort结构体

    }

    static bool parse_ipv4(const std::string& host,in_addr& out){
        return inet_pton(AF_INET,host.c_str(),&out)==1;// 使用inet_pton函数将IPv4地址字符串转换为二进制形式，存储在out中，返回转换结果
    }

    int create_listen_socket(const std::string& host,uint16_t port,int backlog){
        int fd=::socket(AF_INET,SOCK_STREAM,0);//创建一个TCP套接字，使用IPv4协议
        if(fd<0) return -1;// 如果套接字创建失败，返回-1

        int yes=1;// 设置套接字选项，允许地址重用
        ::setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));// 设置套接字选项，允许地址重用
        sockaddr_in addr{};// 定义一个sockaddr_in结构体，用于存储套接字地址信息
        addr.sin_family=AF_INET;// 设置地址族为IPv4
        addr.sin_port=htons(port);// 将端口号转换为网络字节序，并存储在sin_port字段中
        if(!parse_ipv4(host,addr.sin_addr)){
            errno=EINVAL;// 如果主机地址解析失败，设置errno为EINVAL（无效参数）
            ::close(fd);// 关闭套接字
            return -1;// 返回-1表示失败
        }

        if(::bind(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0){
            // 将套接字绑定到指定的地址和端口，如果绑定失败，返回-1
            ::close(fd);// 如果绑定失败，关闭套接字
            return -1;// 返回-1表示失败
        }
        if(::listen(fd,backlog)<0){
            // 开始监听传入的连接请求，如果监听失败，返回-1
            ::close(fd);// 如果监听失败，关闭套接字
            return -1;// 返回-1表示失败
        }
        return fd;// 返回监听套接字的文件描述符
    }
    int connect_socket(const std::string& host,uint16_t port){
        int fd=::socket(AF_INET,SOCK_STREAM,0);//创建一个TCP套接字，使用IPv4协议
        if(fd<0) return -1;// 如果套接字创建失败，返回-1
        sockaddr_in addr{};// 定义一个sockaddr_in结构体，用于存储套接字地址信息
        addr.sin_family=AF_INET;// 设置地址族为IPv4
        addr.sin_port=htons(port);// 将端口号转换为网络字节序，并存储在sin_port字段中
        if(!parse_ipv4(host,addr.sin_addr)){
            errno=EINVAL;// 如果主机地址解析失败，设置errno为EINVAL（无效参数）
            ::close(fd);// 关闭套接字
            return -1;// 返回-1表示失败
        }
        if(::connect(fd,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))<0){
            // 连接到指定的地址和端口，如果连接失败，返回-1
            ::close(fd);// 如果连接失败，关闭套接字
            return -1;// 返回-1表示失败
        }
        return fd;// 返回连接套接字的文件描述符

    }
}// namespace mq  
