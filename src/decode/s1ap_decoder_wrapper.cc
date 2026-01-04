/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: s1ap_decoder_wrapper.cc
 * Description: Implementation of S1AP decoder wrapper classes for converting raw
 *              S1AP bytes to CanonicalMessage protobuf format. Integrates with
 *              s1ap_parser to extract identifiers, S1AP IDs, ECGI, and other
 *              information elements from S1AP PDUs.
 */

#include "s1see/decode/s1ap_decoder_wrapper.h"
#include "s1ap_parser.h"
#include <sstream>
#include <iomanip>
#include <google/protobuf/util/json_util.h>
#include <cctype>

namespace s1see {
namespace decode {

// Helper function to convert hex string to bytes
static std::vector<uint8_t> hex_string_to_bytes(const std::string& hex_str) {
    std::vector<uint8_t> bytes;
    std::string clean_hex = hex_str;
    
    // Remove whitespace and common separators
    clean_hex.erase(std::remove_if(clean_hex.begin(), clean_hex.end(),
        [](char c) { return std::isspace(c) || c == ':' || c == '-'; }), clean_hex.end());
    
    // Convert pairs of hex digits to bytes
    for (size_t i = 0; i < clean_hex.length(); i += 2) {
        if (i + 1 < clean_hex.length()) {
            std::string byte_str = clean_hex.substr(i, 2);
            try {
                uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
                bytes.push_back(byte);
            } catch (...) {
                // Skip invalid hex pairs
            }
        }
    }
    
    return bytes;
}

// Helper function to parse EUTRAN-CGI components from bytes
// EUTRAN-CGI ::= SEQUENCE { pLMNidentity PLMNidentity, cell-ID CellIdentity }
// pLMNidentity is 3 bytes (TBCD-STRING)
// cell-ID is 28 bits (BIT STRING) typically encoded as 4 bytes
static void parse_eutran_cgi(const std::vector<uint8_t>& ecgi_bytes,
                             std::vector<uint8_t>& plmn_identity,
                             std::vector<uint8_t>& cell_id) {
    plmn_identity.clear();
    cell_id.clear();
    
    if (ecgi_bytes.size() >= 3) {
        // Extract PLMNidentity (first 3 bytes)
        plmn_identity.assign(ecgi_bytes.begin(), ecgi_bytes.begin() + 3);
    }
    
    if (ecgi_bytes.size() >= 7) {
        // Extract cell-ID (next 4 bytes, 28 bits)
        cell_id.assign(ecgi_bytes.begin() + 3, ecgi_bytes.begin() + 7);
    } else if (ecgi_bytes.size() > 3) {
        // Cell-ID might be shorter, take remaining bytes
        cell_id.assign(ecgi_bytes.begin() + 3, ecgi_bytes.end());
    }
}

// Map S1AP procedure code + PDU type to canonical message type name
// Uses procedure_name from s1ap_parser::getProcedureCodeName() and maps PDU types
// Based on 3GPP TS 36.413 S1AP specification (ASN.1 definitions)
static std::string map_procedure_to_msg_type(uint8_t procedure_code, 
                                             s1ap_parser::S1apPduType pdu_type,
                                             const std::string& procedure_name) {
    // procedure_name is already set by parse_result using getProcedureCodeName()
    // We map PDU types to specific message names based on ASN.1 definitions
    // Handover Preparation (procedure code 0)
    if (procedure_code == 0) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "HandoverRequired";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "HandoverCommand";
        } else if (pdu_type == s1ap_parser::S1apPduType::UNSUCCESSFUL_OUTCOME) {
            return "HandoverPreparationFailure";
        }
    }
    
