#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <string_view>
#include <filesystem>
#include <execution>
#include <algorithm>
#include <sstream>
#include <chrono>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <array>

// C++20 / C++23 Standard Headers
#include <span>
#include <expected>
#include <print>

// Third-Party Libraries
#include "simdjson.h"
#include "json.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#error "Native Shift-JIS conversion in this code relies on Windows APIs (CP 932). Implement iconv for non-Windows."
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// Shift-JIS <-> UTF-8 Conversion (Windows API)
// -----------------------------------------------------------------------------
std::string sjis_to_utf8(std::string_view sjis) {
    if (sjis.empty()) return "";
    int size_needed = MultiByteToWideChar(932, 0, sjis.data(), static_cast<int>(sjis.size()), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(932, 0, sjis.data(), static_cast<int>(sjis.size()), &wstr[0], size_needed);

    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
    std::string utf8(utf8_size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), static_cast<int>(wstr.size()), &utf8[0], utf8_size, NULL, NULL);
    return utf8;
}

std::string utf8_to_sjis(std::string_view utf8) {
    if (utf8.empty()) return "";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), NULL, 0);
    std::wstring wstr(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), &wstr[0], size_needed);

    int sjis_size = WideCharToMultiByte(932, 0, wstr.c_str(), static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
    std::string sjis(sjis_size, 0);
    WideCharToMultiByte(932, 0, wstr.c_str(), static_cast<int>(wstr.size()), &sjis[0], sjis_size, NULL, NULL);
    return sjis;
}

// -----------------------------------------------------------------------------
// Binary Struct Layout
// -----------------------------------------------------------------------------
struct NekoStr {
    uint32_t len = 0;
    std::string raw; // Shift-JIS raw bytes
};

struct Node {
    uint32_t id = 0;
    uint32_t type1 = 0;
    uint32_t some_ofs = 0;
    uint32_t opcode = 0;
    char spacer1[128]{};
    uint32_t next_id = 0;
    char spacer2[64]{};
    std::array<NekoStr, 33> strs; // Zero-cost inline allocation
};

struct Script {
    char magic[19]{};
    uint32_t nodes_qty = 0;
    std::vector<Node> nodes;
};

enum class ParseError {
    FileNotFound,
    FileTooSmall,
    InvalidHeader,
    CorruptedData
};

// -----------------------------------------------------------------------------
// String Manipulation Helpers
// -----------------------------------------------------------------------------
std::string clean_text(std::string s) {
    // Remove null bytes
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    
    // Replace \r\n with \n
    size_t pos = 0;
    while ((pos = s.find("\r\n", pos)) != std::string::npos) {
        s.replace(pos, 2, "\n");
        pos += 1;
    }

    // Helper to strip ASCII spaces AND UTF-8 Japanese Full-Width Spaces (\xE3\x80\x80)
    auto trim_left = [](std::string& str) {
        size_t start = 0;
        while (start < str.size()) {
            if (std::strchr(" \t\n\r", str[start])) {
                start += 1;
            } else if (start + 2 < str.size() && str.compare(start, 3, "\xE3\x80\x80") == 0) {
                start += 3;
            } else {
                break;
            }
        }
        str.erase(0, start);
    };

    auto trim_right = [](std::string& str) {
        while (!str.empty()) {
            if (std::strchr(" \t\n\r", str.back())) {
                str.pop_back();
            } else if (str.size() >= 3 && str.compare(str.size() - 3, 3, "\xE3\x80\x80") == 0) {
                str.erase(str.size() - 3);
            } else {
                break;
            }
        }
    };

    trim_left(s);
    trim_right(s);
    return s;
}

std::string ensure_null_terminated(std::string s) {
    if (s.empty() || s.back() != '\0') {
        s.push_back('\0');
    }
    return s;
}

