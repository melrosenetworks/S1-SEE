/*
 * Melrose Networks (Melrose Labs Ltd) - https://melrosenetworks.com
 * Date: 2026-01-04
 * Support: support@melrosenetworks.com
 * Disclaimer: Provided "as is" without warranty; use at your own risk.
 * Title: s1ap_ue_correlator.h
 * Description: C++ header for correlating S1AP messages to UEs (User Equipment).
 *              Defines S1apUeCorrelator class and SubscriberRecord structure
 *              for managing UE identifiers and associations across S1AP messages.
 */

#ifndef S1AP_UE_CORRELATOR_H
#define S1AP_UE_CORRELATOR_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <cstdint>

// Forward declaration - S1apParseResult is defined in s1ap_parser namespace
namespace s1ap_parser {
    struct S1apParseResult;
}

// Debug logging control
#ifndef DEBUG_LOG
    #ifdef ENABLE_DEBUG_LOGGING
        #include <iostream>
        #include <iomanip>
        #define DEBUG_LOG std::cout
    #else
        #include <iosfwd>
        // Null stream that discards output
        namespace s1ap_correlator {
            class NullStream {
            public:
                template<typename T>
                NullStream& operator<<(const T&) { return *this; }
                // Handle std::endl and other stream manipulators
                typedef std::basic_ostream<char, std::char_traits<char> > CoutType;
                typedef CoutType& (*StandardEndLine)(CoutType&);
                NullStream& operator<<(StandardEndLine) { return *this; }
            };
            inline NullStream null_stream;
        }
        #define DEBUG_LOG s1ap_correlator::null_stream
    #endif
#endif

namespace s1ap_correlator {

// Subscriber Record
struct SubscriberRecord {
    std::optional<std::string> imsi;
    std::optional<std::string> tmsi;
    std::optional<uint32_t> enb_ue_s1ap_id;
    std::optional<uint32_t> mme_ue_s1ap_id;
    std::unordered_set<uint32_t> teids;
    std::optional<std::string> imeisv;
    
    // Drone protocol and GPS tracking
    std::optional<std::string> drone_protocol_type;  // "MAVLink", "DJI DUML", "other", or nullopt
    bool gps_data_available;
    std::optional<double> first_seen_timestamp;
    std::optional<double> last_seen_timestamp;
    
    // GPS location data (when available)
    std::optional<double> gps_latitude;
    std::optional<double> gps_longitude;
    std::optional<double> gps_altitude;
    std::optional<double> gps_heading;  // Course over ground / heading in degrees (0-360)
    
    // Velocity vectors (when available from GLOBAL_POSITION_INT)
    std::optional<double> gps_velocity_x;  // Velocity in X direction (m/s, NED frame)
    std::optional<double> gps_velocity_y;  // Velocity in Y direction (m/s, NED frame)
    std::optional<double> gps_velocity_z;  // Velocity in Z direction (m/s, NED frame)
    
    // Home position (first GPS position seen)
    std::optional<double> home_latitude;
    std::optional<double> home_longitude;
    std::optional<double> home_altitude;
    
    SubscriberRecord() : gps_data_available(false) {}
};

// Result structure for TMSI extraction
struct TmsiExtractionResult {
    std::vector<std::string> tmsis;
    std::vector<uint32_t> teids;  // All TEIDs found in decoded_list.items
};

// S1AP UE Correlator Class
class S1apUeCorrelator {
public:
    S1apUeCorrelator();
    ~S1apUeCorrelator();

    // Process S1AP frame and correlate to UE
    // Returns the subscriber record that was created or updated, or nullptr if no identifiers were found
    SubscriberRecord* processS1apFrame(uint32_t frame_no, const s1ap_parser::S1apParseResult& s1ap_result, double timestamp = 0.0);
    
    // Subscriber record management
    // Get or create subscriber record by any identifier
    SubscriberRecord* getOrCreateSubscriber(
        const std::optional<std::string>& imsi = std::nullopt,
        const std::optional<std::string>& tmsi = std::nullopt,
        const std::optional<uint32_t>& enb_ue_s1ap_id = std::nullopt,
        const std::optional<uint32_t>& mme_ue_s1ap_id = std::nullopt,
        const std::optional<uint32_t>& teid = std::nullopt,
        const std::optional<std::string>& imeisv = std::nullopt);
    
    // Get subscriber by identifier
    SubscriberRecord* getSubscriberByImsi(const std::string& imsi);
    SubscriberRecord* getSubscriberByTmsi(const std::string& tmsi);
    SubscriberRecord* getSubscriberByEnbUeS1apId(uint32_t enb_ue_s1ap_id);
    SubscriberRecord* getSubscriberByMmeUeS1apId(uint32_t mme_ue_s1ap_id);
    SubscriberRecord* getSubscriberByTeid(uint32_t teid);
    SubscriberRecord* getSubscriberByImeisv(const std::string& imeisv);
    
    // Associate identifier with subscriber (subscriber_id passed to avoid O(n) lookup)
    void associateImsi(SubscriberRecord* subscriber, uint64_t subscriber_id, const std::string& imsi);
    void associateTmsi(SubscriberRecord* subscriber, uint64_t subscriber_id, const std::string& tmsi);
    void associateEnbUeS1apId(SubscriberRecord* subscriber, uint64_t subscriber_id, uint32_t enb_ue_s1ap_id);
    void associateMmeUeS1apId(SubscriberRecord* subscriber, uint64_t subscriber_id, uint32_t mme_ue_s1ap_id);
    void associateTeid(SubscriberRecord* subscriber, uint64_t subscriber_id, uint32_t teid);
    void associateImeisv(SubscriberRecord* subscriber, uint64_t subscriber_id, const std::string& imeisv);
    
