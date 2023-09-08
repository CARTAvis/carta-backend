macro(install_uWebSockets)
    INCLUDE(FetchContent)

    # Build uSocket
    SET(USOCKETS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third-party/uSockets)

    FetchContent_Declare(
            uSockets-build
            URL https://github.com/uNetworking/uSockets/archive/refs/tags/v0.8.6.zip
            SOURCE_DIR ${USOCKETS_SOURCE_DIR}
    )

    FetchContent_GetProperties(uSockets-build)
    if (NOT uSockets-build_POPULATED)
        FetchContent_Populate(uSockets-build)
    endif ()

    INCLUDE_DIRECTORIES(${USOCKETS_SOURCE_DIR}/src)

    AUX_SOURCE_DIRECTORY(${USOCKETS_SOURCE_DIR}/src USOCKETS_FILES)
    AUX_SOURCE_DIRECTORY(${USOCKETS_SOURCE_DIR}/src/crypto USOCKETS_CRYPTO_FILES)
    AUX_SOURCE_DIRECTORY(${USOCKETS_SOURCE_DIR}/src/eventing USOCKETS_EVENTING_FILES)
    AUX_SOURCE_DIRECTORY(${USOCKETS_SOURCE_DIR}/src/internal USOCKETS_INTERNAL_FILES)
    AUX_SOURCE_DIRECTORY(${USOCKETS_SOURCE_DIR}/src/io_uring USOCKETS_IO_URING_FILES)

    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -DLIBUS_NO_SSL")

    ADD_LIBRARY(uSockets
            ${USOCKETS_FILES}
            ${USOCKETS_CRYPTO_FILES}
            ${USOCKETS_EVENTING_FILES}
            ${USOCKETS_INTERNAL_FILES}
            ${USOCKETS_IO_URING_FILES})

    # Build uWebSockets
    SET(UWEBSOCKETS_SOURCE_DIR ${CMAKE_SOURCE_DIR}/third-party/uWebSockets)

    FetchContent_Declare(
            uWebSockets-build
            URL https://github.com/uNetworking/uWebSockets/archive/refs/tags/v20.46.0.zip
            SOURCE_DIR ${UWEBSOCKETS_SOURCE_DIR}
    )

    FetchContent_GetProperties(uWebSockets-build)
    if (NOT uWebSockets-build_POPULATED)
        FetchContent_Populate(uWebSockets-build)
    endif ()

    INCLUDE_DIRECTORIES(${UWEBSOCKETS_SOURCE_DIR}/src)

endmacro()
