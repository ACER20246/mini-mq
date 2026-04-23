#pragma once

#include<cstddef>
#include<cstdint>
#include<string>

#include<sys/types.h>

namespace mq{

    
    ssize_t read_n(int fd, void* buf, size_t n);
    // 读取指定字节数的数据，直到读取完成或发生错误

    ssize_t write_n(int fd, const void* buf, size_t n);
    // 写入指定字节数的数据，直到写入完成或发生错误

    struct HostPort{
        std::string host="127.0.0.1";
        uint16_t port=9092;
    };
    // HostPort结构体用于表示主机地址和端口号，默认值为127.0.0.1:9092

    HostPort parse_host_port(int argc, char* argv[],HostPort defaults={});
    // 解析命令行参数，提取主机地址和端口号，如果未提供则使用默认值

    int create_listen_socket(const std::string& host, uint16_t port, int backlog=128);
    // 创建一个监听套接字，绑定到指定的主机地址和端口，并开始监听传入的连接请求
    
    int connect_socket(const std::string& host, uint16_t port);
    // 创建一个连接套接字，连接到指定的主机地址和端口，并返回套接字文件描述符

} // mq命名空间包含了一些与网络相关的函数和结构体，用于处理套接字通信和命令行参数解析