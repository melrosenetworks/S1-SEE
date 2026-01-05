# Workaround for protobuf 6.x / gRPC target conflicts
# This file manually defines missing protobuf targets that gRPC expects

if(TARGET protobuf::libprotobuf AND NOT TARGET protobuf::libupb)
    # gRPC expects these targets but protobuf 6.x doesn't define them
    # We'll create empty interface libraries as placeholders
    add_library(protobuf::libupb INTERFACE IMPORTED)
    add_library(protobuf::protoc-gen-upb INTERFACE IMPORTED)
    add_library(protobuf::protoc-gen-upbdefs INTERFACE IMPORTED)
    add_library(protobuf::protoc-gen-upb_minitable INTERFACE IMPORTED)
endif()




