#include "../../include/mq/net.hpp"
#include "../../include/mq/protocol.hpp"

#include <iostream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <string>
#include <vector>


static std::string parse_message(int argc, char *argv[],const std::string& key,const std::string& default_value){
    for(int i=1;i<argc;i++){
        std::string a= argv[i];
        if(a==key && i+1<argc){
            return argv[i+1];//解析命令行参数，获取消息内容
        }
    }
    return default_value; // 默认消息
}

static uint32_t parse_u32(int argc,char *argv[],const std::string& key,uint32_t default_value){
    for(int i=1;i<argc;i++){
        if(std::string(argv[i])==key&&i+1<argc){
            return static_cast<uint32_t>(std::stoul(argv[i+1],nullptr,10)); // 解析命令行参数，获取消息内容长度
        }
    }
    return default_value; // 默认值
}

static bool parse_flag(int argc,char *argv[],const std::string& key){
    for(int i=1;i<argc;i++){
        if(std::string(argv[i])==key){
            return true; // 解析命令行参数，获取标志位
        }
    }
    return false; // 默认值
}

static bool decode_produce_offset(const mq::Frame& frame,uint64_t& offset,std::string& err){
    if(frame.type==mq::MessageType::Resp){
        size_t off=0;
        if(!mq::get_uint64(frame.payload,off,offset)){
            err="Failed to decode offset from response frame";
            return false;
        }
        return true;
    }
    if(frame.type==mq::MessageType::Error){
        size_t off=0;
        uint32_t code=0;
        std::string message;
        mq::get_uint32(frame.payload,off,code);
        mq::get_string_u16(frame.payload,off,message);
        err="Error response from broker: code="+std::to_string(code)+", message="+message;
        return false;   
    }
    err="Unknown response type from broker";
    return false;
}

int main(int argc,char *argv[]){
    mq::HostPort hp= mq::parse_host_port(argc,argv,mq::HostPort{"127.0.0.1",9092});//解析命令行参数，获取主机和端口信息
    std::string topic=parse_message(argc,argv,"--topic","test");//解析命令行参数，获取主题信息
    std::string message=parse_message(argc,argv,"--message","Hello, World!"); // 解析命令行参数，获取消息内容
    uint32_t count=parse_u32(argc,argv,"--count",1); // 解析命令行参数，获取消息发送次数
    std::string prefix = parse_message(argc,argv,"--prefix",""); // 解析命令行参数，获取消息前缀
    bool quiet = parse_flag(argc,argv,"--quiet"); // 解析命令行参数，获取是否启用详细输出的标志

    int fd=mq::connect_socket(hp.host,hp.port);//连接到指定的主机和端口
    if(fd<0){
        std::cerr<<"Failed to connect to broker: "<<std::strerror(errno)<<std::endl;
        return 1;
    }
    std::vector<uint8_t> inbuf;
    inbuf.reserve(8192);
    uint64_t last_off=0;//定义一个变量来存储上一次发送消息的偏移量

    for(uint32_t i=0;i<count;i++){
        std::string msg=prefix+message+" #"+std::to_string(i+1);//构造要发送的消息内容，包含前缀、消息内容和消息编号

    //构造消息协议，包含主题和消息内容
        std::vector<uint8_t> payload;
        //将主题长度和主题内容添加到消息协议中
        mq::put_string_u16(payload,topic);
        mq::put_uint32(payload,static_cast<uint32_t>(msg.size()));//将消息内容长度添加到消息协议中
        mq::put_bytes(payload,reinterpret_cast<const uint8_t*>(msg.data()),msg.size());//将消息内容添加到消息协议中

        auto frame=mq::encode_frame(mq::MessageType::Produce,0,payload);//将消息协议编码成一个消息帧


        if(mq::write_n(fd,frame.data(),frame.size())<0){
            std::cerr<<"Failed to send message to broker: "<<std::strerror(errno)<<std::endl;
            ::close(fd);
            return 1;
        }

        while(true){
            mq::Frame f;//定义一个消息帧对象，用于存储解码后的消息帧数据
            mq::ErrorCode error;
            if(mq::try_decode_one(inbuf,f,error)){
                std::string err;
                uint64_t offset=0;
                if(!decode_produce_offset(f,offset,err)){
                    std::cerr<<"produce"<<err<<"\n";
                    ::close(fd);
                    return 1;
                }
                last_off=offset;//更新上一次发送消息的偏移量
                if(!quiet&&count<=20){
                    std::cout<<"producer offset: "<<offset<<std::endl;
                }
                break;//成功解码出一个完整的消息帧后，退出循环
            }
            if(error!=mq::ErrorCode::Ok){
                std::cerr<<"Failed to decode response frame: "<<static_cast<int>(error)<<std::endl;
                ::close(fd);
                return 1;
            }
            uint8_t temp[4096];
            ssize_t n=::read(fd,temp,sizeof(temp));//从套接字中读取数据到缓冲区中
            if(n<0){
                if(errno==EINTR)                continue;//如果读取数据失败且错误是EINTR，继续读取数据
                std::cerr<<"Failed to read from broker: "<<std::strerror(errno)<<std::endl;
            }
            if(n==0){
                std::cerr<<"Broker closed the connection."<<std::endl;
                ::close(fd);
                return 1;
            }
            inbuf.insert(inbuf.end(),temp,temp+n);//将读取到的数据添加到输入缓冲区中，以便后续解码使用
        }
    }

    if (quiet || count > 20) {
        std::cout << "producer: sent " << count << " messages, last_offset=" << last_off << "\n";
    }

    ::close(fd);//关闭连接
    return 0;
}