std::string wrap_text(std::string text, size_t max_len) {
    if (text.empty()) return text;
    std::replace(text.begin(), text.end(), '\n', ' ');

    std::vector<std::string> words;
    std::string word;
    std::istringstream stream(text);
    while (stream >> word) {
        words.push_back(word);
    }

    std::vector<std::string> lines;
    std::string current_line;

    for (const auto& w : words) {
        if (!current_line.empty() && current_line.length() + w.length() + 1 > max_len) {
            while (!current_line.empty() && current_line.back() == ' ') current_line.pop_back();
            lines.push_back(current_line);
            current_line = w + " ";
        } else {
            current_line += w + " ";
        }
    }
    if (!current_line.empty()) {
        while (!current_line.empty() && current_line.back() == ' ') current_line.pop_back();
        lines.push_back(current_line);
    }

    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i + 1 < lines.size()) {
            result += " \r\n ";
        }
    }
    return result;
}

// -----------------------------------------------------------------------------
// Binary I/O Operations (C++23 span & expected)
// -----------------------------------------------------------------------------
std::expected<std::vector<uint8_t>, ParseError> read_file_bytes(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::unexpected(ParseError::FileNotFound);

    auto size = f.tellg();
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!f.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return std::unexpected(ParseError::CorruptedData);
    }
    return buffer;
}

std::expected<Script, ParseError> parse_script_buffer(std::span<const uint8_t> buffer) {
    if (buffer.size() < 23) {
        return std::unexpected(ParseError::FileTooSmall);
    }

    if (std::memcmp(buffer.data(), "NEKOSDK_ADVSCRIPT2\x00", 19) != 0) {
        return std::unexpected(ParseError::InvalidHeader);
    }

    Script scr;
    std::memcpy(scr.magic, buffer.data(), 19);

    size_t offset = 19;
    std::memcpy(&scr.nodes_qty, buffer.data() + offset, 4);
    offset += 4;

    scr.nodes.resize(scr.nodes_qty);

    for (size_t i = 0; i < scr.nodes_qty; ++i) {
        if (offset + 4 + 4 + 4 + 4 + 128 + 4 + 64 > buffer.size()) {
            return std::unexpected(ParseError::CorruptedData);
        }

        auto& node = scr.nodes[i];
        
        // 100% Safe memory copies (zero Undefined Behavior)
        std::memcpy(&node.id, buffer.data() + offset, 4);       offset += 4;
        std::memcpy(&node.type1, buffer.data() + offset, 4);    offset += 4;
        std::memcpy(&node.some_ofs, buffer.data() + offset, 4); offset += 4;
        std::memcpy(&node.opcode, buffer.data() + offset, 4);   offset += 4;

        std::memcpy(node.spacer1, buffer.data() + offset, 128); offset += 128;
        std::memcpy(&node.next_id, buffer.data() + offset, 4);  offset += 4;
        std::memcpy(node.spacer2, buffer.data() + offset, 64);  offset += 64;

        for (size_t s = 0; s < 33; ++s) {
            if (offset + 4 > buffer.size()) return std::unexpected(ParseError::CorruptedData);

            uint32_t str_len;
            std::memcpy(&str_len, buffer.data() + offset, 4);
            offset += 4;
            node.strs[s].len = str_len;

            if (offset + str_len > buffer.size()) return std::unexpected(ParseError::CorruptedData);

            node.strs[s].raw.assign(reinterpret_cast<const char*>(buffer.data() + offset), str_len);
            offset += str_len;
        }
    }

    return scr;
}

void write_script(const fs::path& path, const Script& scr) {
    std::ofstream f(path, std::ios::binary);
    f.write(scr.magic, 19);
    f.write(reinterpret_cast<const char*>(&scr.nodes_qty), 4);

    for (const auto& node : scr.nodes) {
        f.write(reinterpret_cast<const char*>(&node.id), 4);
        f.write(reinterpret_cast<const char*>(&node.type1), 4);
        f.write(reinterpret_cast<const char*>(&node.some_ofs), 4);
        f.write(reinterpret_cast<const char*>(&node.opcode), 4);
        f.write(node.spacer1, 128);
        f.write(reinterpret_cast<const char*>(&node.next_id), 4);
        f.write(node.spacer2, 64);

        for (const auto& s : node.strs) {
            uint32_t len = static_cast<uint32_t>(s.raw.size());
            f.write(reinterpret_cast<const char*>(&len), 4);
            f.write(s.raw.data(), len);
        }
    }
}

