#include "sender.h"
#include <iomanip>

using namespace std;
using namespace apsi;
using namespace apsi::oprf;
using namespace apsi::sender;

unique_ptr<CSVReader::DBData> db_data_from_csv(const string &db_file)
{
     CSVReader::DBData db_data;
    try {
        CSVReader reader(db_file);
        tie(db_data, ignore) = reader.read();
    } catch (const exception &ex) {
        APSI_LOG_WARNING("Could not open or read file `" << db_file << "`: " << ex.what());
        return nullptr;
    }

    return make_unique<CSVReader::DBData>(move(db_data));
}

shared_ptr<SenderDB> try_load_csv_db(
    const string &db_file_path,
    const string &params_json, 
    size_t nonce_byte_count, 
    bool compressed)
{
    unique_ptr<PSIParams> params;
    try {
        params = make_unique<PSIParams>(PSIParams::Load(params_json));
    } catch (const exception &ex) {
        APSI_LOG_ERROR("APSI threw an exception creating PSIParams: " << ex.what());
        return nullptr;
    }

    if (!params) {
        // We must have valid parameters given
        APSI_LOG_ERROR("Failed to set PSI parameters");
        return nullptr;
    }

    unique_ptr<CSVReader::DBData> db_data;
    if (db_file_path.empty() || !(db_data = db_data_from_csv(db_file_path))) {
        // Failed to read db file
        APSI_LOG_DEBUG("Failed to load data from a CSV file");
        return nullptr;
    }

    return create_sender_db(
        *db_data, move(params), nonce_byte_count, compressed);
}

shared_ptr<SenderDB> create_sender_db(
    const CSVReader::DBData &db_data, 
    unique_ptr<PSIParams> psi_params, 
    size_t nonce_byte_count, 
    bool compress)
{
    if (!psi_params) {
        APSI_LOG_ERROR("No PSI parameters were given");
        return nullptr;
    }

    shared_ptr<SenderDB> sender_db;
    if (holds_alternative<CSVReader::UnlabeledData>(db_data)) {
        try {
            sender_db = make_shared<SenderDB>(*psi_params, 0, 0, compress);
            sender_db->set_data(get<CSVReader::UnlabeledData>(db_data));

            APSI_LOG_INFO(
                "Created unlabeled SenderDB with " << sender_db->get_item_count() << " items");
        } catch (const exception &ex) {
            APSI_LOG_ERROR("Failed to create SenderDB: " << ex.what());
            return nullptr;
        }
    } else if (holds_alternative<CSVReader::LabeledData>(db_data)) {
        try {
            auto &labeled_db_data = get<CSVReader::LabeledData>(db_data);

            // Find the longest label and use that as label size
            size_t label_byte_count =
                max_element(labeled_db_data.begin(), labeled_db_data.end(), [](auto &a, auto &b) {
                    return a.second.size() < b.second.size();
                })->second.size();

            sender_db =
                make_shared<SenderDB>(*psi_params, label_byte_count, nonce_byte_count, compress);
            sender_db->set_data(labeled_db_data);
            APSI_LOG_INFO(
                "Created labeled SenderDB with " << sender_db->get_item_count() << " items and "
                                                 << label_byte_count << "-byte labels ("
                                                 << nonce_byte_count << "-byte nonces)");
        } catch (const exception &ex) {
            APSI_LOG_ERROR("Failed to create SenderDB: " << ex.what());
            return nullptr;
        }
    } else {
        // Should never reach this point
        APSI_LOG_ERROR("Loaded database is in an invalid state");
        return nullptr;
    }

    if (compress) {
        APSI_LOG_INFO("Using in-memory compression to reduce memory footprint");
    }

    // Read the OPRFKey and strip the SenderDB to reduce memory use , Not NOW
    //oprf_key = sender_db->strip();
    APSI_LOG_INFO("SenderDB packing rate: " << sender_db->get_packing_rate());

    return sender_db;
}