    // Remove association
    void removeImsiAssociation(const std::string& imsi);
    void removeTmsiAssociation(const std::string& tmsi);
    void removeEnbUeS1apIdAssociation(uint32_t enb_ue_s1ap_id);
    void removeMmeUeS1apIdAssociation(uint32_t mme_ue_s1ap_id);
    void removeTeidAssociation(uint32_t teid);
    void removeImeisvAssociation(const std::string& imeisv);
    
    // Get all identifiers for a subscriber (by IMSI)
    struct SubscriberIdentifiers {
        std::optional<std::string> imsi;
        std::optional<std::string> tmsi;
        std::optional<uint32_t> enb_ue_s1ap_id;
        std::optional<uint32_t> mme_ue_s1ap_id;
        std::vector<uint32_t> teids;
        std::optional<std::string> imeisv;
    };
    std::optional<SubscriberIdentifiers> getIdentifiersByImsi(const std::string& imsi);
    
    // Get all TEIDs for a subscriber (by IMSI, TMSI, or IMEISV)
    std::vector<uint32_t> getTeidsByImsi(const std::string& imsi);
    std::vector<uint32_t> getTeidsByTmsi(const std::string& tmsi);
    std::vector<uint32_t> getTeidsByImeisv(const std::string& imeisv);
    
    // Get all subscriber records (for iteration)
    const std::unordered_map<uint64_t, SubscriberRecord>& getAllSubscribers() const {
        return subscriber_records_;
    }
    
    // Identifier extraction from S1AP
    std::vector<uint32_t> extractTeidsFromS1ap(const s1ap_parser::S1apParseResult& s1ap_result);
    std::vector<std::string> extractImsisFromS1ap(const s1ap_parser::S1apParseResult& s1ap_result);
    TmsiExtractionResult extractTmsisFromS1ap(const s1ap_parser::S1apParseResult& s1ap_result);
    std::vector<std::string> extractImeisvsFromS1ap(const s1ap_parser::S1apParseResult& s1ap_result);
    std::pair<std::optional<uint32_t>, std::optional<uint32_t>> extractS1apIds(const s1ap_parser::S1apParseResult& s1ap_result);

private:
    // Subscriber records storage (keyed by unique ID)
    std::unordered_map<uint64_t, SubscriberRecord> subscriber_records_;
    uint64_t next_subscriber_id_;
    
    // Identifier to subscriber record ID mappings
    std::unordered_map<std::string, uint64_t> imsi_to_subscriber_id_;
    std::unordered_map<std::string, uint64_t> tmsi_to_subscriber_id_;
    std::unordered_map<uint32_t, uint64_t> enb_ue_s1ap_id_to_subscriber_id_;
    std::unordered_map<uint32_t, uint64_t> mme_ue_s1ap_id_to_subscriber_id_;
    std::unordered_map<uint32_t, uint64_t> teid_to_subscriber_id_;
    std::unordered_map<std::string, uint64_t> imeisv_to_subscriber_id_;
    
    // Mappings: Identifier -> Set of TEIDs
    std::unordered_map<std::string, std::unordered_set<uint32_t>> imsi_to_teids_;
    std::unordered_map<std::string, std::unordered_set<uint32_t>> tmsi_to_teids_;
    std::unordered_map<std::string, std::unordered_set<uint32_t>> imeisv_to_teids_;
    
    // Reverse mappings: TEID -> Identifier
    std::unordered_map<uint32_t, std::string> teid_to_imsi_;
    std::unordered_map<uint32_t, std::string> teid_to_tmsi_;
    std::unordered_map<uint32_t, std::string> teid_to_imeisv_;
    
    // S1AP ID mappings
    std::unordered_map<std::string, uint32_t> imsi_to_mme_ue_s1ap_id_;
    std::unordered_map<std::string, uint32_t> imsi_to_enb_ue_s1ap_id_;
    
    // Note: Hash function for pair is defined in .cpp file
    struct PairHash {
        size_t operator()(const std::pair<uint32_t, uint32_t>& p) const {
            return std::hash<uint32_t>()(p.first) ^ (std::hash<uint32_t>()(p.second) << 1);
        }
    };
    std::unordered_map<std::pair<uint32_t, uint32_t>, std::unordered_set<uint32_t>, PairHash> s1ap_ids_to_teids_;
    
    // Helper functions for identifier extraction from NAS
    std::vector<std::string> extractImsiFromNas(const uint8_t* nas_bytes, size_t len);
    std::vector<std::string> extractTmsiFromNas(const uint8_t* nas_bytes, size_t len);
    std::vector<std::string> extractImeisvFromNas(const uint8_t* nas_bytes, size_t len);
    
    // Normalization functions
    std::string normalizeImsi(const std::string& imsi);
    std::string normalizeTmsi(const std::string& tmsi);
    std::string normalizeImeisv(const std::string& imeisv);
    
};

} // namespace s1ap_correlator

#endif // S1AP_UE_CORRELATOR_H

