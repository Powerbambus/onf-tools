#pragma once

#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "doctest.h"

#ifndef ONF_VECTORS_DIR
#error "ONF_VECTORS_DIR must point to tests/vectors"
#endif

namespace onf::test {

using Row = std::vector<std::string>;

inline std::vector<Row> load_vectors(const std::string& file) {
    const std::string path = std::string(ONF_VECTORS_DIR) + "/" + file;
    std::ifstream in(path);
    REQUIRE_MESSAGE(in.is_open(), "missing vector file: " << path);

    std::vector<Row> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        Row row;
        std::stringstream cells(line);
        std::string cell;
        while (std::getline(cells, cell, '\t')) row.push_back(cell);
        rows.push_back(std::move(row));
    }
    REQUIRE_MESSAGE(!rows.empty(), "no vectors in " << path);
    return rows;
}

inline std::vector<std::uint8_t> parse_hex_bytes(const std::string& text) {
    std::vector<std::uint8_t> out;
    std::stringstream in(text);
    std::string token;
    while (in >> token) out.push_back(static_cast<std::uint8_t>(std::stoul(token, nullptr, 16)));
    return out;
}

}  // namespace onf::test
