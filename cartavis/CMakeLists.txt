FIND_PACKAGE(Protobuf REQUIRED)
INCLUDE_DIRECTORIES(${PROTOBUF_INCLUDE_DIR})

#PROTOBUF_GENERATE_CPP(PROTO_SRC PROTO_HEADER carta_service.proto)

find_program(PROTOC protoc)
find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)

# Proto file
get_filename_component(cartavis_proto "./carta_service.proto" ABSOLUTE)
get_filename_component(cartavis_proto_path "${cartavis_proto}" PATH)
# Generated sources
set(proto_srcs "${CMAKE_BINARY_DIR}/cartavis/carta_service.pb.cc")
set(proto_hdrs "${CMAKE_BINARY_DIR}/cartavis/carta_service.pb.h")
set(grpc_srcs "${CMAKE_BINARY_DIR}/cartavis/carta_service.grpc.pb.cc")
set(grpc_hdrs "${CMAKE_BINARY_DIR}/cartavis/carta_service.grpc.pb.h")

add_custom_command(
      OUTPUT "${proto_srcs}" "${proto_hdrs}" "${grpc_srcs}" "${grpc_hdrs}"
      COMMAND ${PROTOC}
      ARGS --grpc_out "${CMAKE_BINARY_DIR}/cartavis"
        --cpp_out "${CMAKE_BINARY_DIR}/cartavis"
        -I "${cartavis_proto_path}"
        --plugin=protoc-gen-grpc="${GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${cartavis_proto}"
      DEPENDS "${cartavis_proto}")

ADD_LIBRARY(cartavis ${proto_hdrs} ${proto_srcs} ${grpc_hdrs} ${grpc_srcs})
