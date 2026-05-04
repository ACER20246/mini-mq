#include "../include/mq/protocol.hpp"
#include <cstring>
#include <arpa/inet.h>
namespace mq{
    static void put_raw(std::vector<uint8_t>&b,const void*p,size_t n){
        const auto* u=static_cast<const uint8_t*>(p);
        b.insert(b.end(),u,u+n);
        //这段代码的作用是将一个原始内存块的内容写入到一个字节向量中。
        //函数put_raw接受三个参数：一个字节向量b，一个指向原始内存块的指针p，以及内存块的大小n（以字节为单位）。
        //函数首先将指针p转换为一个指向uint8_t类型的指针u，然后使用insert方法将内存块的内容插入到字节向量b的末尾。
    }

    void put_uint16(std::vector<uint8_t>& buf,uint16_t v){
        uint16_t be=htons(v);//htons函数将一个16位无符号整数从主机字节序转换为网络字节序（大端字节序）
        put_raw(buf,&be,sizeof(be));
    }
    void put_uint32(std::vector<uint8_t>& buf,uint32_t v){
        uint32_t be=htonl(v);//htonl函数将一个32位无符号整数从主机字节序转换为网络字节序（大端字节序）
        put_raw(buf,&be,sizeof(be));
    }
    void put_uint64(std::vector<uint8_t>& buf,uint64_t v){
        uint32_t high=static_cast<uint32_t>(v>>32);
        uint32_t low=static_cast<uint32_t>(v&0xFFFFFFFFu);
        put_uint32(buf,high);
        put_uint32(buf,low);
    }//put_uint64函数将一个64位无符号整数从主机字节序转换为网络字节序（大端字节序），并将其写入到字节向量中。
    void put_bytes(std::vector<uint8_t>& buf,const uint8_t* p,size_t n){
        buf.insert(buf.end(),p,p+n); 
    }
    void put_string_u16(std::vector<uint8_t>& buf,const std::string& s){
        put_uint16(buf,static_cast<uint16_t>(s.size()));
        put_bytes(buf,reinterpret_cast<const uint8_t*>(s.data()),s.size());
        //put_string_u16函数将一个字符串以16位长度前缀的形式写入到字节向量中。
        //首先，它将字符串的长度作为一个16位无符号整数写入到字节向量中
        //然后将字符串的内容作为字节数据写入到字节向量中。
    }

    bool get_uint16(const std::vector<uint8_t>&buf,size_t& off,uint16_t& v){
        if(off+sizeof(v)>buf.size()) return false;
        v=ntohs(*reinterpret_cast<const uint16_t*>(buf.data()+off));
        off+=sizeof(v);
        return true;
    }//get_uint16函数从字节向量中读取一个16位无符号整数，使用大端字节序。
    bool get_uint32(const std::vector<uint8_t>&buf,size_t& off,uint32_t& v){
        if(off+4>buf.size()) return false;
        uint32_t x;
        std::memcpy(&x,buf.data()+off,4);
        off+=4;
        v=ntohl(x);
        return true;
    }
    //
    bool get_uint64(const std::vector<uint8_t>& buf,size_t&off,uint64_t& v){
        uint32_t high,low;
        if(!get_uint32(buf,off,high)) return false;
        if(!get_uint32(buf,off,low))  return false;
        v=(static_cast<uint64_t>(high)<<32)|low;
        //首先，将高32位左移32位，然后使用按位或运算符将其与低32位进行组合，得到最终的64位整数。
        //get_uint64函数从字节向量中读取一个64位无符号整数，使用大端字节序。它首先读取高32位和低32位，然后将它们组合成一个64位整数。
        return true;
    }
    bool get_bytes(const std::vector<uint8_t>& buf,size_t& off,std::vector<uint8_t>& bytes,size_t n){
        if(off+n>buf.size()) return false;
        bytes.insert(bytes.end(),buf.begin()+off,buf.begin()+off+n);
        off+=n;
        return true;
    }
    bool get_string_u16(const std::vector<uint8_t>& buf,size_t& off,std::string& s){
        uint16_t len;
        if(!get_uint16(buf,off,len)) return false;
        if(off+len>buf.size()) return false;
        s.assign(buf.begin()+off,buf.begin()+off+len);
        off+=len;
        return true;    
    }
    //get_string_u16函数从字节向量中读取一个以16位长度前缀的字符串。
    //它首先读取字符串的长度，然后根据长度读取字符串的内容，并将其赋值给参数s。
    
    std::vector<uint8_t> encode_frame(MessageType type, uint16_t flags, const std::vector<uint8_t>& payload) {
        std::vector<uint8_t> out;
        out.reserve(4 + 2 + 2 + payload.size());

        uint32_t len = static_cast<uint32_t>(2 + 2 + payload.size());
        put_uint32(out, len);
        put_uint16(out, static_cast<uint16_t>(type));
        put_uint16(out, flags);
        out.insert(out.end(), payload.begin(), payload.end());
        return out;
    }

