# S1 Signal Event Engine (S1-SEE)

![S1-SEE Architecture](docs/architecture-diagram.svg)

A production-oriented prototype for ingesting, processing, and analyzing LTE S1AP Protocol Data Units (PDUs) with a modular, replayable real-time pipeline.

**Repository**: [https://github.com/melrosenetworks/S1-SEE](https://github.com/melrosenetworks/S1-SEE)

## Architecture

S1-SEE implements a multi-stage pipeline:

1. **Stage 0: Ingress Spooler** - Ingests messages via multiple transports (gRPC, Kafka, AMQP, NATS) and durably spools them before ACKing upstream
2. **Stage 1: Decoder + Normaliser** - Decodes S1AP PDUs and normalizes them into canonical messages
3. **Stage 2: Correlator** - Maintains UE contexts and correlates messages to subscribers
4. **Stage 3: Event Engine** - Applies declarative YAML rules to emit events
5. **Stage 5: Sinks** - Publishes events to various outputs (stdout, JSONL, Kafka, gRPC, etc.)

### Key Features

- **No-loss upgradeability**: Messages are durably spooled before ACKing
- **Spool as system of record**: Append-only log with partitions, offsets, and replay capability
- **Evidence chain**: Every event carries pointers to spool offsets for underlying messages
- **Transport-agnostic core**: All transports feed into a unified SignalMessage model
- **Declarative rules**: YAML-based event rules (single-message triggers + two-step sequences)

## Building

### Prerequisites

- CMake 3.20+
- C++20 compatible compiler (GCC 10+, Clang 12+)
- Protobuf 3.x
- gRPC
- yaml-cpp
- libpcap (optional, for PCAP file processing)

### Build Steps

```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

This will build:
- `s1see_spoolerd` - Ingress spooler daemon
- `s1see_processor` - Main processing pipeline
- `s1see_demo_generator` - Demo tool for generating test messages

## Running

### 1. Start the Spooler Daemon

```bash
./s1see_spoolerd [listen_address] [spool_dir]
```

Example:
```bash
./s1see_spoolerd 0.0.0.0:50051 spool_data
```

The spooler listens for gRPC streaming connections and durably stores all incoming messages.

### 2. Generate Test Messages

In another terminal:
```bash
./s1see_demo_generator [server_address] [num_messages]
```

Example:
```bash
./s1see_demo_generator localhost:50051 10
```

### 3. Run the Processor

```bash
./s1see_processor [spool_dir] [ruleset_file] [output_file] [continuous]
```

Example:
```bash
./s1see_processor spool_data config/rulesets/mobility.yaml events.jsonl true
```

The processor will:
- Read messages from the spool
- Decode and normalize them
- Correlate to UE contexts
- Apply rules to emit events
- Write events to stdout and JSONL file

## Configuration

### Rulesets

Rulesets are defined in YAML format. See `config/rulesets/mobility.yaml` for an example:

```yaml
ruleset:
  id: "mobility"
  version: "1.0"
  
  single_message_rules:
    - event_name: "Mobility.Handover.Commanded"
      msg_type: "HandoverRequest"
      attributes:
        category: "mobility"
        action: "commanded"
  
  sequence_rules:
    - event_name: "Mobility.Handover.Completed"
      first_msg_type: "HandoverRequest"
      second_msg_type: "HandoverNotify"
      time_window_ms: 15000
      attributes:
        category: "mobility"
        action: "completed"
```

### Spool Configuration

The spool can be configured via code or configuration file:
- `base_dir`: Directory for spool data
- `num_partitions`: Number of partitions (default: 1)
- `max_segment_size`: Maximum segment size before rotation
- `max_retention_bytes`: Maximum total size before pruning
- `max_retention_seconds`: Maximum age before pruning
- `fsync_on_append`: Whether to fsync on each append (default: true)

## Architecture Details

### Spool (WAL)

The spool is implemented as a local disk-based Write-Ahead Log (WAL) with:
- **Segmented log files**: `segment_{partition}_{baseOffset}.log`
- **Index files**: `segment_{partition}_{baseOffset}.idx` mapping offsets to file positions
- **Partitioning**: Messages are partitioned by hash(source_id + source_sequence)
- **Consumer groups**: Support for multiple consumer groups with independent offsets
- **Replay**: Full replay capability by reading from any offset

### Transport Adapters

- **gRPC**: Fully implemented streaming ingest server
- **Kafka**: Skeleton (integrate librdkafka for production)
- **AMQP**: Skeleton (integrate RabbitMQ-C for production)
- **NATS**: Skeleton (integrate nats.c for production)

### S1AP Decoder

The decoder uses a real S1AP parser implementation (`RealS1APDecoder`) that:
- Extracts S1AP PDUs from SCTP packets (PayloadProtocolID = 18)
- Parses S1AP Information Elements using PER (Packed Encoding Rules) decoding
- Extracts UE identifiers: IMSI, TMSI, IMEISV, MME-UE-S1AP-ID, eNB-UE-S1AP-ID
- Extracts TEIDs from E-RAB setup messages
- Parses embedded NAS PDUs to extract additional identifiers
- Based on 3GPP TS 36.413 (S1AP) and TS 24.301 (EPS NAS) specifications

### Correlator

The correlator consists of two main components:

**S1apUeCorrelator**: Maintains subscriber records with all identifiers:
- Stable identifiers: IMSI, TMSI, IMEISV
- Network identifiers: MME-UE-S1AP-ID, eNB-UE-S1AP-ID
- Tunnel identifiers: TEIDs (GTP tunnel endpoint identifiers)
- Tracks associations between identifiers and handles conflicts
- Automatically removes S1AP IDs when UEContextReleaseComplete is received

**UE Context Correlator**: Maintains UE contexts indexed by:
1. IMSI (globally unique subscriber identifier)
2. TMSI (temporary mobile subscriber identity)
3. TMSI + ECGI (location-scoped)
4. MME composite (mme_id + mme_ue_s1ap_id)
5. eNB composite (enb_id + enb_ue_s1ap_id)
6. IMEISV (device identifier)

The correlator handles context merging during handovers and automatically cleans up expired contexts.

### Event Evidence Chain

Every event includes an `EvidenceChain` with spool offsets pointing to the underlying messages. This allows:
- Retrieving raw bytes from spool
- Replaying events deterministically
- Auditing and debugging

## Integration Guide

### Customizing the S1AP Decoder

The system uses `RealS1APDecoder` which wraps the `s1ap_parser` implementation. To customize:

1. Implement `S1APDecoderWrapper` interface
2. In `Pipeline::Pipeline()`, replace the default decoder:
   ```cpp
   pipeline.set_decoder(std::make_unique<YourS1APDecoder>());
   ```

The current implementation includes:
- Full S1AP PDU parsing with PER decoding
- NAS PDU parsing for embedded messages
- Identifier extraction (IMSI, TMSI, IMEISV, S1AP IDs, TEIDs)
- Support for all major S1AP procedures

### Integrating Real Kafka/AMQP/NATS

1. **Kafka**: Implement `KafkaIngestAdapter::start()` using librdkafka
2. **AMQP**: Implement `AMQPIngestAdapter::start()` using RabbitMQ-C
3. **NATS**: Implement `NATSIngestAdapter::start()` using nats.c

See the adapter headers for the interface contract.

### Replacing the Spool with Kafka/JetStream

The spool interface is abstracted. To use Kafka:
1. Implement a `KafkaSpool` class with the same interface as `Spool`
2. Replace `WALLog` with Kafka consumer/producer logic
3. Update `Pipeline` to use the new spool implementation

## Deterministic Replay

The system is designed for deterministic replay:
- Messages are stored with monotonic offsets
- Events include evidence chains pointing to source messages
- Replaying from the same spool with the same rules produces identical events

## Error Handling

- **Decode failures**: Raw bytes are preserved, `decode_failed` flag is set
- **Spool failures**: Exceptions are thrown (never drop data silently)
- **Rule evaluation**: Failures are logged, processing continues

## Testing

### Unit and Integration Tests

```bash
cd build
./test_ue_context
./test_correlator
./test_integration
```

### PCAP Processing Test

Process a PCAP file containing S1AP traffic:

```bash
cd build
./test_pcap [path/to/file.pcap]
# Or use default location: ./test_pcap (looks for ../test_data/sample.pcap)
```

The PCAP test will:
- Read all packets from the PCAP file
- Extract S1AP PDUs from SCTP packets
- Process through the full pipeline (spool → decode → correlate → rules)
- Emit events to stdout and `test_pcap_events.jsonl`

### Demo with gRPC

Run the demo:
```bash
# Terminal 1
./s1see_spoolerd

# Terminal 2
./s1see_demo_generator localhost:50051 20

# Terminal 3
./s1see_processor spool_data config/rulesets/mobility.yaml events.jsonl true
```

Check `events.jsonl` for emitted events.

## Directory Structure

```
S1-SEE/
├── proto/              # Protobuf definitions
├── include/s1see/      # Header files
│   ├── spool/         # Spool/WAL implementation
│   ├── ingest/        # Transport adapters
│   ├── decode/        # S1AP decoder wrapper
│   ├── correlate/     # UE context correlator
│   ├── rules/         # Event rule engine
│   ├── sinks/         # Event sinks
│   ├── processor/     # Main pipeline
│   └── utils/         # Utility functions (PCAP reader)
├── src/               # Implementation files
│   ├── s1ap_parser.*  # S1AP PDU parser (PER decoding)
│   ├── nas_parser.*   # NAS message parser
│   ├── s1ap_ue_correlator.*  # Subscriber correlation
│   └── ...            # Other components
├── apps/              # Main applications
├── config/            # Configuration files (rulesets)
├── test_data/         # Test PCAP files
└── CMakeLists.txt     # Build configuration
```

## Future Enhancements

- Full Kafka/AMQP/NATS adapter implementations
- Metrics and observability
- Distributed processing support
- Advanced rule conditions (regex, ranges, etc.)
- Event aggregation and windowing
- Web UI for monitoring

## License

Copyright (c) 2026 Melrose Networks (Melrose Labs Ltd)

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for detailed guidelines.

Quick start:
1. **Fork the repository** from [https://github.com/melrosenetworks/S1-SEE](https://github.com/melrosenetworks/S1-SEE) and create a feature branch
2. **Follow the existing code style** and formatting conventions
3. **Add tests** for new functionality
4. **Update documentation** as needed
5. **Submit a pull request** with a clear description of changes

### Reporting Issues

Please use the [GitHub issue tracker](https://github.com/melrosenetworks/S1-SEE/issues) to report bugs or request features. Include:
- Description of the issue
- Steps to reproduce (if applicable)
- Expected vs. actual behavior
- Environment details (OS, compiler version, etc.)

