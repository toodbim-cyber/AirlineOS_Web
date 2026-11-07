FROM ubuntu:24.04

RUN apt-get update && apt-get install -y \
    g++ \
    make \
    libasio-dev \
    libsqlite3-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN g++ -std=c++17 -O2 -pthread -I. \
    AirlineOS_web_enhanced.cpp \
    -o AirlineOS_Web \
    -lsqlite3

EXPOSE 8080

CMD ["./AirlineOS_Web"]