shared_ptr<SenderDB> try_load_csv_uid_db(
    const string &csv_file_path,
    const string &params_json,
    size_t nonce_byte_count,
    bool compressed,
    vector<pair<string, vector<uint8_t>>> &out_table)
{
    // 1) PSIParams 로드
    unique_ptr<PSIParams> params;
    try {
        params = make_unique<PSIParams>(PSIParams::Load(params_json));
    } catch (const exception &ex) {
        APSI_LOG_ERROR("Failed to load PSIParams: " << ex.what());
        return nullptr;
    }

    // 2) CSVReader 에서 LabeledData 꺼내기
    auto dbptr = db_data_from_csv(csv_file_path);
    if (!dbptr || !holds_alternative<CSVReader::LabeledData>(*dbptr)) {
        APSI_LOG_ERROR("Failed to load labeled CSV data");
        return nullptr;
    }
    auto &labeled = get<CSVReader::LabeledData>(*dbptr);
    size_t total = labeled.size();
    if (total == 0) {
        APSI_LOG_ERROR("CSV has no valid entries");
        return nullptr;
    }

    // 3) UID 바이트 수 계산
    size_t uid_bytes = static_cast<size_t>(ceil(log2(double(total) + 1) / 8.0));
    uid_bytes = max<size_t>(1, uid_bytes);
    APSI_LOG_INFO("try_load_csv_uid_db: total_items=" << total
                  << ", uid_bytes=" << uid_bytes);

    // 4) SenderDB 생성 (label_byte_count = uid_bytes)
    auto sender_db = make_shared<SenderDB>(
        *params,
        uid_bytes,
        nonce_byte_count,
        compressed);

    // 5) 원본 (item, raw-UID) 쌍과 out_table 키 준비
    vector<pair<Item, Label>> db_vec;
    db_vec.reserve(total);
    out_table.clear();
    out_table.reserve(total);

    for (size_t i = 0; i < total; ++i) {
        const auto &item_str    = labeled[i].first;   // CSVReader 가 보관한 원본 item
        const auto &orig_label  = labeled[i].second;  // CSVReader 가 보관한 원본 label bytes

        // big-endian raw UID 생성
        vector<uint8_t> uid_raw(uid_bytes);
        uint64_t idx = uint64_t(i) + 1;
        for (size_t b = 0; b < uid_bytes; ++b) {
            uid_raw[uid_bytes - 1 - b] =
                static_cast<uint8_t>((idx >> (8 * b)) & 0xFF);
        }

        // SenderDB 에 집어넣을 (Item, raw-UID) 벡터
        db_vec.emplace_back(Item(item_str), Label(uid_raw));

        // out_table 키(HEX) 자리만 미리 찍어두기
        ostringstream oss;
        oss << hex << uppercase << setfill('0');
        for (auto byte : uid_raw) {
            oss << setw(2) << int(byte);
        }
        out_table.emplace_back(oss.str(), vector<uint8_t>{});
    }

    // 6) SenderDB 초기화
    sender_db->set_data(db_vec);

    // 7) OPRFKey 꺼내기
    auto oprf_key = sender_db->get_oprf_key();

    // 8) 모든 item에 대해 한 번에 PRF 계산
    vector<Item> all_items;
    all_items.reserve(total);
    for (auto &p : db_vec) all_items.push_back(p.first);
    auto all_hashes = oprf::OPRFSender::ComputeHashes(all_items, oprf_key);

    // 9) PRF ⊕ 원본 label → out_table.second 채움
    for (size_t i = 0; i < total; ++i) {
        // 9-a) PRF 바이트열 준비
        auto words = all_hashes[i].get_as<uint64_t>();
        vector<uint8_t> prf;
        prf.reserve(words.size() * sizeof(uint64_t));
        for (auto w : words) {
            for (size_t b = 0; b < sizeof(w); ++b) {
                prf.push_back(uint8_t((w >> (8 * b)) & 0xFF));
            }
        }

        // 9-b) CSVReader 에서 넘어온 원본 label 과 XOR
        const auto &orig_lbl = labeled[i].second;
        auto &masked = out_table[i].second;
        masked.resize(orig_lbl.size());
        for (size_t j = 0; j < orig_lbl.size(); ++j) {
            masked[j] = orig_lbl[j] ^ prf[j % prf.size()];
        }
    }

    APSI_LOG_INFO("Loaded UID‐labeled DB: " << total << " entries");
    return sender_db;
}