#include "../../include/mq/net.hpp"
#include "../../include/mq/protocol.hpp"

#include <iostream>
#include <string>
#include <vector>
#include <cerrno>
#include <cstring>
#include <thread>
#include <chrono>

#include <unistd.h>
static std::string parse_arg(int argc,char *argv[],const std::string& key,const std::string& default_value){
    for(int i=1;i<argc-1;i++){
        if(std::string(argv[i])==key&&i+1<argc){
            return std::string(argv[i+1]);
        }
    }
    return default_value;
}

static uint64_t parse_uint64(int argc,char *argv[],const std::string& key,uint64_t default_value){
    for(int i=1;i<argc-1;i++){
        if(std::string(argv[i])==key && i+1<argc){
            return static_cast<uint64_t>(std::strtoull(argv[i+1], nullptr, 10));
        }
    }
    return default_value;
}

static bool parse_flag(int argc,char *argv[],const std::string& key){
    for(int i=1;i<argc;i++){
        if(std::string(argv[i])==key){
            return true;
        }
    }
    return false;
}

static uint32_t parse_uint32(int argc,char *argv[],const std::string& key,uint32_t default_value){
    for(int i=1;i<argc-1;i++){
        if(std::string(argv[i])==key && i+1<argc){
            return static_cast<uint32_t>(std::strtoul(argv[i+1], nullptr, 10));
        }
    }
    return default_value;
}

static bool decode_fetch_resp(const mq::Frame& f,std::vector<std::pair<uint64_t,std::string>>& out,std::string& err){
    if(f.type==mq::MessageType::Resp){
        size_t off=0;
        uint32_t msg_count=0;
        if(!mq::get_uint32(f.payload,off,msg_count)){
            err="bad resp";
            return false;
        }
        out.clear();
        out.reserve(msg_count);
        for(uint32_t i=0;i<msg_count;i++){
            uint64_t offv=0;
            uint32_t len=0;
            if(!mq::get_uint64(f.payload,off,offv)||!mq::get_uint32(f.payload,off,len)){
                err="bad item";
                return false;
            }
            if(off+len>f.payload.size()){
                err="bad item len";
                return false;
            }
            std::string msg(reinterpret_cast<const char*>(f.payload.data()+off),len);
            off+=len;
            out.emplace_back(offv,std::move(msg));
        }
        return true;
    }
    if(f.type==mq::MessageType::Error){
        size_t off=0;
        uint32_t code=0;
        std::string msg;
        mq::get_uint32(f.payload,off,code);
        mq::get_string_u16(f.payload,off,msg);
        err="error error "+std::to_string(code)+"msg= "+msg;
        return false;
    }
    err="unexpected message type ";
    return false;
}

int main(int argc,char *argv[]){
    mq::HostPort hp=mq::parse_host_port(argc,argv,mq::HostPort{"127.0.0.1", 9092});
    std::string topic=parse_arg(argc,argv,"--topic","test");
    uint64_t offset=parse_uint64(argc,argv,"--offset",0);
    uint32_t max_msgs=parse_uint32(argc,argv,"--max",100);
    bool follow=parse_flag(argc,argv,"--follow");
    uint32_t interval_ms=parse_uint32(argc,argv,"--interval-ms",200);


    int fd=mq::connect_socket(hp.host,hp.port);
    if(fd<0){
        std::cerr<<"Failed to connect to "<<hp.host<<":"<<hp.port<<" - "<<std::strerror(errno)<<std::endl;
        return 1;
    }

    std::vector<uint8_t> buf;
    buf.reserve(8192);

    while(true){
        std::vector<uint8_t> payload;
        mq::put_string_u16(payload, topic);
        mq::put_uint64(payload, offset);
        mq::put_uint32(payload, max_msgs);

        auto frame=mq::encode_frame(mq::MessageType::Fetch,0,payload);
        if(mq::write_n(fd, frame.data(), frame.size())<0){
            std::cerr<<"Failed to send request - "<<std::strerror(errno)<<std::endl;
            close(fd);
            return 1;
        }

        mq::Frame f;
        mq::ErrorCode err;
        while(!mq::try_decode_one(buf,f,err)){
            if(err!=mq::ErrorCode::Ok){
                std::cerr<<"consumer : decode error"<<std::endl;
                ::close(fd);
                return 1;
            }
            uint8_t tmpbuf[4096];
            ssize_t n=::read(fd,tmpbuf,sizeof(tmpbuf));
            if(n<0){
                if(errno==EINTR){
                    continue;
                }
                std::cerr<<"consumer : read error "<<std::strerror(errno)<<std::endl;
                ::close(fd);
                return 1;
            }
            if(n==0){
                std::cerr<<"consumer : server closed"<<std::endl;
                ::close(fd);
                return 1;
            }
            buf.insert(buf.end(),tmpbuf,tmpbuf+n);
        }
        std::vector<std::pair<uint64_t,std::string>> msgs;
        std::string errstr;
        if(!decode_fetch_resp(f,msgs,errstr)){
            std::cerr<<"consumer : "<<errstr<<std::endl;
            if(!follow) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
            continue;   
            
        }
        for(const auto& p:msgs){
            std::cout<<p.first<<": "<<p.second<<std::endl;
        }
        if(!msgs.empty()){
            offset=msgs.back().first+1;
        }else{
            if(!follow) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
    }
    ::close(fd);
    return 0;
}