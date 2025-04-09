REDIS_MODULE_DIR ?= /usr/include/redis

all:
	gcc -fPIC -shared -o archive.so archive.c cJSON.c -I$(REDIS_MODULE_DIR) -lsqlite3

clean:
	rm -f archive.so
