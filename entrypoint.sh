#!/bin/bash

# Check if archive.so exists in the current directory
if [ ! -f "archive.so" ]; then
    echo "archive.so not found in current directory, copying from /module/archive.so"
    cp /module/archive.so .
fi

# Check if redis.conf exists in the current directory
if [ ! -f "redis.conf" ]; then
    echo "redis.conf not found in current directory, copying from /module/redis.conf"
    cp /module/redis.conf .
fi

# Start Redis server with the module
redis-server "redis.conf"