// -----------------------------------------------------------------------------
// Core Multithreaded Routines
// -----------------------------------------------------------------------------
void extract_all(const fs::path& script_dir, const fs::path& json_dir) {
    fs::create_directories(json_dir);

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(script_dir)) {
        if (entry.path().extension() == ".txt") files.push_back(entry.path());
    }

    std::for_each(std::execution::par, files.begin(), files.end(), [&](const fs::path& input_path) {
        auto buffer = read_file_bytes(input_path);
        if (!buffer) return;

        auto scr = parse_script_buffer(*buffer);
        if (!scr) {
            std::println("[SKIP] {} is not a valid NekoSDK binary file (invalid header).", input_path.filename().string());
            return;
        }

        json out = json::array();
        for (const auto& node : scr->nodes) {
            if (node.opcode == 5) {
                std::string character = (node.strs[1].len > 0) ? clean_text(sjis_to_utf8(node.strs[1].raw)) : "";
                std::string original = (node.strs[2].len > 0) ? clean_text(sjis_to_utf8(node.strs[2].raw)) : "";

                out.push_back({
                    {"node_id", node.id},
                    {"character", character},
                    {"character_eng", ""},
                    {"original", original},
                    {"translation", ""}
                });
            }
        }

        fs::path output_path = json_dir / input_path.filename().replace_extension(".json");
        if (!out.empty()) {
            std::ofstream o(output_path);
            o << out.dump(2);
            std::println("[EXTRACT] {} -> {} ({} entries)", input_path.filename().string(), output_path.filename().string(), out.size());
        } else {
            std::println("[SKIP] {} contained no dialogue nodes.", input_path.filename().string());
        }
    });
}

