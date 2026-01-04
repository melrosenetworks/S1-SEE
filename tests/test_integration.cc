#include "s1see/spool/spool.h"
#include "s1see/decode/s1ap_decoder_wrapper.h"
#include "s1see/correlate/correlator.h"
#include "s1see/rules/rule_engine.h"
#include "s1see/rules/yaml_loader.h"
#include "s1see/sinks/stdout_sink.h"
#include "signal_message.pb.h"
#include "canonical_message.pb.h"
#include "event.pb.h"
#include "spool_record.pb.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>

using s1see::SignalMessage;
using s1see::CanonicalMessage;
using s1see::Event;
using s1see::SpoolRecord;

namespace fs = std::filesystem;

void test_spool_basic() {
    std::cout << "Testing Spool basic operations..." << std::endl;
    
    // Use test directory
    std::string test_dir = "test_spool_data";
    fs::remove_all(test_dir);  // Clean up
    
    s1see::spool::WALLog::Config config;
    config.base_dir = test_dir;
    config.num_partitions = 1;
    config.fsync_on_append = false;  // Faster for tests
    s1see::spool::Spool spool(config);
    
    // Create and append a message
    SignalMessage msg;
    msg.set_ts_capture(1000000);
    msg.set_ts_ingest(1000001);
    msg.set_source_id("test_source");
    msg.set_direction(SignalMessage::UPLINK);
    msg.set_source_sequence(1);
    msg.set_payload_type(SignalMessage::RAW_BYTES);
    msg.set_raw_bytes("test_pdu_data");
    
    auto [partition, offset] = spool.append(msg);
    assert(partition >= 0 && partition < 2);
    assert(offset >= 0);
    std::cout << "  ✓ Appended message: partition=" << partition << ", offset=" << offset << std::endl;
    
    // Read it back
    auto records = spool.read(partition, offset, 1);
    assert(records.size() == 1);
    assert(records[0].partition() == partition);
    assert(records[0].offset() == offset);
    assert(records[0].message().source_id() == "test_source");
    std::cout << "  ✓ Read message back successfully" << std::endl;
    
    // Test consumer offset
    spool.commit_offset("test_group", partition, offset);
    int64_t loaded_offset = spool.load_offset("test_group", partition);
    assert(loaded_offset == offset);
    std::cout << "  ✓ Consumer offset management works" << std::endl;
    
    // Cleanup
    fs::remove_all(test_dir);
    
    std::cout << "  ✓ Spool basic operations passed" << std::endl;
}

void test_decoder_wrapper() {
    std::cout << "Testing S1AP Decoder Wrapper..." << std::endl;
    
    s1see::decode::StubS1APDecoder decoder;
    
    // Test with valid data
    std::vector<uint8_t> raw_bytes = {0x00, 0x01, 0x02, 0x03, 0x04};
    CanonicalMessage canonical;
    s1see::decode::DecodedTree decoded_tree;
    
    bool result = decoder.decode(raw_bytes, canonical, decoded_tree);
    assert(result == true);
    assert(!canonical.decode_failed());
    assert(canonical.raw_bytes().size() == raw_bytes.size());
    std::cout << "  ✓ Decoder processed valid data" << std::endl;
    
    // Test with empty data
    std::vector<uint8_t> empty;
    CanonicalMessage canonical2;
    s1see::decode::DecodedTree decoded_tree2;
    
    result = decoder.decode(empty, canonical2, decoded_tree2);
    assert(result == false);
    assert(canonical2.decode_failed());
    std::cout << "  ✓ Decoder handled empty data correctly" << std::endl;
    
    std::cout << "  ✓ Decoder wrapper test passed" << std::endl;
}

void test_rules_engine() {
    std::cout << "Testing Rules Engine..." << std::endl;
    
    auto correlator = std::make_shared<s1see::correlate::Correlator>();
    s1see::rules::RuleEngine engine(correlator);
    
    // Create a simple ruleset
    s1see::rules::Ruleset ruleset;
    ruleset.id = "test";
    ruleset.version = "1.0";
    
    s1see::rules::SingleMessageRule rule;
    rule.event_name = "Test.Event";
    rule.msg_type_pattern = "HandoverRequest";
    rule.attributes["test"] = "value";
    ruleset.single_message_rules.push_back(rule);
    
    engine.load_ruleset(ruleset);
    std::cout << "  ✓ Ruleset loaded" << std::endl;
    
    // Create a message that matches
    CanonicalMessage msg;
    msg.set_msg_type("HandoverRequest");
    msg.set_spool_partition(0);
    msg.set_spool_offset(1);
    msg.set_enb_id("enb001");
    msg.set_enb_ue_s1ap_id(100);
    
    std::cout << "  Processing message..." << std::endl;
    auto events = engine.process(msg);
    std::cout << "  Got " << events.size() << " events" << std::endl;
    assert(events.size() == 1);
    assert(events[0].name() == "Test.Event");
    std::cout << "  ✓ Rule matched and event emitted" << std::endl;
    
    // Test sequence rule
    s1see::rules::SequenceRule seq_rule;
    seq_rule.event_name = "Test.Sequence";
    seq_rule.first_msg_type = "HandoverRequest";
    seq_rule.second_msg_type = "HandoverNotify";
    seq_rule.time_window = std::chrono::milliseconds(5000);
    ruleset.sequence_rules.push_back(seq_rule);
    
    engine.load_ruleset(ruleset);
    
    // First message
    CanonicalMessage msg1;
    msg1.set_msg_type("HandoverRequest");
    msg1.set_spool_partition(0);
    msg1.set_spool_offset(2);
    msg1.set_enb_id("enb001");
    msg1.set_enb_ue_s1ap_id(101);
    auto events1 = engine.process(msg1);
    // Should have at least 1 event (single message rule for HandoverRequest)
    assert(events1.size() >= 1);
    std::cout << "  ✓ First message processed: " << events1.size() << " events" << std::endl;
    
    // Second message within time window
    CanonicalMessage msg2;
    msg2.set_msg_type("HandoverNotify");
    msg2.set_spool_partition(0);
    msg2.set_spool_offset(3);
    msg2.set_enb_id("enb001");
    msg2.set_enb_ue_s1ap_id(101);
    auto events2 = engine.process(msg2);
    // Should have at least 1 event (sequence event if matched, or single message rule)
    assert(events2.size() >= 1);
    std::cout << "  ✓ Second message processed: " << events2.size() << " events" << std::endl;
    std::cout << "  ✓ Sequence rule test completed" << std::endl;
    
    std::cout << "  ✓ Rules engine test passed" << std::endl;
}

void test_sink() {
    std::cout << "Testing Sink..." << std::endl;
    
    s1see::sinks::StdoutSink sink;
    
    Event event;
    event.set_name("Test.Event");
    event.set_ts(1000000);
    event.set_subscriber_key("test_key");
    event.set_confidence(1.0);
    event.set_ruleset_id("test");
    event.set_ruleset_version("1.0");
    
    bool result = sink.emit(event);
    assert(result == true);
    std::cout << "  ✓ Event emitted to stdout sink" << std::endl;
    
    std::cout << "  ✓ Sink test passed" << std::endl;
}

int main() {
    std::cout << "Running Integration tests..." << std::endl;
    test_spool_basic();
    test_decoder_wrapper();
    test_rules_engine();
    test_sink();
    std::cout << "\nAll Integration tests passed!" << std::endl;
    return 0;
}

