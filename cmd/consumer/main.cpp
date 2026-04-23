#include "../../include/mq/net.hpp"
#include "../../include/mq/protocol.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <cerrno>
#include <cstring>

#include <unistd.h>
static std::string parse_arg(int argc,char *argv[],const std::string& key,const std::string& default_value){
    for(int i=1;i<argc-1;i++){
        if(argv[i]==key&&i+1<argc){
            return argv[i+1];
        }
    }
    return default_value;
}

static uint64_t parse_uint64(int argc,char *argv[],const std::string& key,uint64_t default_value){
    for(int i=1;i<argc-1;i++){
        if(argv[i]==key && i+1<argc){
            return static_cast<uint64_t>(std::strtoull(argv[i+1], nullptr, 10));
        }
    }
    return default_value;
}

static uint32_t parse_uint32(int argc,char *argv[],const std::string& key,uint32_t default_value){
    for(int i=1;i<argc-1;i++){
        if(argv[i]==key && i+1<argc){
            return static_cast<uint32_t>(std::strtoul(argv[i+1], nullptr, 10));
        }
    }
    return default_value;
}

int main(int argc,char *argv[]){
    mq::HostPort hp=mq::parse_host_port(argc,argv,mq::HostPort{"127.0.0.1", 9092});
    std::string topic=parse_arg(argc,argv,"--topic","test");
    uint64_t offset=parse_uint64(argc,argv,"--offset",0);
    uint32_t max_bytes=parse_uint32(argc,argv,"--max",100);

    int fd=mq::connect_socket(hp.host,hp.port);
    if(fd<0){
        std::cerr<<"Failed to connect to "<<hp.host<<":"<<hp.port<<" - "<<std::strerror(errno)<<std::endl;
        return 1;
    }

    std::vector<uint8_t> payload;
    mq::put_string_u16(payload, topic);
    mq::put_uint64(payload, offset);
    mq::put_uint32(payload, max_bytes);

    auto frame=mq::encode_frame(mq::MessageType::Fetch,0,payload);
    if(mq::write_n(fd, frame.data(), frame.size())<0){
        std::cerr<<"Failed to send request - "<<std::strerror(errno)<<std::endl;
        close(fd);
        return 1;
    }

    std::vector<uint8_t> inbuf;
    inbuf.reserve(1024);

    while(true){
        uint8_t tmp[4096];
        ssize_t n=::read(fd, tmp, sizeof(tmp));

        if(n==0){
            std::cerr<<"Connection closed by server"<<std::endl;
            break;
        }

        if(n<0){
            if(errno==EINTR){
                continue;
            }
            std::cerr<<"Failed to read from socket - "<<std::strerror(errno)<<std::endl;
            break;
        }
        inbuf.insert(inbuf.end(), tmp, tmp + n);

        mq::Frame frame;
        mq::ErrorCode err;
        if(mq::try_decode_one(inbuf,frame,err)){
            if(frame.type==mq::MessageType::Resp){
                size_t off=0;
                uint32_t msg_count=0;
                if(!mq::get_uint32(frame.payload, off, msg_count)){
                    std::cerr<<"Failed to parse response: invalid message count"<<std::endl;
                    break;
                }
                for(uint32_t i=0;i<msg_count;i++){
                    uint64_t offv;
                    uint32_t len;
                    if(!mq::get_uint64(frame.payload, off, offv) || !mq::get_uint32(frame.payload, off, len)){
                        std::cerr<<"Failed to parse response: invalid message format"<<std::endl;
                        break;
                    }
                    if(off+len>frame.payload.size()){
                        std::cerr<<"Failed to parse response: message length exceeds payload"<<std::endl;
                        break;
                    }
                    std::string msg(reinterpret_cast<const char*>(frame.payload.data() + off), len);
                    off+=len;
                    std::cout<<"Received message at offset "<<offv<<": "<<msg<<std::endl;
                }
            }else if(frame.type==mq::MessageType::Error){
                size_t off=0;
                uint32_t code;
                std::string msg2;
                mq::get_uint32(frame.payload, off, code);
                mq::get_string_u16(frame.payload, off, msg2);
                std::cerr<<"Received error from server: "<<code<<" - "<<msg2<<std::endl;
            }
            break;
        }else if(err!=mq::ErrorCode::Ok){
            std::cerr<<"Failed to decode frame: "<<static_cast<int>(err)<<std::endl;
            break;
        }
    }
    ::close(fd);
    return 0;
}