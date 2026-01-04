# S1-SEE Application Flow

This document contains a Mermaid diagram showing the complete flow through the S1-SEE application.

```mermaid
flowchart TB
    %% External Sources
    External[External Sources<br/>S1AP PDUs]
    
    %% Stage 0: Ingress Spooler
    subgraph Stage0["Stage 0: Ingress Spooler"]
        direction TB
        GrpcAdapter[gRPC Adapter<br/>Streaming Server]
        KafkaAdapter[Kafka Adapter<br/>Skeleton]
        AMQPAdapter[AMQP Adapter<br/>Skeleton]
        NatsAdapter[NATS Adapter<br/>Skeleton]
    end
    
    %% Spool/WAL
    Spool[Spool/WAL<br/>Write-Ahead Log<br/>- Partitioned by hash<br/>- Append-only segments<br/>- Index files<br/>- Consumer offsets]
    
    %% Stage 1: Decoder + Normaliser
    subgraph Stage1["Stage 1: Decoder + Normaliser"]
        direction TB
        Decoder[S1AP Decoder<br/>RealS1APDecoder]
        Normalizer[Canonical Message<br/>Normalization]
    end
    
    %% Stage 2: Correlator
    subgraph Stage2["Stage 2: Correlator"]
        direction TB
        Correlator[UE Context Correlator<br/>- IMSI/GUTI/TMSI indexing<br/>- MME/eNB composite keys<br/>- Context merging<br/>- Expiry cleanup]
        UEContext[UE Context Store<br/>- Subscriber keys<br/>- Stable identities<br/>- Network IDs]
    end
    
    %% Stage 3: Event Engine
    subgraph Stage3["Stage 3: Event Engine"]
        direction TB
        RuleEngine[Rule Engine<br/>- YAML ruleset loader]
        SingleRules[Single Message Rules<br/>Match msg_type]
        SeqRules[Sequence Rules<br/>Match msg pairs<br/>within time window]
    end
    
    %% Stage 4: Sinks
    subgraph Stage4["Stage 4: Sinks"]
        direction TB
        StdoutSink[Stdout Sink<br/>JSON output]
        JsonlSink[JSONL Sink<br/>File output]
        FutureSinks[Future Sinks<br/>Kafka/gRPC/etc.]
    end
    
    %% Main Applications
    subgraph Apps["Applications"]
        direction TB
        Spoolerd[s1see_spoolerd<br/>Ingress Daemon]
        Processor[s1see_processor<br/>Main Pipeline]
        DemoGen[s1see_demo_generator<br/>Test Generator]
    end
    
    %% Flow connections
    External --> GrpcAdapter
    External --> KafkaAdapter
    External --> AMQPAdapter
    External --> NatsAdapter
    
    GrpcAdapter --> Spool
    KafkaAdapter --> Spool
    AMQPAdapter --> Spool
    NatsAdapter --> Spool
    
    Spool --> Decoder
    Decoder --> Normalizer
    Normalizer --> Correlator
    
    Correlator --> UEContext
    UEContext --> Correlator
    
    Correlator --> RuleEngine
    RuleEngine --> SingleRules
    RuleEngine --> SeqRules
    
    SingleRules --> StdoutSink
    SeqRules --> StdoutSink
    SingleRules --> JsonlSink
    SeqRules --> JsonlSink
    SingleRules --> FutureSinks
    SeqRules --> FutureSinks
    
    %% Application connections
    Spoolerd -.->|manages| GrpcAdapter
    Spoolerd -.->|writes to| Spool
    Processor -.->|reads from| Spool
    Processor -.->|orchestrates| Stage1
    Processor -.->|orchestrates| Stage2
    Processor -.->|orchestrates| Stage3
    Processor -.->|orchestrates| Stage4
    DemoGen -.->|sends to| GrpcAdapter
    
    %% Styling
    classDef stage0 fill:#e1f5ff,stroke:#01579b,stroke-width:2px
    classDef stage1 fill:#f3e5f5,stroke:#4a148c,stroke-width:2px
    classDef stage2 fill:#e8f5e9,stroke:#1b5e20,stroke-width:2px
    classDef stage3 fill:#fff3e0,stroke:#e65100,stroke-width:2px
    classDef stage4 fill:#fce4ec,stroke:#880e4f,stroke-width:2px
    classDef spool fill:#fff9c4,stroke:#f57f17,stroke-width:3px
    classDef app fill:#e0f2f1,stroke:#004d40,stroke-width:2px
    
    class GrpcAdapter,KafkaAdapter,AMQPAdapter,NatsAdapter stage0
    class Decoder,Normalizer stage1
    class Correlator,UEContext stage2
    class RuleEngine,SingleRules,SeqRules stage3
    class StdoutSink,JsonlSink,FutureSinks stage4
    class Spool spool
    class Spoolerd,Processor,DemoGen app
```

