FROM ubuntu:18.04

RUN apt-get update &&    \
    apt-get install -y   \
    build-essential      \
    libcurl4-openssl-dev \
    libmysqlclient-dev   \
    valgrind             \
    clang-tools-6.0      \
    && apt-get clean

ADD src /src

# Debug build
RUN cc -g -O0 /src/b52.c -o /b52-dbg -I/usr/include/mysql/ -lmysqlclient -L/usr/lib/x86_64-linux-gnu -lcurl

# Prod build
RUN cc src/b52.c -o /b52 -I/usr/include/mysql/ -lmysqlclient -L/usr/lib/x86_64-linux-gnu -lcurl

ENTRYPOINT tail -f /dev/null