    bool try_decode_one(std::vector<uint8_t>& inbuf,Frame& out,ErrorCode& err){
        err = ErrorCode::Ok;//初始化错误代码为Ok，表示没有错误
        if(inbuf.size()<4) return false;//如果输入缓冲区的大小小于4字节，说明无法读取消息帧的长度，因此返回false
        size_t off =0;
        uint32_t len=0;
        if(!get_uint32(inbuf,off,len)) return false;//尝试从输入缓冲区中读取一个32位无符号整数作为消息帧的长度，如果读取
        if(len>kMaxFramelen){
            err=ErrorCode::TooLarge;
            inbuf.clear();
            return false;
        }
        if(inbuf.size()<4+len) return false;
        //byte不足
        std::vector<uint8_t> frame_bytes(inbuf.begin()+4,inbuf.begin()+4+len);
        size_t off2=0;
        //从输入缓冲区中提取消息帧的字节数据，并将其存储在frame_bytes向量中。
        //off2变量用于跟踪在frame_bytes中的当前偏移位置。
        uint16_t t=0,flags=0;
        if(!get_uint16(frame_bytes,off2,t)||!get_uint16(frame_bytes,off2,flags)){
            err=ErrorCode::BadRequest;
            inbuf.clear();
            return false;
        }//尝试从frame_bytes中读取消息类型和标志，
        //如果读取失败，说明消息帧格式不正确，设置错误代码为BadRequest，并清空输入缓冲区，返回false。
        out.type=static_cast<MessageType>(t);
        out.flags=flags;
        out.payload.assign(frame_bytes.begin()+off2,frame_bytes.end());
        //将读取到的消息类型和标志赋值给输出参数out，
        //并将剩余的字节数据作为负载数据存储在out.payload中。
        inbuf.erase(inbuf.begin(),inbuf.begin()+4+len);
        //从输入缓冲区中删除已经处理的消息帧数据，以便下一次尝试解码时能够正确处理新的数据。
        return true;
    }
    bool decode_produce_req(const Frame& frame,ProduceReq& out)
    {
        if(frame.type!=MessageType::Produce) return false;
        size_t off=0;
        if(!get_string_u16(frame.payload,off,out.topic)) return false;
        uint32_t n=0;
        if(!get_uint32(frame.payload,off,n)) return false;
        if(off+n>frame.payload.size()) return false;
        out.message.assign(reinterpret_cast<const char*>(frame.payload.data()+off), n);
        off+=n;
        return true;
    }
    //decode_produce_req函数将一个消息帧解码成一个生产请求对象。它首先检查消息帧的类型是否为MessageType::Produce，如果不是，则返回false。
    //然后，它从消息帧的负载中读取主题和消息内容，并将它们存储在ProduceReq结构体中。如果解码过程中发生任何错误，例如负载
    //数据不足以构成一个完整的生产请求，则返回false。

    bool decode_fetch_req(const Frame& frame,FetchReq& out){
        if(frame.type!=MessageType::Fetch) return false;
        size_t off=0;
        if(!get_string_u16(frame.payload,off,out.topic)) return false;
        if(!get_uint64(frame.payload,off,out.offset)) return false;
        if(!get_uint32(frame.payload,off,out.max_msgs)) return false;
        return true;
    }
    //decode_fetch_req函数将一个消息帧解码成一个获取请求对象。它首先检查消息帧的类型是否为MessageType::Fetch，如果不是，则返回false。
    //然后，它从消息帧的负载中读取主题、偏移量和最大字节数，并将它们存储在FetchReq结构体中。如果解码过程中发生任何错误，例如负载数据不足以构成一个完整的获取请求，则返回false。
    //decode_fetch_req函数的作用是将一个消息帧解码成一个获取请求对象。它首先检查消息帧的类型是否为MessageType::Fetch，如果不是，则返回false。
    //然后，它从消息帧的负载中读取主题、偏移量和最大字节数，并将它们存储在FetchReq结构体中。如果解码过程中发生任何错误，例如负载数据不足以构成一个完整的获取请求，则返回false。 

    std::vector<uint8_t> encode_produce_resp(uint64_t offset){
        std::vector<uint8_t> payload;
        put_uint64(payload,offset);
        return payload;
    }//encode_produce_resp函数将一个生产响应编码成一个消息帧。它首先创建一个字节向量payload，并将偏移量写入到其中。
    //然后，它调用encode_frame函数将消息类型设置为MessageType::Resp，标志设置为0，负载设置为payload，
    //并返回编码后的消息帧字节向量。

    std::vector<uint8_t> encode_fetch_resp(const std::vector<std::pair<uint64_t,std::string>>& messages){
        std::vector<uint8_t> payload;
        put_uint32(payload,static_cast<uint32_t>(messages.size()));
        for(const auto& msg : messages){
            put_uint64(payload,msg.first);
            put_uint32(payload,static_cast<uint32_t>(msg.second.size()));
            put_bytes(payload,reinterpret_cast<const uint8_t*>(msg.second.data()),msg.second.size());
        }
        return payload;
    }
    //encode_fetch_resp函数将一个获取响应编码成一个消息帧。它首先创建一个字节向量payload，并将消息的数量写入到其中。
    //然后，它遍历消息列表，对于每个消息，将其偏移量和内容写入到payload中。最后，它调用encode_frame函数将消息类型设置为MessageType::Resp，标志设置为0，负载设置为payload，并返回编码后的消息帧字节向量。

    std::vector<uint8_t> encode_error_resp(ErrorCode code,const std::string& message){
        std::vector<uint8_t> payload;
        put_uint32(payload,static_cast<uint32_t>(code));
        put_string_u16(payload,message);
        return payload;
    }
    //encode_error_resp函数将一个错误响应编码成一个消息帧。它首先创建一个字节向量payload，并将错误代码写入到其中。
    //然后，它将错误消息写入到payload中。最后，它调用encode_frame函数将消息类型设置为MessageType::Error，标志设置为0，负载设置为payload，并返回编码后的消息帧字节向量。
}