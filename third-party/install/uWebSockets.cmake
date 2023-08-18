macro(install_uWebSockets)

    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLIBUS_NO_SSL")

    include_directories(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src)
    include_directories(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/src)

    aux_source_directory(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src USOCKETS_FILES)
    aux_source_directory(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/crypto USOCKETS_CRYPTO_FILES)
    aux_source_directory(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/eventing USOCKETS_EVENTING_FILES)
    aux_source_directory(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/internal USOCKETS_INTERNAL_FILES)
    aux_source_directory(${CMAKE_SOURCE_DIR}/third-party/uWebSockets/uSockets/src/io_uring USOCKETS_IO_URING_FILES)

    add_library(uSockets
            ${USOCKETS_FILES}
            ${USOCKETS_CRYPTO_FILES}
            ${USOCKETS_EVENTING_FILES}
            ${USOCKETS_INTERNAL_FILES}
            ${USOCKETS_IO_URING_FILES})

endmacro()

