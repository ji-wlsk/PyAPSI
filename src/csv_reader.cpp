// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.

// STD
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// APSI
#include <apsi/log.h>
#include "common_utils.h"
#include "csv_reader.h"

using namespace std;
using namespace apsi;
using namespace apsi::util;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
static inline void trim(string &s)
{
    // leading
    s.erase(s.begin(), find_if(s.begin(), s.end(), [](int ch) { return !isspace(ch); }));
    // trailing
    s.erase(find_if(s.rbegin(), s.rend(), [](int ch) { return !isspace(ch); }).base(), s.end());
}

/// \brief simple unescape: "\\,"→","  and  "\\\\"→"\\"
static inline void unescape_backslash(string &s)
{
    string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == ',' || next == '\\') {
                out.push_back(next);
                ++i; // skip next
                continue;
            }
        }
        out.push_back(s[i]);
    }
    s.swap(out);
}

// ─────────────────────────────────────────────────────────────────────────────
CSVReader::CSVReader() {}

CSVReader::CSVReader(const string &file_name) : file_name_(file_name)
{
    throw_if_file_invalid(file_name_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Core CSV line parser (first two fields)
static pair<string, string> parse_two_fields(const string &line)
{
    enum class State { kStart, kInQuotes, kQuoteInQuotes, kOutQuotes };

    vector<string> fields;
    string current;
    State st = State::kStart;

    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        switch (st) {
        case State::kStart:
            if (ch == '"') {
                st = State::kInQuotes;
            } else if (ch == ',') {
                fields.emplace_back(move(current));
                current.clear();
            } else {
                current.push_back(ch);
            }
            break;
        case State::kInQuotes:
            if (ch == '"') {
                st = State::kQuoteInQuotes;
            } else {
                current.push_back(ch);
            }
            break;
        case State::kQuoteInQuotes:
            if (ch == '"') { // doubled quote
                current.push_back('"');
                st = State::kInQuotes;
            } else if (ch == ',') {
                fields.emplace_back(move(current));
                current.clear();
                st = State::kStart;
            } else { // quote closed, treat char normally
                st = State::kStart;
                if (ch == ',') {
                    fields.emplace_back(move(current));
                    current.clear();
                } else {
                    current.push_back(ch);
                }
            }
            break;
        default:
            break;
        }
    }
    fields.emplace_back(move(current));

    string first = fields.size() > 0 ? move(fields[0]) : string();
    string second = fields.size() > 1 ? move(fields[1]) : string();
    return { first, second };
}

// ─────────────────────────────────────────────────────────────────────────────
// CSVReader::read implementation unchanged (except debug message tweak)
auto CSVReader::read(istream &stream) const -> pair<DBData, vector<string>>
{
    string line;
    DBData result;
    vector<string> orig_items;

    if (!getline(stream, line)) {
        APSI_LOG_WARNING("Nothing to read in `" << file_name_ << "`");
        return { UnlabeledData{}, {} };
    } else {
        string orig_item;
        Item item;
        Label label;
        auto [has_item, has_label] = process_line(line, orig_item, item, label);

        if (!has_item) {
            APSI_LOG_WARNING("Failed to read item from `" << file_name_ << "`");
            return { UnlabeledData{}, {} };
        }

        orig_items.push_back(move(orig_item));
        if (has_label) {
            result = LabeledData{ make_pair(move(item), move(label)) };
        } else {
            result = UnlabeledData{ move(item) };
        }
    }

    while (getline(stream, line)) {
        string orig_item;
        Item item;
        Label label;
        auto [has_item, has_label] = process_line(line, orig_item, item, label);

        string label_str = has_label ? string(label.begin(), label.end()) : string();
        // APSI_LOG_DEBUG("CSVReader::process_line line='" << line << "' orig_item='" << orig_item
        //                              << "' label='" << label_str << "'");

        if (!has_item) {
            APSI_LOG_WARNING("Failed to read item from `" << file_name_ << "`");
            continue;
        }

        orig_items.push_back(move(orig_item));
        if (holds_alternative<UnlabeledData>(result)) {
            get<UnlabeledData>(result).push_back(move(item));
        } else {
            get<LabeledData>(result).push_back(make_pair(move(item), move(label)));
        }
    }

    return { move(result), move(orig_items) };
}

auto CSVReader::read() const -> pair<DBData, vector<string>>
{
    throw_if_file_invalid(file_name_);

    ifstream file(file_name_);
    if (!file.is_open()) {
        APSI_LOG_ERROR("File `" << file_name_ << "` could not be opened for reading");
        throw runtime_error("could not open file");
    }

    return read(file);
}

// ─────────────────────────────────────────────────────────────────────────────
// process_line
//   - handles quoted fields, doubled quotes, backslash escapes
//   - returns {has_item, has_label}
//
pair<bool, bool> CSVReader::process_line(
    const string &line, string &orig_item, Item &item, Label &label) const
{
    // 1) split first two fields respecting quotes
    auto [raw_item, raw_label] = parse_two_fields(line);

    trim(raw_item);
    trim(raw_label);

    // 2) unescape backslash sequences in the CPE (optional but keeps UI clean)
    unescape_backslash(raw_item);

    if (raw_item.empty()) {
        return { false, false };
    }

    // store original (trimmed, unescaped) item string
    orig_item = raw_item;
    item      = raw_item; // Item ctor hashes automatically

    // 3) populate label (vector<uint8_t>)
    label.clear();
    label.reserve(raw_label.size());
    copy(raw_label.begin(), raw_label.end(), back_inserter(label));

    return { true, !raw_label.empty() };
}

