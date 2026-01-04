/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: s1ap_decoder_wrapper.h
 * Description: Header for S1AP decoder wrapper classes that convert raw S1AP bytes
 *              to CanonicalMessage protobuf format. Provides interface for integrating
 *              with s1ap_parser to extract identifiers and information elements.
 */

#pragma once

#include "canonical_message.pb.h"
#include <string>
#include <vector>
#include <memory>

namespace s1see {
namespace decode {

// Decoded tree structure (lossless representation)
struct DecodedTree {
    std::string json_representation; // JSON or similar format
    // Additional structured fields can be added
};

// Interface for S1AP decoder
// This wraps an existing C++ S1AP parsing library
class S1APDecoderWrapper {
public:
    virtual ~S1APDecoderWrapper() = default;
    
    // Decode raw bytes into canonical message and decoded tree
    // Returns true on success, false on decode failure
    // On failure, decode_failed is set in canonical_message and raw_bytes are preserved
    virtual bool decode(const std::vector<uint8_t>& raw_bytes,
                       CanonicalMessage& canonical_message,
                       DecodedTree& decoded_tree) = 0;
    
    // Convenience method
    bool decode(const std::string& raw_bytes,
                CanonicalMessage& canonical_message,
                DecodedTree& decoded_tree) {
        std::vector<uint8_t> bytes(raw_bytes.begin(), raw_bytes.end());
        return decode(bytes, canonical_message, decoded_tree);
    }
};

// Stub implementation for prototype
// Replace with actual S1AP parser integration
class StubS1APDecoder : public S1APDecoderWrapper {
public:
    bool decode(const std::vector<uint8_t>& raw_bytes,
                CanonicalMessage& canonical_message,
                DecodedTree& decoded_tree) override;
};

// Real S1AP parser implementation using s1ap_parser library
class RealS1APDecoder : public S1APDecoderWrapper {
public:
    bool decode(const std::vector<uint8_t>& raw_bytes,
                CanonicalMessage& canonical_message,
                DecodedTree& decoded_tree) override;
};

} // namespace decode
} // namespace s1see

