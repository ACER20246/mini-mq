#pragma once

#include "protocol.hpp"

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <unordered_map>

namespace mq {
    struct InmemoryStore{
        std::mutex mu;
        std::unordered_map<std::string, std::vector<std::string>> topics;
    };

    void handle_connection(int cfd,InmemoryStore& store);
    //解释：这是一个消息队列（Message Queue）的头文件，定义了一个名为`InmemoryStore`的结构体和一个函数`handle_connection`。
    //1. `InmemoryStore`结构体包含一个互斥锁（`std::mutex mu`）和一个哈希表（`std::unordered_map<std::string, std::vector<std::string>> topics`）。
    //这个结构体用于存储消息队列中的主题（topics）和对应的消息列表。
    // Handle one TCP connection (long-lived).
    // This function reads frames, decodes them, processes Produce/Fetch, and writes responses.
    // It returns when the client closes the connection or on fatal error.
}
