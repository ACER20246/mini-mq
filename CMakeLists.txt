cmake_minimum_required(VERSION 3.16)
project(mini_mq LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(mq
  src/net.cpp
  src/protocol.cpp
)

target_include_directories(mq PUBLIC include)

add_executable(broker cmd/broker/main.cpp)
target_link_libraries(broker PRIVATE mq)

add_executable(producer cmd/producer/main.cpp)
target_link_libraries(producer PRIVATE mq)

add_executable(consumer cmd/consumer/main.cpp)
target_link_libraries(consumer PRIVATE mq)