## Detailed Flow Description

### Stage 0: Ingress Spooler
- **gRPC Adapter**: Fully implemented streaming server that receives SignalMessage protobufs
- **Kafka/AMQP/NATS Adapters**: Skeleton implementations ready for integration
- All adapters write to the Spool before ACKing upstream (no-loss guarantee)

### Spool (Write-Ahead Log)
- **Partitioning**: Messages partitioned by hash(source_id + source_sequence)
- **Segments**: Append-only log files with index files for fast offset lookups
- **Consumer Groups**: Support for multiple independent consumers with offset tracking
- **Replay**: Full replay capability from any offset

### Stage 1: Decoder + Normaliser
- **S1AP Decoder** (`RealS1APDecoder`): 
  - Extracts S1AP PDUs from SCTP packets (PayloadProtocolID = 18)
  - Parses S1AP Information Elements using PER (Packed Encoding Rules) decoding
  - Extracts procedure codes and message types
  - Parses embedded NAS PDUs for additional identifiers
- **Normalization**: Converts to CanonicalMessage format with:
  - Message type mapping (HandoverRequest, InitialUEMessage, UEContextReleaseComplete, etc.)
  - UE identifiers (IMSI, TMSI, IMEISV, MME/eNB UE S1AP IDs)
  - TEIDs from E-RAB setup messages
  - ECGI (E-UTRAN Cell Global Identifier)
  - Decoded tree (JSON representation of information elements)
  - Spool offset references

### Stage 2: Correlator
- **S1apUeCorrelator**: Maintains subscriber records with all identifiers:
  - IMSI, TMSI, IMEISV (stable identifiers)
  - MME-UE-S1AP-ID, eNB-UE-S1AP-ID (temporary network identifiers)
  - TEIDs (GTP tunnel endpoint identifiers)
  - Tracks associations and handles identifier conflicts
- **UE Context Management**: Maintains UE contexts indexed by:
  1. IMSI (globally unique subscriber identifier)
  2. TMSI (temporary mobile subscriber identity)
  3. TMSI + ECGI (location-scoped)
  4. MME composite (mme_id + mme_ue_s1ap_id)
  5. eNB composite (enb_id + enb_ue_s1ap_id)
  6. IMEISV (device identifier)
- **Context Merging**: Handles handovers and network element changes
- **ID Removal**: Automatically removes S1AP IDs on UEContextReleaseComplete
- **Expiry**: Automatic cleanup of expired contexts

### Stage 3: Event Engine
- **Rule Engine**: Loads YAML rulesets and evaluates rules
- **Single Message Rules**: Match on message type (e.g., "HandoverRequest" → "Mobility.Handover.Commanded")
- **Sequence Rules**: Match message pairs within time windows (e.g., "HandoverRequest" + "HandoverNotify" → "Mobility.Handover.Completed")
- **Evidence Chain**: Every event includes spool offsets pointing to source messages

### Stage 4: Sinks
- **Stdout Sink**: Emits events as JSON to stdout
- **JSONL Sink**: Writes events to JSONL file (one event per line)
- **Future Sinks**: Architecture supports Kafka, gRPC, and other output formats

## Data Flow Example

1. **Ingest**: External source sends S1AP PDU via gRPC → gRPC Adapter receives it
2. **Spool**: Adapter writes SignalMessage to Spool (partitioned, indexed) → ACKs upstream
3. **Read**: Processor reads from Spool at consumer offset
4. **Decode**: S1AP PDU extracted from SCTP, parsed, normalized to CanonicalMessage
5. **Correlate**: Message matched to UE context (by IMSI, TMSI, etc.), context updated
6. **Rule Match**: Rule engine checks single-message and sequence rules
7. **Emit**: If rule matches, Event created with evidence chain → sent to all sinks
8. **Output**: Event appears in stdout and JSONL file

## Key Features

- **No-loss upgradeability**: Messages spooled before ACK
- **Evidence chain**: Every event links to spool offsets
- **Deterministic replay**: Replay from spool produces identical events
- **Transport-agnostic**: All transports feed unified SignalMessage model
- **Declarative rules**: YAML-based event rules


