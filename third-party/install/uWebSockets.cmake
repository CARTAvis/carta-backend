macro(install_uWebSockets)

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLIBUS_NO_SSL")

    include_directories(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src)
    include_directories(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/src)

    set(USOCKETS_SOURCE_FILES
            ${SOURCE_FILES}
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/crypto/openssl.c
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/crypto/wolfssl.c
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/eventing/epoll_kqueue.c
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/eventing/gcd.c
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/eventing/libuv.c
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/bsd.c
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/context.c
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/loop.c
            ${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/socket.c)

    add_library(uSockets ${USOCKETS_SOURCE_FILES})

endmacro()