    // Handover Resource Allocation (procedure code 1)
    if (procedure_code == 1) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "HandoverRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "HandoverRequestAcknowledge";
        } else if (pdu_type == s1ap_parser::S1apPduType::UNSUCCESSFUL_OUTCOME) {
            return "HandoverFailure";
        }
    }
    
    // Handover Notification (procedure code 2) - uses procedure_name "HandoverNotification"
    if (procedure_code == 2) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "HandoverNotify";
        }
    }
    
    // Path Switch Request (procedure code 3)
    if (procedure_code == 3) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "PathSwitchRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "PathSwitchRequestAcknowledge";
        } else if (pdu_type == s1ap_parser::S1apPduType::UNSUCCESSFUL_OUTCOME) {
            return "PathSwitchRequestFailure";
        }
    }
    
    // Handover Cancel (procedure code 4)
    if (procedure_code == 4) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "HandoverCancel";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "HandoverCancelAcknowledge";
        }
    }
    
    // E-RAB Setup (procedure code 5)
    if (procedure_code == 5) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "E-RABSetupRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "E-RABSetupResponse";
        }
    }
    
    // E-RAB Modify (procedure code 6)
    if (procedure_code == 6) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "E-RABModifyRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "E-RABModifyResponse";
        }
    }
    
    // E-RAB Release (procedure code 7)
    if (procedure_code == 7) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "E-RABReleaseCommand";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "E-RABReleaseResponse";
        }
    }
    
    // E-RAB Release Indication (procedure code 8)
    if (procedure_code == 8) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "E-RABReleaseIndication";
        }
    }
    
    // Initial Context Setup (procedure code 9)
    if (procedure_code == 9) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "InitialContextSetupRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "InitialContextSetupResponse";
        } else if (pdu_type == s1ap_parser::S1apPduType::UNSUCCESSFUL_OUTCOME) {
            return "InitialContextSetupFailure";
        }
    }
    
    // Paging (procedure code 10)
    if (procedure_code == 10) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "Paging";
        }
    }
    
    // Downlink NAS Transport (procedure code 11)
    if (procedure_code == 11) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "DownlinkNASTransport";
        }
    }
    
    // Initial UE Message (procedure code 12) - uses procedure_name "initialUEMessage"
    if (procedure_code == 12) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "initialUEMessage";  // Match the procedure name format from parser
        }
    }
    
    // Uplink NAS Transport (procedure code 13)
    if (procedure_code == 13) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "UplinkNASTransport";
        }
    }
    
    // Reset (procedure code 14)
    if (procedure_code == 14) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "Reset";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "ResetAcknowledge";
        }
    }
    
    // Error Indication (procedure code 15)
    if (procedure_code == 15) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "ErrorIndication";
        }
    }
    
    // NAS Non Delivery Indication (procedure code 16)
    if (procedure_code == 16) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "NASNonDeliveryIndication";
        }
    }
    
    // S1 Setup (procedure code 17)
    if (procedure_code == 17) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "S1SetupRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "S1SetupResponse";
        } else if (pdu_type == s1ap_parser::S1apPduType::UNSUCCESSFUL_OUTCOME) {
            return "S1SetupFailure";
        }
    }
    
    // UE Context Release Request (procedure code 18)
    if (procedure_code == 18) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "UEContextReleaseRequest";
        }
    }
    
    // Downlink S1cdma2000 Tunneling (procedure code 19)
    if (procedure_code == 19) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "DownlinkS1cdma2000tunneling";
        }
    }
    
    // Uplink S1cdma2000 Tunneling (procedure code 20)
    if (procedure_code == 20) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "UplinkS1cdma2000tunneling";
        }
    }
    
    // UE Context Modification (procedure code 21)
    if (procedure_code == 21) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "UEContextModificationRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "UEContextModificationResponse";
        } else if (pdu_type == s1ap_parser::S1apPduType::UNSUCCESSFUL_OUTCOME) {
            return "UEContextModificationFailure";
        }
    }
    
    // UE Capability Info Indication (procedure code 22)
    if (procedure_code == 22) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "UECapabilityInfoIndication";
        }
    }
    
    // UE Context Release (procedure code 23)
    if (procedure_code == 23) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "UEContextReleaseCommand";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "UEContextReleaseComplete";
        }
    }
    
    // eNB Status Transfer (procedure code 24)
    if (procedure_code == 24) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "ENBStatusTransfer";
        }
    }
    
    // MME Status Transfer (procedure code 25)
    if (procedure_code == 25) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "MMEStatusTransfer";
        }
    }
    
    // Deactivate Trace (procedure code 26)
    if (procedure_code == 26) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "DeactivateTrace";
        }
    }
    
    // Trace Start (procedure code 27)
    if (procedure_code == 27) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "TraceStart";
        }
    }
    
    // Trace Failure Indication (procedure code 28)
    if (procedure_code == 28) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "TraceFailureIndication";
        }
    }
    
    // eNB Configuration Update (procedure code 29)
    if (procedure_code == 29) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "ENBConfigurationUpdate";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "ENBConfigurationUpdateAcknowledge";
        } else if (pdu_type == s1ap_parser::S1apPduType::UNSUCCESSFUL_OUTCOME) {
            return "ENBConfigurationUpdateFailure";
        }
    }
    
    // MME Configuration Update (procedure code 30)
    if (procedure_code == 30) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "MMEConfigurationUpdate";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "MMEConfigurationUpdateAcknowledge";
        } else if (pdu_type == s1ap_parser::S1apPduType::UNSUCCESSFUL_OUTCOME) {
            return "MMEConfigurationUpdateFailure";
        }
    }
    
    // Location Reporting Control (procedure code 31)
    if (procedure_code == 31) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "LocationReportingControl";
        }
    }
    
    // Location Reporting Failure Indication (procedure code 32)
    if (procedure_code == 32) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "LocationReportingFailureIndication";
        }
    }
    
    // Location Report (procedure code 33)
    if (procedure_code == 33) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "LocationReport";
        }
    }
    
    // Overload Start (procedure code 34)
    if (procedure_code == 34) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "OverloadStart";
        }
    }
    
    // Overload Stop (procedure code 35)
    if (procedure_code == 35) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "OverloadStop";
        }
    }
    
    // Write Replace Warning (procedure code 36)
    if (procedure_code == 36) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "WriteReplaceWarningRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "WriteReplaceWarningResponse";
        }
    }
    
    // eNB Direct Information Transfer (procedure code 37)
    if (procedure_code == 37) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "ENBDirectInformationTransfer";
        }
    }
    
    // MME Direct Information Transfer (procedure code 38)
    if (procedure_code == 38) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "MMEDirectInformationTransfer";
        }
    }
    
    // Private Message (procedure code 39)
    if (procedure_code == 39) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "PrivateMessage";
        }
    }
    
    // eNB Configuration Transfer (procedure code 40)
    if (procedure_code == 40) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "ENBConfigurationTransfer";
        }
    }
    
    // MME Configuration Transfer (procedure code 41)
    if (procedure_code == 41) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "MMEConfigurationTransfer";
        }
    }
    
    // Cell Traffic Trace (procedure code 42)
    if (procedure_code == 42) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "CellTrafficTrace";
        }
    }
    
    // Kill (procedure code 43)
    if (procedure_code == 43) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "KillRequest";
        } else if (pdu_type == s1ap_parser::S1apPduType::SUCCESSFUL_OUTCOME) {
            return "KillResponse";
        }
    }
    
    // Downlink UE Associated LPPa Transport (procedure code 44)
    if (procedure_code == 44) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "DownlinkUEAssociatedLPPaTransport";
        }
    }
    
    // Uplink UE Associated LPPa Transport (procedure code 45)
    if (procedure_code == 45) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "UplinkUEAssociatedLPPaTransport";
        }
    }
    
    // Downlink Non UE Associated LPPa Transport (procedure code 46)
    if (procedure_code == 46) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "DownlinkNonUEAssociatedLPPaTransport";
        }
    }
    
    // Uplink Non UE Associated LPPa Transport (procedure code 47)
    if (procedure_code == 47) {
        if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
            return "UplinkNonUEAssociatedLPPaTransport";
        }
    }
    
    // For procedures that don't have multiple PDU types, use procedure_name from parser
    // This handles cases where the procedure name from getProcedureCodeName() is sufficient
    if (pdu_type == s1ap_parser::S1apPduType::INITIATING_MESSAGE) {
        // For initiating messages, use procedure name as-is (already from getProcedureCodeName)
        return procedure_name;
    }
    
    // Fallback: use procedure name from parser
    return procedure_name.empty() ? "Unknown" : procedure_name;
}

