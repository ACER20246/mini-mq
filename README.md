# mini-mq

A learning project: build a minimal message queue in C++ on Linux.

## Build

```bash
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

## Run (Week 1)

Start broker:

```bash
./build/broker --host 0.0.0.0 --port 9092
```

Run producer:

```bash
./build/producer --host 127.0.0.1 --port 9092 --message "hello"
```

Expected output: producer prints `OK`.