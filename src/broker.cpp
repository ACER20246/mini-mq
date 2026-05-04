#include "../include/mq/broker.hpp"
#include "../include/mq/net.hpp"
#include "../include/mq/protocol.hpp"

#include <iostream>
#include <cerrno>
#include <thread>
#include <cstring>

#include <unistd.h>

namespace mq{
    static constexpr uint32_t kMaxTopclen =128;//定义最大主题长度常量
    static constexpr uint32_t kMaxMsglen =1024;//定义最大消息长度常量
    static constexpr uint32_t kMaxFetchMaxMsgs=1000;//定义最大获取消息数量常量

    static void send_error(int cfd,ErrorCode code,const std::string& msg){
        auto payload=encode_error_resp(code,msg);//编码错误响应，包含错误代码和消息
        auto bytes=encode_frame(MessageType::Error,0,payload);//将错误响应编码成一个消息帧
        write_n(cfd,bytes.data(),bytes.size());//将消息帧发送给客户端
    }//发送错误响应给客户端

    static void send_response(int cfd,const std::vector<uint8_t>& payload){
        auto bytes=encode_frame(MessageType::Resp,0,payload);//将响应编码成一个消息帧
        write_n(cfd,bytes.data(),bytes.size());//将消息帧发送给客户端
    }//发送响应给客户端

    static bool validate_topic(const std::string& topic){
        return !topic.empty()&&topic.size()<=kMaxTopclen;//验证主题是否合法，不能为空且长度不超过最大值
    }

    static void handle_frame(int cfd,InmemoryStore& store,const Frame& frame){
        if(frame.type==MessageType::Produce){
            ProduceReq req;
            if(!decode_produce_req(frame,req)){
                send_error(cfd,ErrorCode::BadRequest,"Invalid ProduceReq");
                return;
                //处理生产请求，如果解码失败，发送错误响应给客户端
            }
            if(!validate_topic(req.topic)){
                send_error(cfd,ErrorCode::BadRequest,"Invalid topic");
                return;//验证主题是否合法，如果不合法，发送错误响应给客户端
            }
            if(req.message.size()>kMaxMsglen){
                send_error(cfd,ErrorCode::TooLarge,"Message too large");
                return;//验证消息长度是否合法，如果超过最大值，发送错误响应给客户端
            }

            uint64_t offset = 0;
            {
                std::lock_guard<std::mutex> lock(store.mu);//加锁保护内存存储
                auto& messages=store.topics[req.topic];//获取主题对应的消息列表
                offset=static_cast<uint64_t>(messages.size());//获取当前消息列表的大小作为新消息的偏移量
                messages.push_back(req.message);//将新消息添加到消息列表中
            }
            send_response(cfd,encode_produce_resp(offset));//发送生产响应给客户端，包含新消息的偏移量  
            return ;
        }
        if(frame.type==MessageType::Fetch){
            //处理获取请求的逻辑
            FetchReq req;
            if(!decode_fetch_req(frame,req)){
                send_error(cfd,ErrorCode::BadRequest,"bad FetchReq");
                return;//如果解码失败，发送错误响应给客户端
            }
            if(!validate_topic(req.topic)){
                send_error(cfd,ErrorCode::BadRequest,"Invalid topic");
                return;//验证主题是否合法，如果不合法，发送错误响应给客户端
            }
            if(req.max_msgs==0||req.max_msgs>kMaxFetchMaxMsgs){
                send_error(cfd,ErrorCode::BadRequest,"Invalid max_msgs");
                return;//验证最大消息数量是否合法，如果不合法，发送错误响应给客户端
            }

            std::vector<std::pair<uint64_t,std::string>> out;
            {
                std::lock_guard<std::mutex> lock(store.mu);//加锁保护内存存储
                auto it =store.topics.find(req.topic);//查找主题对应的消息列表
                if(it==store.topics.end()){
                    send_error(cfd,ErrorCode::OffsetOutOfRange,"Topic not found");
                    return;//如果主题不存在，发送错误响应给客户端
                }
                auto& messages=it->second;//获取消息列表
                if(req.offset>messages.size()){
                    send_error(cfd,ErrorCode::OffsetOutOfRange,"Offset out of range");
                    return;//验证偏移量是否合法，如果超过消息列表的大小，发送错误响应给客户端
                }

                uint64_t i=req.offset;
                uint32_t left=req.max_msgs;
                while(i<messages.size()&&left>0){
                    out.emplace_back(i,messages[i]);//将符合条件的消息添加到输出列表中，包含偏移量和消息内容
                    ++i;
                    --left;
                }
            }
            send_response(cfd,encode_fetch_resp(out));//发送获取响应给客户端，包含符合条件的消息列表
            return ;
        }
        send_error(cfd,ErrorCode::UnknownType,"Unknown message type");
        //如果消息类型未知，发送错误响应给客户端
    }

    void handle_connection(int cfd,InmemoryStore& store){
        std::vector<uint8_t> buf;
        buf.reserve(8192);//为缓冲区预留空间
        while(true){
            uint8_t tmp[4096];
            ssize_t n=read(cfd,tmp,sizeof(tmp));//从客户端读取数据到临时缓冲区
            if(n==0) break;//如果读取到EOF，关闭连接
            if(n<0){
                if(errno==EINTR){
                    continue;//如果读取被中断，继续读取
                }
                std::cerr<<"read error: "<<std::strerror(errno)<<std::endl;
                break;//如果读取发生错误，输出错误信息并关闭连接
            }
            buf.insert(buf.end(),tmp,tmp+n);//将读取到的数据添加到缓冲区中

            while(true){
                Frame frame;
                ErrorCode err;
                bool ok=try_decode_one(buf,frame,err);//尝试从缓冲区解码出一个完整的消息帧
                if(!ok){
                    if(err!=ErrorCode::Ok){
                        send_error(cfd,err,"Failed to decode frame");
                    }
                    break;
                }
                handle_frame(cfd,store,frame);//处理解码出的消息帧
            }
        }
    }
}