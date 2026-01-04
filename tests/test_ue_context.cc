#include "s1see/correlate/ue_context.h"
#include "canonical_message.pb.h"
#include <cassert>
#include <iostream>

using s1see::CanonicalMessage;

void test_ue_context_update() {
    std::cout << "Testing UEContext::update()..." << std::endl;
    
    s1see::correlate::UEContext context;
    CanonicalMessage msg;
    
    // Set identifiers
    msg.set_mme_ue_s1ap_id(12345);
    msg.set_enb_ue_s1ap_id(67890);
    msg.set_imsi("123456789012345");
    msg.set_guti("guti123");
    msg.set_tmsi("tmsi456");
    msg.set_imei("imei789");
    msg.set_enb_id("enb001");
    msg.set_mme_id("mme001");
    msg.set_ecgi("ecgi123");
    
    context.update(msg);
    
    assert(context.mme_ue_s1ap_id.has_value() && context.mme_ue_s1ap_id.value() == 12345);
    assert(context.enb_ue_s1ap_id.has_value() && context.enb_ue_s1ap_id.value() == 67890);
    assert(context.imsi.has_value() && context.imsi.value() == "123456789012345");
    assert(context.guti.has_value() && context.guti.value() == "guti123");
    assert(context.tmsi.has_value() && context.tmsi.value() == "tmsi456");
    assert(context.imei.has_value() && context.imei.value() == "imei789");
    assert(context.enb_id.has_value() && context.enb_id.value() == "enb001");
    assert(context.mme_id.has_value() && context.mme_id.value() == "mme001");
    assert(context.ecgi == "ecgi123");
    
    std::cout << "  ✓ UEContext::update() passed" << std::endl;
}

void test_subscriber_key_generation() {
    std::cout << "Testing UEContext::generate_subscriber_key()..." << std::endl;
    
    s1see::correlate::UEContext context;
    
    // Test with IMSI (highest priority)
    context.imsi = "123456789012345";
    std::string key = context.generate_subscriber_key();
    assert(key == "imsi:123456789012345");
    std::cout << "  ✓ IMSI key generation passed" << std::endl;
    
    // Test with GUTI (when IMSI not available)
    context.imsi.reset();
    context.guti = "guti123";
    key = context.generate_subscriber_key();
    assert(key == "guti:guti123");
    std::cout << "  ✓ GUTI key generation passed" << std::endl;
    
    // Test with eNB composite (when IMSI/GUTI not available)
    context.guti.reset();
    context.enb_id = "enb001";
    context.enb_ue_s1ap_id = 456;
    key = context.generate_subscriber_key();
    assert(key == "enb:enb001:456");
    std::cout << "  ✓ eNB composite key generation passed" << std::endl;
    
    // Test with MME composite
    context.enb_id.reset();
    context.enb_ue_s1ap_id.reset();
    context.mme_id = "mme001";
    context.mme_ue_s1ap_id = 789;
    key = context.generate_subscriber_key();
    assert(key == "mme:mme001:789");
    std::cout << "  ✓ MME composite key generation passed" << std::endl;
    
    std::cout << "  ✓ generate_subscriber_key() passed" << std::endl;
}

void test_stable_identity_matching() {
    std::cout << "Testing UEContext::matches_stable_identity()..." << std::endl;
    
    s1see::correlate::UEContext ctx1, ctx2;
    
    // Test IMSI matching
    ctx1.imsi = "123456789012345";
    ctx2.imsi = "123456789012345";
    assert(ctx1.matches_stable_identity(ctx2));
    std::cout << "  ✓ IMSI matching passed" << std::endl;
    
    // Test GUTI matching
    ctx1.imsi.reset();
    ctx2.imsi.reset();
    ctx1.guti = "guti123";
    ctx2.guti = "guti123";
    assert(ctx1.matches_stable_identity(ctx2));
    std::cout << "  ✓ GUTI matching passed" << std::endl;
    
    // Test IMEI matching
    ctx1.guti.reset();
    ctx2.guti.reset();
    ctx1.imei = "imei789";
    ctx2.imei = "imei789";
    assert(ctx1.matches_stable_identity(ctx2));
    std::cout << "  ✓ IMEI matching passed" << std::endl;
    
    // Test non-matching
    ctx1.imei = "imei789";
    ctx2.imei = "imei999";
    assert(!ctx1.matches_stable_identity(ctx2));
    std::cout << "  ✓ Non-matching test passed" << std::endl;
    
    std::cout << "  ✓ matches_stable_identity() passed" << std::endl;
}

int main() {
    std::cout << "Running UEContext tests..." << std::endl;
    test_ue_context_update();
    test_subscriber_key_generation();
    test_stable_identity_matching();
    std::cout << "\nAll UEContext tests passed!" << std::endl;
    return 0;
}

