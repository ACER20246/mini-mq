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


int main(int argc,char *argv[]){
    mq::HostPort hp= mq::parse_host_port(argc,argv,mq::HostPort{"127.0.0.1",9092});//解析命令行参数，获取主机和端口信息
    std::string topic=parse_message(argc,argv,"--topic","test");//解析命令行参数，获取主题信息
    std::string message=parse_message(argc,argv,"--message","Hello, World!"); // 解析命令行参数，获取消息内容
    int fd=mq::connect_socket(hp.host,hp.port);//连接到指定的主机和端口
    if(fd<0){
        std::cerr<<"Failed to connect to broker: "<<std::strerror(errno)<<std::endl;
        return 1;
    }

    //构造消息协议，包含主题和消息内容
    std::vector<uint8_t> payload;
    //将主题长度和主题内容添加到消息协议中
    mq::put_string_u16(payload,topic);
    mq::put_uint32(payload,static_cast<uint32_t>(message.size()));//将消息内容长度添加到消息协议中
    mq::put_bytes(payload,reinterpret_cast<const uint8_t*>(message.data()),message.size());//将消息内容添加到消息协议中

    auto frame=mq::encode_frame(mq::MessageType::Produce,0,payload);//将消息协议编码成一个消息帧


    if(mq::write_n(fd,frame.data(),frame.size())<0){
        std::cerr<<"Failed to send message to broker: "<<std::strerror(errno)<<std::endl;
        ::close(fd);
        return 1;
    }

    std::vector<uint8_t> inbuf;
    inbuf.reserve(4096);
    while(true){
        uint8_t tmp[4096];
        ssize_t n=::read(fd,tmp,sizeof(tmp));
        //从服务器读取响应数据
        if(n==0){
            std::cerr<<"Connection closed by broker"<<std::endl;
            break;
        }//如果读取到的数据长度为0，表示连接被服务器关闭，输出错误信息并退出循环
        if(n<0){
            if(errno==EINTR){
                continue;//如果读取被信号中断，继续尝试读取
            }
            std::cerr<<"Failed to read response from broker: "<<std::strerror(errno)<<std::endl;
            break;
        }///如果读取数据时发生错误，输出错误信息并退出循环

        inbuf.insert(inbuf.end(),tmp,tmp+n);//将读取到的数据追加到输入缓冲区中
        mq::Frame frame;//定义一个消息帧对象，用于存储解码后的消息帧数据
        mq::ErrorCode err;
        if(mq::try_decode_one(inbuf,frame,err)){
            size_t off=0;
            uint64_t offset=0;
            if(frame.type==mq::MessageType::Resp){
                if(mq::get_uint64(frame.payload,off,offset)){
                    std::cout<<"Message produced successfully, offset="<<offset<<std::endl;
                }else{
                    std::cerr<<"Failed to decode response frame"<<std::endl;
                }
            }else if(frame.type==mq::MessageType::Error){
                uint32_t code=0;
                size_t off=0;
                std::string message;
                mq::get_uint32(frame.payload,off,code);
                mq::get_string_u16(frame.payload,off,message);

                std::cerr<<"Error response from broker: code="<<code<<", message="<<message<<std::endl;
            }else{
                std::cerr<<"Unknown response type from broker"<<std::endl;
            }
            break;//成功解码出一个完整的消息帧后，退出循环
        }else if(err!=mq::ErrorCode::Ok){
            std::cerr<<"Failed to decode response frame: error code="<<static_cast<uint32_t>(err)<<std::endl;
            break;//如果解码过程中发生错误，输出错误信息并退出循环
        }
        //如果输入缓冲区中的数据不足以构成一个完整的消息帧，继续等待更多数据的到来
    }
    ::close(fd);//关闭连接
    return 0;
}