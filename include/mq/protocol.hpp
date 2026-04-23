#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mq{
    enum class MessageType : std::int32_t {
        Produce = 1,
        Fetch = 2,
        Resp = 100,
        Error = 101
    };
    // MessageType枚举类定义了消息类型，包括Produce、Fetch、Resp和Error，每个类型对应一个整数值
    enum class ErrorCode : std::int32_t{
        Ok=0,
        BadRequest=1,
        TooLarge=2,
        UnknownType=3,
        OffsetOutOfRange=4,
    };
    // ErrorCode枚举类定义了错误代码，包括Ok、BadRequest、TooLarge、UnknownType和OffsetOutOfRange，每个代码对应一个整数值

    struct Frame{
        MessageType type{};
        uint16_t flags{};
        std::vector<uint8_t> payload;
    };
    // Frame结构体表示一个消息帧，包含消息类型、标志和负载数据。消息类型使用MessageType枚举，标志是一个16位无符号整数，负载是一个字节向量

    constexpr uint32_t kMaxFramelen=1024*1024*4;
    // kMaxFramelen常量定义了消息帧的最大长度，设置为4MB

    std::vector<uint8_t> encode_frame(MessageType type,uint16_t flags, const std::vector<uint8_t>& payload);
    // encode_frame函数将消息类型和负载编码成一个字节向量，用于发送给客户端或服务器
    // 该函数将消息类型转换为4字节整数，并将其与负载数据一起打包成一个完整的消息帧


    bool try_decode_one(std::vector<uint8_t>& inbuf,Frame& out,ErrorCode& err);
    // try_decode_one函数尝试从输入缓冲区解码出一个完整的消息帧。如果成功解码出一个完整的帧，则将其存储在out参数中，并返回true。如果输入缓冲区中的数据不足以构成一个完整的
    // 消息帧，则返回false，并将错误代码设置为ErrorCode::BadRequest。如果解码过程中发生其他错误，则将错误代码设置为相应的错误类型

    void put_uint32(std::vector<uint8_t>& buf,uint32_t v);
    // 将一个32位无符号整数以大端字节序的形式写入到字节向量中
    void put_uint16(std::vector<uint8_t>& buf,uint16_t v);
    // 将一个16位无符号整数以大端字节序的形式写入到字节向量中
    void put_uint64(std::vector<uint8_t>& buf,uint64_t v);
    // 将一个64位无符号整数以大端字节序的形式写入到字节向量中
    void put_bytes(std::vector<uint8_t>& buf,const uint8_t* p,size_t n);
    // 将一个字节向量的内容写入到另一个字节向量中
    void put_string_u16(std::vector<uint8_t>& buf,const std::string& s);
    // 将一个字符串以16位长度前缀的形式写入到字节向量中




    bool get_uint32(const std::vector<uint8_t>& buf,size_t& off,uint32_t& v);
    // 从字节向量中读取一个32位无符号整数，使用大端字节序
    bool get_uint16(const std::vector<uint8_t>& buf,size_t& off,uint16_t& v);
    // 从字节向量中读取一个16位无符号整数，使用大端字节序
    bool get_uint64(const std::vector<uint8_t>& buf,size_t& off,uint64_t& v);
    // 从字节向量中读取一个64位无符号整数，使用大端字节序
    bool get_bytes(const std::vector<uint8_t>& buf,size_t& off,std::vector<uint8_t>& bytes,size_t n);
    // 从字节向量中读取指定长度的字节数据，并将其存储在另一个字节向量中 
    bool get_string_u16(const std::vector<uint8_t>& buf,size_t& off,std::string& s);
    // 从字节向量中读取一个以16位长度前缀的字符串，并将其存储在一个字符串变量中



    struct ProduceReq{
        std::string topic;
        std::string message;
    };
    // ProduceReq结构体表示一个生产请求，包含主题和消息内容。主题是一个字符串，消息内容也是一个字符串

    struct FetchReq{
        std::string topic;
        uint64_t offset{0};
        uint32_t max_bytes{100};
    };
    // FetchReq结构体表示一个获取请求，包含主题和偏移量。主题是一个字符串，
    //偏移量是一个64位无符号整数，表示从哪个位置开始获取消息
    // max_bytes是一个32位无符号整数，表示一次获取的最大字节数

    bool decode_produce_req(const Frame& frame,ProduceReq& out);//生产请求
    // decode_produce_req函数将一个消息帧解码成一个生产请求对象
    // 该函数首先检查消息帧的类型是否为MessageType::Produce，如果不是，则返回false。
    //然后，它从消息帧的负载中读取主题和消息内容，并将它们存储在ProduceReq结构体中。
    //如果解码过程中发生任何错误，例如负载数据不足以构成一个完整的生产请求，则返回false

    bool decode_fetch_req(const Frame& frame,FetchReq& out);//获取请求
    //decode_fetch_req函数将一个消息帧解码成一个获取请求对象
    //该函数首先检查消息帧的类型是否为MessageType::Fetch，如果不是，则返回false。
    //然后，它从消息帧的负载中读取主题、偏移量和最大字节数，并将它们存储在FetchReq结构体中。
    //如果解码过程中发生任何错误，例如负载数据不足以构成一个完整的获取请求，则返回false

    std::vector<uint8_t> encode_produce_resp(uint64_t offset);
    // encode_produce_resp函数将一个生产响应编码成一个字节向量
    //该函数将生产响应的偏移量作为参数，并将其编码成一个消息帧的负载数据。消息帧的类型设置为MessageType::Resp，
    //标志设置为0，负载数据包含一个64位无符号整数，表示生产消息的偏移量。最终返回一个完整的消息帧的字节向量
    std::vector<uint8_t> encode_fetch_resp(const std::vector<std::pair<uint64_t,std::string>>& messages);
    // encode_fetch_resp函数将一个获取响应编码成一个字节向量
    //该函数将获取响应的消息列表作为参数，并将其编码成一个消息帧的负载数据。消息帧的类型设置为MessageType::Resp，
    //标志设置为0，负载数据包含一个32位无符号整数，表示消息的数量，后跟每个消息的长度和内容。最终返回一个完整的消息帧的字节向量

    std::vector<uint8_t> encode_error_resp(ErrorCode code,const std::string& message);
    // encode_error_resp函数将一个错误响应编码成一个字节向量
    //该函数将错误响应的错误代码和错误消息作为参数，并将它们编码成一个消息帧的负载数据。消息帧的类型设置为MessageType::Error，标志设置为0，负载数据包含一个32位无符号整数，表示错误代码，后跟一个以16位长度前缀的字符串，表示错误消息。最终返回一个完整的消息帧的字节向量
}//namespace mq