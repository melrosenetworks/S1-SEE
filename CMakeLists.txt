cmake_minimum_required(VERSION 3.20)
project(S1-SEE VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find dependencies
# Note: There may be conflicts between protobuf 6.x and gRPC
# If you encounter errors, try: cmake .. -Dprotobuf_MODULE_COMPATIBLE=ON
find_package(Protobuf REQUIRED)

# Load workaround for protobuf/gRPC conflicts
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProtobufWorkaround)

find_package(gRPC REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(Threads REQUIRED)

# Find libpcap for PCAP file reading (optional)
# Try common locations including Homebrew
find_library(PCAP_LIBRARY 
    NAMES pcap
    PATHS 
        /opt/homebrew/lib
        /opt/homebrew/Cellar/libpcap/*/lib
        /usr/local/lib
        /usr/lib
)
find_path(PCAP_INCLUDE_DIR 
    NAMES pcap/pcap.h
    PATHS
        /opt/homebrew/include
        /opt/homebrew/Cellar/libpcap/*/include
        /usr/local/include
        /usr/include
)
if(PCAP_LIBRARY AND PCAP_INCLUDE_DIR)
    message(STATUS "Found libpcap: ${PCAP_LIBRARY}")
    message(STATUS "Found pcap headers: ${PCAP_INCLUDE_DIR}")
    set(HAVE_PCAP TRUE)
    add_definitions(-DHAVE_PCAP)
else()
    message(WARNING "libpcap not found. PCAP tests will be disabled.")
    message(WARNING "  Install with: brew install libpcap (macOS) or apt-get install libpcap-dev (Linux)")
    set(HAVE_PCAP FALSE)
endif()

# Protobuf generation
set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILES
    ${PROTO_DIR}/signal_message.proto
    ${PROTO_DIR}/spool_record.proto
    ${PROTO_DIR}/canonical_message.proto
    ${PROTO_DIR}/event.proto
    ${PROTO_DIR}/ingest.proto
)

set(PROTO_SRCS)
set(PROTO_HDRS)
foreach(PROTO_FILE ${PROTO_FILES})
    get_filename_component(PROTO_NAME ${PROTO_FILE} NAME_WE)
    list(APPEND PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.cc")
    list(APPEND PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.pb.h")
    list(APPEND PROTO_GRPC_SRCS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.cc")
    list(APPEND PROTO_GRPC_HDRS "${CMAKE_CURRENT_BINARY_DIR}/${PROTO_NAME}.grpc.pb.h")
endforeach()

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${PROTO_GRPC_SRCS} ${PROTO_GRPC_HDRS}
    COMMAND protobuf::protoc
    ARGS --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
         --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         -I${PROTO_DIR}
         ${PROTO_FILES}
    DEPENDS ${PROTO_FILES}
    COMMENT "Generating protobuf and gRPC files"
)

# Core library
add_library(s1see_core STATIC
    src/spool/wal_log.cc
    src/spool/spool.cc
    src/ingest/ingest_adapter.cc
    src/ingest/grpc_adapter.cc
    src/ingest/kafka_adapter.cc
    src/ingest/amqp_adapter.cc
    src/ingest/nats_adapter.cc
    src/decode/s1ap_decoder_wrapper.cc
    src/s1ap_parser.cpp
    src/s1ap_ue_correlator.cpp
    src/nas_parser.cpp
    src/utils/pcap_reader.cc
    src/correlate/ue_context.cc
    src/correlate/correlator.cc
    src/rules/rule_engine.cc
    src/rules/yaml_loader.cc
    src/sinks/sink.cc
    src/sinks/stdout_sink.cc
    src/sinks/jsonl_sink.cc
    src/processor/pipeline.cc
    ${PROTO_SRCS}
    ${PROTO_GRPC_SRCS}
)

target_include_directories(s1see_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}
)


# Add pcap include directory if available
if(HAVE_PCAP)
    target_include_directories(s1see_core PRIVATE ${PCAP_INCLUDE_DIR})
endif()

target_link_libraries(s1see_core PUBLIC
    protobuf::libprotobuf
    gRPC::grpc++
    gRPC::grpc++_reflection
    yaml-cpp::yaml-cpp
    Threads::Threads
)

# Applications
add_executable(s1see_spoolerd
    apps/s1see_spoolerd.cc
)
target_link_libraries(s1see_spoolerd s1see_core)

add_executable(s1see_processor
    apps/s1see_processor.cc
)
target_link_libraries(s1see_processor s1see_core)

add_executable(s1see_demo_generator
    apps/s1see_demo_generator.cc
)
target_link_libraries(s1see_demo_generator s1see_core)

# Tests
enable_testing()

add_executable(test_ue_context
    tests/test_ue_context.cc
)
target_link_libraries(test_ue_context s1see_core)

add_executable(test_correlator
    tests/test_correlator.cc
)
target_link_libraries(test_correlator s1see_core)

add_executable(test_integration
    tests/test_integration.cc
)
target_link_libraries(test_integration s1see_core)

# PCAP test (optional, requires libpcap)
if(HAVE_PCAP)
    add_executable(test_pcap
        tests/test_pcap.cc
    )
    target_include_directories(test_pcap PRIVATE ${PCAP_INCLUDE_DIR})
    target_link_libraries(test_pcap s1see_core pcap)
    add_test(NAME PCAPTest COMMAND test_pcap)
    message(STATUS "PCAP test enabled")
else()
    message(STATUS "PCAP test disabled (libpcap not found)")
endif()

# Add tests
add_test(NAME UEContextTest COMMAND test_ue_context)
add_test(NAME CorrelatorTest COMMAND test_correlator)
add_test(NAME IntegrationTest COMMAND test_integration)

# Install
install(TARGETS s1see_spoolerd s1see_processor s1see_demo_generator
    RUNTIME DESTINATION bin
)

install(DIRECTORY config/ DESTINATION etc/s1see
    FILES_MATCHING PATTERN "*.yaml" PATTERN "*.yml"
)
