macro(install_uWebSockets)

    set(CMAKE_C_FLAGS "-O3 -DLIBUS_NO_SSL")

    include_directories(/usr/local/include)
    link_directories(/usr/local/lib)
    link_directories(/usr/local/opt/openssl/lib)

    include_directories(${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src)
    include_directories(${CMAKE_SOURCE_DIR}/uWebSockets/src)

    set(USOCKETS_SOURCE_FILES
            ${SOURCE_FILES}
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/crypto/openssl.c
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/crypto/wolfssl.c
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/eventing/epoll_kqueue.c
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/eventing/gcd.c
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/eventing/libuv.c
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/bsd.c
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/context.c
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/loop.c
            ${CMAKE_SOURCE_DIR}/uWebSockets/uSockets/src/socket.c)

    add_library(uSockets ${USOCKETS_SOURCE_FILES})

endmacro()

