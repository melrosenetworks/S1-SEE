# Build Instructions

## Prerequisites

### macOS

```bash
# Install dependencies via Homebrew
brew install cmake protobuf grpc yaml-cpp libpcap

# Or via MacPorts
sudo port install cmake protobuf-cpp grpc yaml-cpp libpcap
```

**Note**: libpcap is required for PCAP file processing. If not installed, the PCAP test will be disabled but other components will build successfully.

### Ubuntu/Debian

```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    libyaml-cpp-dev \
    libpcap-dev
```

### Fedora/RHEL

```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    protobuf-devel \
    protobuf-compiler \
    grpc-devel \
    grpc-plugins \
    yaml-cpp-devel \
    libpcap-devel
```

## Building

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)  # Linux: make -j$(nproc)
# macOS: make -j$(sysctl -n hw.ncpu)
```

This will build:
- `s1see_core` - Core library (static library)
- `s1see_spoolerd` - Ingress spooler daemon
- `s1see_processor` - Main processing pipeline
- `s1see_demo_generator` - Demo tool for generating test messages
- `test_ue_context` - Unit tests for UE context
- `test_correlator` - Unit tests for correlator
- `test_integration` - Integration tests
- `test_pcap` - PCAP file processing test (requires libpcap)

## Running Tests

After building, you can test the system:

```bash
# Unit tests
./test_ue_context
./test_correlator

# Integration tests
./test_integration

# PCAP processing test (requires libpcap and a PCAP file)
./test_pcap [path/to/file.pcap]
# Or use default: ./test_pcap (looks for test_data/sample.pcap)
```

## PCAP Test

The PCAP test processes a PCAP file containing S1AP traffic:

1. Place your PCAP file in `test_data/` directory (or provide path as argument)
2. Run: `./test_pcap test_data/your_file.pcap`
3. The test will:
   - Extract S1AP PDUs from SCTP packets
   - Process through the full pipeline
   - Emit events to stdout and `test_pcap_events.jsonl`

## Troubleshooting

### Protobuf/gRPC Issues

If you encounter issues with protobuf or gRPC:

1. Ensure `protoc` and `grpc_cpp_plugin` are in your PATH
2. Check that protobuf and gRPC versions are compatible
3. Try building with verbose output: `make VERBOSE=1`

### Missing Dependencies

If CMake fails to find dependencies:

```bash
# Set custom paths if needed
cmake -DProtobuf_DIR=/path/to/protobuf \
      -DgRPC_DIR=/path/to/grpc \
      -Dyaml-cpp_DIR=/path/to/yaml-cpp \
      ..
```

### libpcap Not Found

If PCAP test is disabled:

```bash
# macOS
brew install libpcap

# Linux
sudo apt-get install libpcap-dev  # Debian/Ubuntu
sudo dnf install libpcap-devel   # Fedora/RHEL
```

Then reconfigure:
```bash
cd build
rm CMakeCache.txt
cmake ..
make test_pcap
```

### Compilation Errors

- Ensure you're using a C++20 compatible compiler
- Check that all include paths are correct
- Verify protobuf files were generated in the build directory
