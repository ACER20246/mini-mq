#include "../../include/mq/net.hpp"
#include "../../include/mq/protocol.hpp"

#include <iostream>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <unordered_map>
#include <mutex>

#include <sys/socket.h>
#include <cstring>

static volatile std::sig_atomic_t g_stop=0;//全局变量，标志是否停止服务器

static void on_sigint(int){
    g_stop=1;//接收到SIGINT信号时，设置停止标志
}

static std::unordered_map<std::string, std::vector<std::string>> g_topics;//全局变量，存储主题和消息的映射关系
static std::mutex g_topics_mutex;//全局变量，用于保护主题和消息的映射关系

static void handle_frame(int cfd,const mq::Frame& frame){
    using namespace mq;
    if(frame.type==MessageType::Produce){
        ProduceReq req;
        if(!decode_produce_req(frame,req)){
            auto payload=encode_error_resp(ErrorCode::BadRequest,"Invalid ProduceReq");
            auto bytes=encode_frame(MessageType::Error,0,payload);
            write_n(cfd,bytes.data(),bytes.size());
            return;
        }//处理生产请求
        uint64_t offset=0;
        {
            std::lock_guard<std::mutex> lock(g_topics_mutex);//加锁保护主题和消息的映射关系
            auto& messages=g_topics[req.topic];//获取主题对应的消息列表
            offset=static_cast<uint64_t>(messages.size());//获取当前消息列表的大小作为新消息的偏移量
            messages.push_back(req.message);//将新消息添加到消息列表中
        }
        auto payload=encode_produce_resp(offset);//编码生产响应，包含消息的偏移量
        auto bytes=encode_frame(MessageType::Resp,0,payload);//将生产响应编码成一个消息帧
        write_n(cfd,bytes.data(),bytes.size());//将消息帧发送给客户端
        return;
    }
    if(frame.type==MessageType::Fetch){
        FetchReq req;
        if(!decode_fetch_req(frame,req)){
            auto payload=encode_error_resp(ErrorCode::BadRequest,"Invalid FetchReq");
            auto bytes=encode_frame(MessageType::Error,0,payload);
            write_n(cfd,bytes.data(),bytes.size());
            return;
        }

        std::vector<std::pair<uint64_t,std::string>> out;
        {
            std::lock_guard<std::mutex> lock(g_topics_mutex);//加锁保护主题
            auto it=g_topics.find(req.topic);//查找主题对应的消息列表
            if(it==g_topics.end()){
                auto payload = encode_error_resp(ErrorCode::BadRequest,"Topic not found");
                auto bytes = encode_frame(MessageType::Error, 0, payload);
                write_n(cfd, bytes.data(), bytes.size());
                return;
            }
            const auto& messages=it->second;//获取主题对应的消息列表 
            if(req.offset>messages.size()){
                auto payload=encode_error_resp(ErrorCode::OffsetOutOfRange,"Offset out of range");
                auto bytes=encode_frame(MessageType::Error,0,payload);
                write_n(cfd,bytes.data(),bytes.size());
                return;
            }    
            uint64_t offset=req.offset;//获取请求中的偏移量
            uint32_t left=req.max_bytes;//获取请求中的最大字节数
            while(offset<messages.size()&&left>0){
                out.emplace_back(offset,messages[static_cast<size_t>(offset)]);//将消息的偏移量和内容添加到输出列表中
                left--;
                offset++;
            }
        }
        auto payload=encode_fetch_resp(out);//编码获取响应，包含消息的偏移量和内容
        auto bytes=encode_frame(MessageType::Resp,0,payload);//将获取响应编码成一个消息帧
        write_n(cfd,bytes.data(),bytes.size());//将消息帧发送给客户端
        return;
    }
    auto payload=encode_error_resp(ErrorCode::UnknownType,"Unknown message type");
    auto bytes=encode_frame(MessageType::Error,0,payload);
    write_n(cfd,bytes.data(),bytes.size());//处理未知类型的消息，返回错误响应给客户端
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
            if(errno==EINTR)
                continue;//如果接受连接失败且错误是EINTR，继续接受下一个连接
            std::cerr<<"broker:accept failed: "<<std::strerror(errno)<<std::endl;
            break;//其他错误则退出循环
        }

        std::vector<uint8_t> inbuf;//定义一个输入缓冲区，用于存储从客户端读取的数据
        inbuf.reserve(8192);//预留空间，但不要预先填充内容
        for(;;){
            uint8_t tmp[4096];
            ssize_t n=::read(cfd,tmp,sizeof(tmp));//从客户端套接字读取数据
            if(n==0) break;
            if(n<0){
                if(errno==EINTR) continue;//如果读取被信号中断，继续尝试读取
                std::cerr<<"Failed to read from client: "<<std::strerror(errno)<<std::endl;
                break;//其他错误则退出循环
            }
            inbuf.insert(inbuf.end(),tmp,tmp+n);//将读取到的数据追加到输入缓冲区中
            for(;;){
                mq::Frame frame;//定义一个消息帧对象，用于存储解码后的消息帧数据
                mq::ErrorCode err;
                if(!mq::try_decode_one(inbuf,frame,err)){
                    if(err==mq::ErrorCode::Ok){
                        break;//如果解码成功，继续尝试解码下一个消息帧
                    }
                    std::cerr<<"Failed to decode frame from client: "<<static_cast<int>(err)<<std::endl;
                    break;//如果解码失败，输出错误信息并退出循环
                }
                handle_frame(cfd,frame);//处理解码后的消息帧
            }
        }
        ::close(cfd);//关闭客户端套接字
    }
    ::close(listen_fd);//关闭监听套接字
    std::cout<<"Broker stopped"<<std::endl;
    return 0;
}