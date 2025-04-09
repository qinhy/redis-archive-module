FROM debian:bullseye

RUN apt-get update && apt-get install -y \
    build-essential \
    libsqlite3-dev \
    wget \
    git \
    tcl \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt
RUN git clone https://github.com/redis/redis.git && \
    cd redis && \
    git checkout 7.4.2 && \
    make -j$(nproc) && make install

RUN mkdir -p /usr/include/redis && cp redis/src/*.h /usr/include/redis/

WORKDIR /module

COPY . .

RUN make

EXPOSE 6379
CMD ["redis-server","redis.conf"]

# Expose archive storage to host
VOLUME ["/data"]
