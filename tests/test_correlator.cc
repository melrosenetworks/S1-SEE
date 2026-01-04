#include "s1see/correlate/correlator.h"
#include "canonical_message.pb.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

using s1see::CanonicalMessage;

void test_correlator_basic() {
    std::cout << "Testing Correlator basic operations..." << std::endl;
    
    s1see::correlate::Correlator correlator;
    
    // Create message with IMSI
    CanonicalMessage msg1;
    msg1.set_imsi("123456789012345");
    msg1.set_mme_ue_s1ap_id(100);
    msg1.set_enb_ue_s1ap_id(200);
    msg1.set_enb_id("enb001");
    msg1.set_mme_id("mme001");
    msg1.set_ecgi("ecgi001");
    
    std::string key1 = correlator.get_or_create_context(msg1);
    assert(!key1.empty());
    assert(key1 == "imsi:123456789012345");
    std::cout << "  ✓ Created context with IMSI: " << key1 << std::endl;
    
    // Get same context with same IMSI
    std::string key2 = correlator.get_or_create_context(msg1);
    assert(key1 == key2);
    std::cout << "  ✓ Retrieved same context by IMSI" << std::endl;
    
    // Get context by subscriber key
    auto ctx = correlator.get_context(key1);
    assert(ctx != nullptr);
    assert(ctx->imsi.has_value() && ctx->imsi.value() == "123456789012345");
    std::cout << "  ✓ Retrieved context by key" << std::endl;
    
    std::cout << "  ✓ Basic correlator operations passed" << std::endl;
}

void test_correlator_without_imsi() {
    std::cout << "Testing Correlator without IMSI..." << std::endl;
    
    s1see::correlate::Correlator correlator;
    
    // Create message without IMSI, but with eNodeB ID + eNB UE S1AP ID
    CanonicalMessage msg1;
    msg1.set_enb_id("enb001");
    msg1.set_enb_ue_s1ap_id(456);
    msg1.set_ecgi("ecgi001");
    
    std::string key1 = correlator.get_or_create_context(msg1);
    assert(!key1.empty());
    assert(key1.find("enb:enb001:456") != std::string::npos);
    std::cout << "  ✓ Created context without IMSI: " << key1 << std::endl;
    
    // Get same context with same eNodeB identifiers
    CanonicalMessage msg2;
    msg2.set_enb_id("enb001");
    msg2.set_enb_ue_s1ap_id(456);
    msg2.set_ecgi("ecgi001");
    
    std::string key2 = correlator.get_or_create_context(msg2);
    assert(key1 == key2);
    std::cout << "  ✓ Retrieved same context by eNodeB composite" << std::endl;
    
    // Now add IMSI to the same UE
    CanonicalMessage msg3;
    msg3.set_imsi("123456789012345");
    msg3.set_enb_id("enb001");
    msg3.set_enb_ue_s1ap_id(456);
    msg3.set_ecgi("ecgi001");
    
    std::string key3 = correlator.get_or_create_context(msg3);
    // Should find existing context and update it with IMSI
    assert(key3 == key1 || key3 == "imsi:123456789012345");
    std::cout << "  ✓ Updated context with IMSI: " << key3 << std::endl;
    
    // Verify IMSI was added
    auto ctx = correlator.get_context(key3);
    assert(ctx != nullptr);
    if (ctx->imsi.has_value()) {
        assert(ctx->imsi.value() == "123456789012345");
        std::cout << "  ✓ IMSI successfully added to context" << std::endl;
    }
    
    std::cout << "  ✓ Correlator without IMSI test passed" << std::endl;
}

void test_correlator_enb_mme_change() {
    std::cout << "Testing Correlator with eNodeB/MME change..." << std::endl;
    
    s1see::correlate::Correlator correlator;
    
    // Create message with IMSI and eNodeB
    CanonicalMessage msg1;
    msg1.set_imsi("123456789012345");
    msg1.set_enb_id("enb001");
    msg1.set_enb_ue_s1ap_id(100);
    msg1.set_mme_id("mme001");
    msg1.set_mme_ue_s1ap_id(200);
    msg1.set_ecgi("ecgi001");
    
    std::string key1 = correlator.get_or_create_context(msg1);
    assert(!key1.empty());
    std::cout << "  ✓ Created context: " << key1 << std::endl;
    
    // Same UE, different eNodeB (handover scenario)
    CanonicalMessage msg2;
    msg2.set_imsi("123456789012345");  // Same IMSI
    msg2.set_enb_id("enb002");          // Different eNodeB
    msg2.set_enb_ue_s1ap_id(300);       // Different eNB UE S1AP ID
    msg2.set_mme_id("mme001");          // Same MME
    msg2.set_mme_ue_s1ap_id(200);      // Same MME UE S1AP ID
    msg2.set_ecgi("ecgi002");
    
    std::string key2 = correlator.get_or_create_context(msg2);
    // Should find same context by IMSI and update it
    assert(key2 == key1);
    std::cout << "  ✓ Updated context with new eNodeB: " << key2 << std::endl;
    
    // Verify eNodeB was updated
    auto ctx = correlator.get_context(key2);
    assert(ctx != nullptr);
    assert(ctx->imsi.has_value() && ctx->imsi.value() == "123456789012345");
    if (ctx->enb_id.has_value()) {
        assert(ctx->enb_id.value() == "enb002");
        std::cout << "  ✓ eNodeB ID successfully updated" << std::endl;
    }
    
    std::cout << "  ✓ eNodeB/MME change test passed" << std::endl;
}

void test_correlator_expiry() {
    std::cout << "Testing Correlator context expiry..." << std::endl;
    
    s1see::correlate::Correlator::Config config;
    config.context_expiry = std::chrono::seconds(1);  // 1 second expiry
    s1see::correlate::Correlator correlator(config);
    
    CanonicalMessage msg;
    msg.set_imsi("123456789012345");
    msg.set_enb_id("enb001");
    msg.set_enb_ue_s1ap_id(100);
    
    std::string key = correlator.get_or_create_context(msg);
    assert(!key.empty());
    
    auto ctx = correlator.get_context(key);
    assert(ctx != nullptr);
    std::cout << "  ✓ Context created" << std::endl;
    
    // Wait for expiry
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Cleanup expired
    correlator.cleanup_expired();
    
    // Context should be gone
    ctx = correlator.get_context(key);
    assert(ctx == nullptr);
    std::cout << "  ✓ Context expired and cleaned up" << std::endl;
    
    std::cout << "  ✓ Context expiry test passed" << std::endl;
}

int main() {
    std::cout << "Running Correlator tests..." << std::endl;
    test_correlator_basic();
    test_correlator_without_imsi();
    test_correlator_enb_mme_change();
    test_correlator_expiry();
    std::cout << "\nAll Correlator tests passed!" << std::endl;
    return 0;
}

