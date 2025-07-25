// -----------------------------------------------------------------------------
// CSV Reader Implementation
// -----------------------------------------------------------------------------
// Reads items (and optional labels) from a CSV file, handling quoted fields,
// comma/backslash escapes, and trimming whitespace.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT license.
// -----------------------------------------------------------------------------

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <apsi/log.h>
#include "common_utils.h"
#include "csv_reader.h"

using namespace std;
using namespace apsi;
using namespace apsi::util;

// -----------------------------------------------------------------------------
// Helper: trim whitespace from both ends of a string
// -----------------------------------------------------------------------------
static inline void trim(string &s)
{
    // Remove leading spaces
    s.erase(s.begin(),
            find_if(s.begin(), s.end(), [](int ch) { return !isspace(ch); }));
    // Remove trailing spaces
    s.erase(find_if(s.rbegin(), s.rend(),
                    [](int ch) { return !isspace(ch); })
                .base(),
            s.end());
}

// -----------------------------------------------------------------------------
// Helper: unescape backslash sequences
//    "\\," -> ","
//    "\\\\" -> "\\"
// -----------------------------------------------------------------------------
static inline void unescape_backslash(string &s)
{
    string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char next = s[i + 1];
            if (next == ',' || next == '\\') {
                out.push_back(next);
                ++i; // skip the escape character
                continue;
            }
        }
        out.push_back(s[i]);
    }

    s.swap(out);
}

// -----------------------------------------------------------------------------
// Parse first two CSV fields of a line, handling quotes and escapes
// Returns: {field0, field1}
// -----------------------------------------------------------------------------
static pair<string, string> parse_two_fields(const string &line)
{
    enum class State { Start, InQuotes, QuoteInQuotes };

    vector<string> fields;
    string current;
    State state = State::Start;

    for (size_t i = 0; i < line.size(); ++i) {
        char ch = line[i];
        switch (state) {
            case State::Start:
                if (ch == '"') {
                    state = State::InQuotes;
                } else if (ch == ',') {
                    fields.emplace_back(move(current));
                    current.clear();
                } else {
                    current.push_back(ch);
                }
                break;

            case State::InQuotes:
                if (ch == '"') {
                    state = State::QuoteInQuotes;
                } else {
                    current.push_back(ch);
                }
                break;

            case State::QuoteInQuotes:
                if (ch == '"') {
                    // Escaped quote
                    current.push_back('"');
                    state = State::InQuotes;
                } else if (ch == ',') {
                    fields.emplace_back(move(current));
                    current.clear();
                    state = State::Start;
                } else {
                    // Closing quote, treat char normally
                    state = State::Start;
                    current.push_back(ch);
                }
                break;
        }
    }

    // Append last field
    fields.emplace_back(move(current));

    string first = fields.size() > 0 ? move(fields[0]) : string();
    string second = fields.size() > 1 ? move(fields[1]) : string();
    return { first, second };
}

// -----------------------------------------------------------------------------
// Constructors
// -----------------------------------------------------------------------------
CSVReader::CSVReader() {}

CSVReader::CSVReader(const string &file_name)
    : file_name_(file_name)
{
    throw_if_file_invalid(file_name_);
}

// -----------------------------------------------------------------------------
// Read from istream: returns (DBData, original_items)
// -----------------------------------------------------------------------------
auto CSVReader::read(istream &stream) const -> pair<DBData, vector<string>>
{
    string line;
    DBData result;
    vector<string> orig_items;

    // Read first line
    if (!getline(stream, line)) {
        APSI_LOG_WARNING("Empty CSV: " << file_name_);
        return { UnlabeledData{}, {} };
    }

    // Process first record
    {
        string raw_item, raw_label;
        Item item;
        Label label;
        auto [has_item, has_label] = process_line(line, raw_item, item, label);

        if (!has_item) {
            APSI_LOG_WARNING("Invalid first line in " << file_name_);
            return { UnlabeledData{}, {} };
        }

        orig_items.push_back(move(raw_item));
        if (has_label) {
            result = LabeledData{ make_pair(move(item), move(label)) };
        } else {
            result = UnlabeledData{ move(item) };
        }
    }

    // Process remaining lines
    while (getline(stream, line)) {
        string raw_item, raw_label;
        Item item;
        Label label;
        auto [has_item, has_label] = process_line(line, raw_item, item, label);

        if (!has_item) {
            APSI_LOG_WARNING("Skipping invalid line in " << file_name_);
            continue;
        }

        orig_items.push_back(move(raw_item));
        if (holds_alternative<UnlabeledData>(result)) {
            get<UnlabeledData>(result).push_back(move(item));
        } else {
            get<LabeledData>(result).push_back(make_pair(move(item), move(label)));
        }
    }

    return { move(result), move(orig_items) };
}

// -----------------------------------------------------------------------------
// Read from file by name
// -----------------------------------------------------------------------------
auto CSVReader::read() const -> pair<DBData, vector<string>>
{
    throw_if_file_invalid(file_name_);
    ifstream file(file_name_);
    if (!file.is_open()) {
        APSI_LOG_ERROR("Cannot open CSV: " << file_name_);
        throw runtime_error("Could not open file");
    }
    return read(file);
}

// -----------------------------------------------------------------------------
// Process a single CSV line into item and label
// Returns: (has_item, has_label)
// -----------------------------------------------------------------------------
pair<bool, bool> CSVReader::process_line(
    const string &line,
    string &orig_item,
    Item &item,
    Label &label
) const
{
    // Split into raw_item and raw_label
    auto [raw_item, raw_label] = parse_two_fields(line);

    trim(raw_item);
    trim(raw_label);
    unescape_backslash(raw_item);

    if (raw_item.empty()) {
        return { false, false };
    }

    // Store item
    orig_item = raw_item;
    item = raw_item;  // Item constructor computes hash

    // Store label bytes
    label.clear();
    label.reserve(raw_label.size());
    copy(raw_label.begin(), raw_label.end(), back_inserter(label));

    return { true, !raw_label.empty() };
}