bool StubS1APDecoder::decode(const std::vector<uint8_t>& raw_bytes,
                            CanonicalMessage& canonical_message,
                            DecodedTree& decoded_tree) {
    // Stub implementation for prototype
    // In production, this would call the actual S1AP parser
    
    if (raw_bytes.empty()) {
        canonical_message.set_decode_failed(true);
        canonical_message.set_raw_bytes(raw_bytes.data(), raw_bytes.size());
        return false;
    }
    
    // For prototype: attempt basic parsing
    // Check if it looks like a HandoverRequest (procedure code 0)
    // This is a simplified stub - replace with real parser
    
    // Generate a JSON-like decoded tree
    std::ostringstream json;
    json << "{";
    json << "\"procedure_code\":" << static_cast<int>(raw_bytes[0] % 256) << ",";
    json << "\"length\":" << raw_bytes.size() << ",";
    json << "\"raw_hex\":\"";
    for (size_t i = 0; i < std::min(raw_bytes.size(), size_t(16)); ++i) {
        json << std::hex << std::setw(2) << std::setfill('0') 
             << static_cast<int>(raw_bytes[i]);
    }
    json << "\"";
    json << "}";
    
    decoded_tree.json_representation = json.str();
    
    // Try to extract some fields (stub logic)
    if (raw_bytes.size() > 0) {
        int proc_code = raw_bytes[0] % 256;
        canonical_message.set_procedure_code(proc_code);
        
        // Stub: map procedure codes to message types
        if (proc_code == 0) {
            canonical_message.set_msg_type("HandoverRequest");
        } else if (proc_code == 1) {
            canonical_message.set_msg_type("HandoverNotify");
        } else if (proc_code == 2) {
            canonical_message.set_msg_type("initialUEMessage");
        } else {
            canonical_message.set_msg_type("Unknown");
        }
        
        // Stub: extract fake UE IDs from bytes
        if (raw_bytes.size() > 4) {
            canonical_message.set_mme_ue_s1ap_id(
                static_cast<int32_t>(raw_bytes[1]) * 256 + raw_bytes[2]);
            canonical_message.set_enb_ue_s1ap_id(
                static_cast<int32_t>(raw_bytes[3]) * 256 + raw_bytes[4]);
        }
        
        // Preserve raw bytes
        canonical_message.set_raw_bytes(raw_bytes.data(), raw_bytes.size());
        canonical_message.set_decoded_tree(decoded_tree.json_representation);
        canonical_message.set_decode_failed(false);
        
        return true;
    }
    
    canonical_message.set_decode_failed(true);
    canonical_message.set_raw_bytes(raw_bytes.data(), raw_bytes.size());
    return false;
}