void recompile_all(const fs::path& script_dir, const fs::path& json_dir, const fs::path& output_dir, size_t wrap_ch, bool force) {
    fs::create_directories(output_dir);

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(script_dir)) {
        if (entry.path().extension() == ".txt") files.push_back(entry.path());
    }

    std::atomic<int> compiled_count = 0;
    std::atomic<int> skipped_count = 0;

    std::for_each(std::execution::par, files.begin(), files.end(), [&](const fs::path& input_path) {
        fs::path json_path = json_dir / input_path.filename().replace_extension(".json");
        fs::path output_path = output_dir / input_path.filename();

        if (!fs::exists(json_path)) {
            std::println("[SKIP] Missing JSON for {}", input_path.filename().string());
            return;
        }

        if (!force && fs::exists(output_path)) {
            auto json_mtime = fs::last_write_time(json_path);
            auto input_mtime = fs::last_write_time(input_path);
            auto output_mtime = fs::last_write_time(output_path);

            if (json_mtime <= output_mtime && input_mtime <= output_mtime) {
                skipped_count++;
                return;
            }
        }

        auto buffer = read_file_bytes(input_path);
        if (!buffer) return;

        auto scr = parse_script_buffer(*buffer);
        if (!scr) {
            std::println("[SKIP] {} is not a valid NekoSDK binary file (invalid header).", input_path.filename().string());
            return;
        }

        // SIMDJSON parsing
        simdjson::ondemand::parser parser;
        simdjson::padded_string json_data;
        if (simdjson::padded_string::load(json_path.string()).get(json_data)) return;

        simdjson::ondemand::document doc;
        if (parser.iterate(json_data).get(doc)) return;

        // Data-Oriented Optimization struct
        struct EntryData {
            std::string character_sjis;
            std::string translation_sjis;
            std::string original;
        };
        std::unordered_map<uint32_t, EntryData> by_id;

        for (simdjson::ondemand::object entry : doc) {
            uint64_t raw_id;
            if (!entry["node_id"].get_uint64().get(raw_id)) {
                uint32_t node_id = static_cast<uint32_t>(raw_id);
                EntryData ed;
                
                std::string_view tmp;
                std::string char_eng, char_jp, trans;

                if (!entry["character_eng"].get_string().get(tmp)) char_eng = std::string(tmp);
                if (!entry["character"].get_string().get(tmp))     char_jp = std::string(tmp);
                if (!entry["original"].get_string().get(tmp))      ed.original = std::string(tmp);
                if (!entry["translation"].get_string().get(tmp))   trans = std::string(tmp);

                // Pre-compute Character SJIS
                std::string char_to_use = clean_text(!char_eng.empty() ? char_eng : char_jp);
                if (!char_to_use.empty()) {
                    ed.character_sjis = utf8_to_sjis(ensure_null_terminated(char_to_use));
                }

                // Pre-compute Translation SJIS
                trans = clean_text(trans);
                if (!trans.empty()) {
                    trans = wrap_text(trans, wrap_ch);
                    ed.translation_sjis = utf8_to_sjis(ensure_null_terminated(trans));
                }

                by_id[node_id] = ed;
            }
        }

        int replaced = 0;
        int warned = 0;

        // The Ultra-Fast Hot Loop
        for (auto& node : scr->nodes) {
            if (node.opcode != 5) continue;

            auto it = by_id.find(node.id);
            if (it == by_id.end()) continue;

            const auto& entry = it->second;

            // Sanity check against original text
            std::string current_original = (node.strs[2].len > 0) ? clean_text(sjis_to_utf8(node.strs[2].raw)) : "";
            if (!entry.original.empty() && entry.original != current_original) {
                warned++;
                std::println("[WARN] Original mismatch at node {}", node.id);
            }

            // 1. Inject Character Name (Slot 1)
            if (!entry.character_sjis.empty()) {
                node.strs[1].raw = entry.character_sjis;
                node.strs[1].len = static_cast<uint32_t>(entry.character_sjis.size());
            }

            // 2. Inject Translated Dialogue (Slot 2)
            if (!entry.translation_sjis.empty()) {
                node.strs[2].raw = entry.translation_sjis;
                node.strs[2].len = static_cast<uint32_t>(entry.translation_sjis.size());
                replaced++;
            }
        }

        write_script(output_path, *scr);
        compiled_count++;
        std::println("[RECOMPILE] {} -> {} ({} replacements, {} warnings)", input_path.filename().string(), output_path.filename().string(), replaced, warned);
    });

    if (skipped_count > 0) {
        std::println("[INFO] Skipped {} unmodified file(s) to save time.", skipped_count.load());
    }
    if (compiled_count == 0) {
        std::println("No scene files updated, skipping copying");
        return;
    }
    std::println("\n[DONE] Successfully recompiled {} file(s).", compiled_count.load());
}

// -----------------------------------------------------------------------------
// Main Entrypoint
// -----------------------------------------------------------------------------
int main() {
    fs::path script_dir = "scr";
    fs::path json_dir = "translation_jsons";
    fs::path output_dir = "output";

    fs::create_directories(script_dir);
    fs::create_directories(json_dir);
    fs::create_directories(output_dir);

    std::string command;
    std::print("extract (et), recompile (re), or force-recompile (fre): ");
    std::cin >> command;

    auto start = std::chrono::high_resolution_clock::now();

    if (command == "extract" || command == "et") {
        extract_all(script_dir, json_dir);
    } else if (command == "recompile" || command == "re") {
        recompile_all(script_dir, json_dir, output_dir, 77, false);
    } else if (command == "force-recompile" || command == "fre") {
        std::println("\n[WARNING] Forcing recompilation of ALL files regardless of modification dates...\n");
        recompile_all(script_dir, json_dir, output_dir, 77, true);
    } else {
        std::println("Invalid COMMAND");
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff = end - start;
    std::println("Task finished in {:.2f} ms", diff.count());

    std::println("\nPress Enter to exit...");
    std::cin.ignore(10000, '\n'); 
    std::cin.get();               
    return 0;
}