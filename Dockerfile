FROM redis:7-alpine

LABEL maintainer="WebServer Developer"
LABEL description="C++ WebServer with Redis support"

WORKDIR /app

RUN apk update && apk add --no-cache \
    build-base \
    cmake \
    pkgconf \
    hiredis-dev \
    && rm -rf /var/cache/apk/*

COPY . .

RUN mkdir -p build && \
    cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && \
    cd ..

EXPOSE 8080 6379

ENV REDIS_HOST=127.0.0.1
ENV REDIS_PORT=6379
ENV SERVER_PORT=8080

CMD redis-server --daemonize yes && sleep 1 && ./build/ChatServer -p ${SERVER_PORT}