bool RealS1APDecoder::decode(const std::vector<uint8_t>& raw_bytes,
                             CanonicalMessage& canonical_message,
                             DecodedTree& decoded_tree) {
    if (raw_bytes.empty()) {
        canonical_message.set_decode_failed(true);
        canonical_message.set_raw_bytes(raw_bytes.data(), raw_bytes.size());
        return false;
    }
    
    // Try to extract S1AP from SCTP packet first
    std::vector<uint8_t> s1ap_bytes;
    auto s1ap_opt = s1ap_parser::extractS1apFromSctp(raw_bytes.data(), raw_bytes.size());
    if (s1ap_opt.has_value()) {
        s1ap_bytes = s1ap_opt.value();
    } else {
        // Assume raw bytes are already S1AP PDU
        s1ap_bytes = raw_bytes;
    }
    
    // Parse S1AP PDU
    auto parse_result = s1ap_parser::parseS1apPdu(s1ap_bytes.data(), s1ap_bytes.size());
    
    if (!parse_result.decoded) {
        canonical_message.set_decode_failed(true);
        canonical_message.set_raw_bytes(raw_bytes.data(), raw_bytes.size());
        return false;
    }

    // Debug
    // for (const auto& [key, value] : parse_result.information_elements) {
    //     std::cout << key << " " << value << std::endl;
    // }
    
    // Set procedure code and message type
    canonical_message.set_procedure_code(parse_result.procedure_code);
    // Map procedure code + PDU type to canonical message type name
    std::string msg_type = map_procedure_to_msg_type(
        parse_result.procedure_code,
        parse_result.pdu_type,
        parse_result.procedure_name
    );
    canonical_message.set_msg_type(msg_type);
    
    // Extract S1AP IDs from information_elements map
    // Values are hex strings that need to be converted to decimal integers
    // According to ASN.1: MME-UE-S1AP-ID ::= INTEGER (0..4294967295)
    //                    ENB-UE-S1AP-ID ::= INTEGER (0..16777215)
    if (parse_result.information_elements.find("MME-UE-S1AP-ID") != parse_result.information_elements.end()) {
        const std::string& hex_value = parse_result.information_elements.at("MME-UE-S1AP-ID");
        try {
            // Convert hex string to decimal integer (0..4294967295)
            uint32_t id = static_cast<uint32_t>(std::stoul(hex_value, nullptr, 16));
            // Store as int32_t (protobuf field type), values fit within range
            canonical_message.set_mme_ue_s1ap_id(static_cast<int32_t>(id));
        } catch (...) {
            // Ignore conversion errors
        }
    }
    
    if (parse_result.information_elements.find("eNB-UE-S1AP-ID") != parse_result.information_elements.end()) {
        const std::string& hex_value = parse_result.information_elements.at("eNB-UE-S1AP-ID");
        try {
            // Convert hex string to decimal integer (0..16777215)
            uint32_t id = static_cast<uint32_t>(std::stoul(hex_value, nullptr, 16));
            // Store as int32_t (protobuf field type), values fit within range
            canonical_message.set_enb_ue_s1ap_id(static_cast<int32_t>(id));
        } catch (...) {
            // Ignore conversion errors
        }
    }
    
    // Extract IMSI
    auto imsis = s1ap_parser::extractImsiFromS1apBytes(s1ap_bytes.data(), s1ap_bytes.size());
    if (!imsis.empty()) {
        canonical_message.set_imsi(imsis[0]);
    }
    
    // Extract TMSI
    auto tmsis = s1ap_parser::extractTmsiFromS1apBytes(s1ap_bytes.data(), s1ap_bytes.size());
    if (!tmsis.empty()) {
        canonical_message.set_tmsi(tmsis[0]);
    }
    
    // Extract IMEISV
    auto imeisvs = s1ap_parser::extractImeisvFromS1apBytes(s1ap_bytes.data(), s1ap_bytes.size());
    if (!imeisvs.empty()) {
        canonical_message.set_imei(imeisvs[0]);
    }
    
    // Extract ECGI (E-UTRAN Cell Global Identifier)
    // ECGI is IE ID 100 (EUTRAN-CGI)
    // According to ASN.1: EUTRAN-CGI ::= SEQUENCE {
    //                          pLMNidentity    PLMNidentity,  -- 3 bytes (TBCD-STRING)
    //                          cell-ID         CellIdentity   -- 28 bits (BIT STRING SIZE(28))
    //                      }
    // Value is hex string that needs to be converted to bytes
    if (parse_result.information_elements.find("EUTRAN-CGI") != parse_result.information_elements.end()) {
        const std::string& hex_value = parse_result.information_elements.at("EUTRAN-CGI");
        std::vector<uint8_t> ecgi_bytes = hex_string_to_bytes(hex_value);
        
        if (!ecgi_bytes.empty()) {
            // Store raw bytes
            canonical_message.set_ecgi(ecgi_bytes.data(), ecgi_bytes.size());
            
            // Parse and store components
            std::vector<uint8_t> plmn_identity, cell_id;
            parse_eutran_cgi(ecgi_bytes, plmn_identity, cell_id);
            
            if (!plmn_identity.empty()) {
                canonical_message.set_ecgi_plmn_identity(plmn_identity.data(), plmn_identity.size());
            }
            if (!cell_id.empty()) {
                canonical_message.set_ecgi_cell_id(cell_id.data(), cell_id.size());
            }
        }
    }
    
    // Extract target ECGI if present (for handover messages)
    // This might be in a different IE or in the transparent container
    for (const auto& [key, value] : parse_result.information_elements) {
        if (key.find("target") != std::string::npos || key.find("Target") != std::string::npos) {
            if (key.find("CGI") != std::string::npos || key.find("cgi") != std::string::npos) {
                std::vector<uint8_t> target_ecgi_bytes = hex_string_to_bytes(value);
                if (!target_ecgi_bytes.empty()) {
                    // Store raw bytes
                    canonical_message.set_target_ecgi(target_ecgi_bytes.data(), target_ecgi_bytes.size());
                    
                    // Parse and store components
                    std::vector<uint8_t> plmn_identity, cell_id;
                    parse_eutran_cgi(target_ecgi_bytes, plmn_identity, cell_id);
                    
                    if (!plmn_identity.empty()) {
                        canonical_message.set_target_ecgi_plmn_identity(plmn_identity.data(), plmn_identity.size());
                    }
                    if (!cell_id.empty()) {
                        canonical_message.set_target_ecgi_cell_id(cell_id.data(), cell_id.size());
                    }
                }
                break;
            }
        }
    }
    
    // Extract information elements and build decoded tree JSON
    std::ostringstream json;
    json << "{";
    json << "\"procedure_code\":" << static_cast<int>(parse_result.procedure_code) << ",";
    json << "\"procedure_name\":\"" << parse_result.procedure_name << "\",";
    json << "\"pdu_type\":" << static_cast<int>(parse_result.pdu_type) << ",";
    json << "\"information_elements\":{";
    bool first = true;
    for (const auto& [key, value] : parse_result.information_elements) {
        if (!first) json << ",";
        json << "\"" << key << "\":\"" << value << "\"";
        first = false;
    }
    json << "}";
    json << "}";

    // std::cout << json.str() << std::endl;
    
    decoded_tree.json_representation = json.str();
    canonical_message.set_decoded_tree(decoded_tree.json_representation);
    
    // Preserve raw bytes
    canonical_message.set_raw_bytes(raw_bytes.data(), raw_bytes.size());
    canonical_message.set_decode_failed(false);
    
    return true;
}

} // namespace decode
} // namespace s1see

