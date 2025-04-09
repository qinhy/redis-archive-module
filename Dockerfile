FROM debian:bullseye

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    libsqlite3-dev \
    wget \
    git \
    tcl \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Build and install Redis
WORKDIR /opt
RUN git clone https://github.com/redis/redis.git && \
    cd redis && \
    git checkout 7.4.2 && \
    make -j$(nproc) && \
    make install

# Copy Redis headers for module development
RUN mkdir -p /usr/include/redis && \
    cp redis/src/*.h /usr/include/redis/

# Set up module directory
WORKDIR /module
COPY . .
RUN make

# Configure container
WORKDIR /data
EXPOSE 6379
VOLUME ["/data"]
CMD ["/module/entrypoint.sh"]
