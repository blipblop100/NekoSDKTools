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
#include <format>
#include <bit>

// Third-Party Libraries
#include "simdjson.h"

#ifdef _WIN32
#include <windows.h>
#else
#error "Native Shift-JIS conversion in this code relies on Windows APIs (CP 932). Implement iconv for non-Windows."
#endif

namespace fs = std::filesystem;

// =============================================================================
// GLOBAL CONFIGURATION (Loaded from config.ini)
// =============================================================================
struct Configuration {
    fs::path SCRIPT_DIR = "scr";
    fs::path JSON_DIR = "jp_scripts_with_translations";
    fs::path OUTPUT_DIR = "output";

    size_t TEXT_WRAP_CH = 77;
    bool CLEAN_EXISTING_NEWLINES = true;
    float FILL_WINDOW = 0.65f;

    bool RECOMPILE_MAIN = true;
};

Configuration Config;

// =============================================================================
// String Manipulation Helpers
// =============================================================================
std::string ltrim_copy(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    return s;
}

std::string rtrim_copy(std::string s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

// -----------------------------------------------------------------------------
// Simple INI Parser
// -----------------------------------------------------------------------------
void load_or_create_config() {
    if (!fs::exists("config.ini")) {
        std::ofstream out("config.ini");
        out << "; NekoSDK Tool Configuration File\n\n"
            << "[Directories]\n"
            << "SCRIPT_DIR=scr\n"
            << "JSON_DIR=jp_scripts_with_translations\n"
            << "OUTPUT_DIR=output\n\n"
            << "[TextWrapping]\n"
            << "TEXT_WRAP_CH=77\n"
            << "CLEAN_EXISTING_NEWLINES=true\n"
            << "FILL_WINDOW=0.65\n\n"
            << "[Optional]\n"
            << "RECOMPILE_MAIN=true\n";
        std::println("[INFO] Generated default config.ini");
        return;
    }

    std::ifstream f("config.ini");
    std::string line;
    while (std::getline(f, line)) {
        std::string cleaned = ltrim_copy(rtrim_copy(line));
        // Skip empty lines, comments, and INI section headers
        if (cleaned.empty() || cleaned[0] == ';' || cleaned[0] == '#' || cleaned[0] == '[') continue;

        size_t delim = cleaned.find('=');
        if (delim == std::string::npos) continue;

        std::string key = rtrim_copy(cleaned.substr(0, delim));
        std::string val = ltrim_copy(cleaned.substr(delim + 1));

        std::string val_lower = val;
        std::transform(val_lower.begin(), val_lower.end(), val_lower.begin(), ::tolower);

        try {
            if (key == "SCRIPT_DIR") Config.SCRIPT_DIR = val;
            else if (key == "JSON_DIR") Config.JSON_DIR = val;
            else if (key == "OUTPUT_DIR") Config.OUTPUT_DIR = val;
            else if (key == "TEXT_WRAP_CH") Config.TEXT_WRAP_CH = std::stoull(val);
            else if (key == "CLEAN_EXISTING_NEWLINES") Config.CLEAN_EXISTING_NEWLINES = (val_lower == "true" || val == "1");
            else if (key == "FILL_WINDOW") Config.FILL_WINDOW = std::stof(val);
            else if (key == "RECOMPILE_MAIN") Config.RECOMPILE_MAIN = (val_lower == "true" || val == "1");
        } catch (const std::exception& e) {
            std::println("[WARN] Failed to parse config key '{}': {}", key, e.what());
        }
    }
    std::println("[INFO] Loaded configuration from config.ini");
}

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

std::string clean_text(std::string s) {
    s.erase(std::remove(s.begin(), s.end(), '\0'), s.end());
    size_t pos = 0;
    while ((pos = s.find("\r\n", pos)) != std::string::npos) {
        s.replace(pos, 2, "\n");
        pos += 1;
    }

    auto trim_left = [](std::string& str) {
        size_t start = 0;
        while (start < str.size()) {
            if (std::strchr(" \t\n\r", str[start])) start += 1;
            else if (start + 2 < str.size() && str.compare(start, 3, "\xE3\x80\x80") == 0) start += 3;
            else break;
        }
        str.erase(0, start);
    };

    auto trim_right = [](std::string& str) {
        while (!str.empty()) {
            if (std::strchr(" \t\n\r", str.back())) str.pop_back();
            else if (str.size() >= 3 && str.compare(str.size() - 3, 3, "\xE3\x80\x80") == 0) str.erase(str.size() - 3);
            else break;
        }
    };

    trim_left(s);
    trim_right(s);
    return s;
}

std::string escape_json(const std::string& s) {
    std::string res;
    res.reserve(s.size() + 10);
    for (char c : s) {
        switch (c) {
            case '"':  res += "\\\""; break;
            case '\\': res += "\\\\"; break;
            case '\b': res += "\\b";  break;
            case '\f': res += "\\f";  break;
            case '\n': res += "\\n";  break;
            case '\r': res += "\\r";  break;
            case '\t': res += "\\t";  break;
            default:   res += c;      break;
        }
    }
    return res;
}

std::string ensure_null_terminated(std::string s) {
    if (s.empty() || s.back() != '\0') s.push_back('\0');
    return s;
}

size_t find_sentence_break(std::string_view search_area, size_t min_len) {
    size_t best_break = std::string::npos;
    for (size_t i = search_area.size(); i >= min_len && i > 0; --i) {
        if (i == search_area.size() || std::isspace(static_cast<unsigned char>(search_area[i]))) {
            size_t p = i;
            while (p > 0 && std::isspace(static_cast<unsigned char>(search_area[p-1]))) p--;
            
            while (p > 0) {
                char c = search_area[p-1];
                if (c == ']' || c == ')' || c == '\'' || c == '"') { p--; continue; }
                if (p >= 3 && search_area.compare(p-3, 3, "\xE2\x80\x9D") == 0) { p -= 3; continue; } // UTF-8 ”
                break;
            }
            
            if (p > 0 && (search_area[p-1] == '.' || search_area[p-1] == '!' || search_area[p-1] == '?')) {
                if (p > 1 && search_area[p-2] == '.') continue;
                best_break = i;
                break;
            }
        }
    }
    return best_break;
}

std::string wrap_text(std::string text, size_t max_len, float min_fill) {
    if (text.empty() || max_len == 0) return text;

    if (Config.CLEAN_EXISTING_NEWLINES) {
        std::istringstream iss(text);
        std::string word, result;
        while (iss >> word) {
            if (!result.empty()) result += " ";
            result += word;
        }
        text = result;
    }

    if (text.length() <= max_len) return text;

    std::string rn = "\n";
    if (text.starts_with("\xE3\x80\x8C") || text.starts_with("\xEF\xBC\x88") || text.starts_with("\xE3\x80\x8A")) {
        rn = "\n ";
    }

    std::vector<std::string> lines;
    size_t min_len = static_cast<size_t>(max_len * min_fill);

    while (text.size() > max_len) {
        std::string_view search_area(text.data(), max_len);
        size_t best_break = find_sentence_break(search_area, min_len);

        if (best_break != std::string::npos) {
            lines.push_back(rtrim_copy(std::string(text.substr(0, best_break))));
            text = ltrim_copy(text.substr(best_break));
            continue;
        }

        size_t last_space = search_area.find_last_of(" \t");
        if (last_space != std::string::npos) {
            lines.push_back(rtrim_copy(std::string(text.substr(0, last_space))));
            text = ltrim_copy(text.substr(last_space));
        } else {
            lines.push_back(std::string(text.substr(0, max_len)));
            text = ltrim_copy(text.substr(max_len));
        }
    }

    if (!text.empty()) {
        lines.push_back(rtrim_copy(text));
    }

    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        result += lines[i];
        if (i + 1 < lines.size()) result += rn;
    }
    return result;
}

std::string make_text_main(const std::string& name, const std::string& message, const std::string& old_main) {
    std::string voice = "";
    std::istringstream stream(old_main);
    std::string line;
    
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.starts_with("[テキスト表示]")) {
            std::istringstream line_stream(line);
            std::string part;
            bool first = true;
            while (line_stream >> part) {
                if (first) { first = false; continue; }
                if (part.find('\\') != std::string::npos || part.find('/') != std::string::npos) {
                    voice = part;
                    break;
                }
            }
            break;
        }
    }
    
    std::string first = "[テキスト表示]";
    if (!name.empty()) first += " " + name;
    if (!voice.empty()) first += " " + voice;
    first = rtrim_copy(first);

    std::string display_message = message;
    size_t pos = 0;
    while ((pos = display_message.find("\r\n", pos)) != std::string::npos) {
        display_message.replace(pos, 2, "\n");
        pos += 1;
    }

    return first + "\n" + display_message + "\n\n";
}

// =============================================================================
// Binary Struct Layout & Fast IO
// =============================================================================
struct NekoStr {
    uint32_t len = 0;
    std::string raw; 
};

struct Node {
    uint32_t id = 0, type1 = 0, some_ofs = 0, opcode = 0;
    char spacer1[128]{};
    uint32_t next_id = 0;
    char spacer2[64]{};
    std::array<NekoStr, 33> strs; 
};

struct Script {
    char magic[19]{};
    uint32_t nodes_qty = 0;
    std::vector<Node> nodes;
};

enum class ParseError { FileNotFound, FileTooSmall, InvalidHeader, CorruptedData };

std::expected<std::vector<uint8_t>, ParseError> read_file_bytes(const fs::path& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return std::unexpected(ParseError::FileNotFound);
    auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(size);
    if (!f.read(reinterpret_cast<char*>(buffer.data()), size)) return std::unexpected(ParseError::CorruptedData);
    return buffer;
}

std::expected<Script, ParseError> parse_script_buffer(std::span<const uint8_t> buffer) {
    if (buffer.size() < 23) return std::unexpected(ParseError::FileTooSmall);
    if (std::memcmp(buffer.data(), "NEKOSDK_ADVSCRIPT2\x00", 19) != 0) return std::unexpected(ParseError::InvalidHeader);

    Script scr;
    std::memcpy(scr.magic, buffer.data(), 19);
    size_t offset = 19;
    std::memcpy(&scr.nodes_qty, buffer.data() + offset, 4);
    offset += 4;
    scr.nodes.resize(scr.nodes_qty);

    for (size_t i = 0; i < scr.nodes_qty; ++i) {
        if (offset + 212 > buffer.size()) return std::unexpected(ParseError::CorruptedData);
        auto& node = scr.nodes[i];
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

void write_u32_le(std::ofstream& f, uint32_t val) {
    if constexpr (std::endian::native != std::endian::little) val = std::byteswap(val);
    f.write(reinterpret_cast<const char*>(&val), 4);
}

void write_script(const fs::path& path, const Script& scr) {
    std::ofstream f(path, std::ios::binary);
    f.write(scr.magic, 19);
    write_u32_le(f, scr.nodes_qty);
    for (const auto& node : scr.nodes) {
        write_u32_le(f, node.id);
        write_u32_le(f, node.type1);
        write_u32_le(f, node.some_ofs);
        write_u32_le(f, node.opcode);
        f.write(node.spacer1, 128);
        write_u32_le(f, node.next_id);
        f.write(node.spacer2, 64);
        for (const auto& s : node.strs) {
            write_u32_le(f, static_cast<uint32_t>(s.raw.size()));
            f.write(s.raw.data(), s.raw.size());
        }
    }
}

// =============================================================================
// Core Multithreaded Routines
// =============================================================================
void extract_all() {
    fs::create_directories(Config.JSON_DIR);
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(Config.SCRIPT_DIR)) {
        if (entry.path().extension() == ".txt") files.push_back(entry.path());
    }

    std::for_each(std::execution::par, files.begin(), files.end(), [&](const fs::path& input_path) {
        auto buffer = read_file_bytes(input_path);
        if (!buffer) return;

        auto scr = parse_script_buffer(*buffer);
        if (!scr) {
            std::println("[SKIP] {} is not a valid NekoSDK binary.", input_path.filename().string());
            return;
        }

        std::string json_out = "[\n";
        bool first = true;
        int entry_count = 0;

        for (const auto& node : scr->nodes) {
            if (node.opcode == 5) {
                if (!first) json_out += ",\n";
                first = false;
                entry_count++;

                std::string character = (node.strs[1].len > 0) ? clean_text(sjis_to_utf8(node.strs[1].raw)) : "";
                std::string original = (node.strs[2].len > 0) ? clean_text(sjis_to_utf8(node.strs[2].raw)) : "";

                json_out += std::format(R"(  {{
    "node_id": {},
    "character": "{}",
    "character_eng": "",
    "original": "{}",
    "translation": ""
  }})", node.id, escape_json(character), escape_json(original));
            }
        }
        json_out += "\n]";

        fs::path output_path = Config.JSON_DIR / input_path.filename().replace_extension(".json");
        if (entry_count > 0) {
            std::ofstream o(output_path);
            o << json_out;
            std::println("[EXTRACT] {} -> {} ({} entries)", input_path.filename().string(), output_path.filename().string(), entry_count);
        } else {
            std::println("[SKIP] {} contained no dialogue nodes.", input_path.filename().string());
        }
    });
}

void recompile_all(bool force) {
    fs::create_directories(Config.OUTPUT_DIR);
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(Config.SCRIPT_DIR)) {
        if (entry.path().extension() == ".txt") files.push_back(entry.path());
    }

    std::atomic<int> compiled_count = 0;
    std::atomic<int> skipped_count = 0;

    std::for_each(std::execution::par, files.begin(), files.end(), [&](const fs::path& input_path) {
        fs::path json_path = Config.JSON_DIR / input_path.filename().replace_extension(".json");
        fs::path output_path = Config.OUTPUT_DIR / input_path.filename();

        if (!fs::exists(json_path)) {
            std::println("[SKIP] Missing JSON for {}", input_path.filename().string());
            return;
        }

        if (!force && fs::exists(output_path)) {
            if (fs::last_write_time(json_path) <= fs::last_write_time(output_path) &&
                fs::last_write_time(input_path) <= fs::last_write_time(output_path)) {
                skipped_count++;
                return;
            }
        }

        auto buffer = read_file_bytes(input_path);
        if (!buffer) return;

        auto scr = parse_script_buffer(*buffer);
        if (!scr) return;

        simdjson::ondemand::parser parser;
        simdjson::padded_string json_data;
        if (auto error = simdjson::padded_string::load(json_path.string()).get(json_data); error) {
            std::println("[ERROR] Failed to read JSON {}: {}", json_path.filename().string(), simdjson::error_message(error));
            return;
        }

        simdjson::ondemand::document doc;
        if (auto error = parser.iterate(json_data).get(doc); error) {
            std::println("[ERROR] Failed to parse JSON {}: {}", json_path.filename().string(), simdjson::error_message(error));
            return;
        }

        struct EntryData {
            std::string character_sjis;
            std::string translation_sjis;
            std::string character_utf8;
            std::string translation_utf8;
            std::string original;
        };
        std::unordered_map<uint32_t, EntryData> by_id;

        for (simdjson::ondemand::object entry : doc) {
            uint64_t raw_id;
            if (!entry["node_id"].get_uint64().get(raw_id)) {
                EntryData ed;
                std::string_view tmp;
                std::string char_eng, char_jp, trans;

                if (!entry["character_eng"].get_string().get(tmp)) char_eng = std::string(tmp);
                if (!entry["character"].get_string().get(tmp))     char_jp = std::string(tmp);
                if (!entry["original"].get_string().get(tmp))      ed.original = std::string(tmp);
                if (!entry["translation"].get_string().get(tmp))   trans = std::string(tmp);

                ed.character_utf8 = clean_text(!char_eng.empty() ? char_eng : char_jp);
                if (!ed.character_utf8.empty()) {
                    ed.character_sjis = utf8_to_sjis(ensure_null_terminated(ed.character_utf8));
                }

                ed.translation_utf8 = clean_text(trans);
                if (!ed.translation_utf8.empty()) {
                    ed.translation_utf8 = wrap_text(ed.translation_utf8, Config.TEXT_WRAP_CH, Config.FILL_WINDOW);
                    ed.translation_sjis = utf8_to_sjis(ensure_null_terminated(ed.translation_utf8));
                }

                by_id[static_cast<uint32_t>(raw_id)] = ed;
            }
        }

        int replaced = 0, warned = 0;

        for (auto& node : scr->nodes) {
            if (node.opcode != 5) continue;
            auto it = by_id.find(node.id);
            if (it == by_id.end()) continue;

            const auto& entry = it->second;

            std::string current_original = (node.strs[2].len > 0) ? clean_text(sjis_to_utf8(node.strs[2].raw)) : "";
            if (!entry.original.empty() && entry.original != current_original) {
                warned++;
                std::println("[WARN] Original mismatch at node {}", node.id);
            }

            if (!entry.character_sjis.empty()) {
                node.strs[1].raw = entry.character_sjis;
                node.strs[1].len = static_cast<uint32_t>(entry.character_sjis.size());
            }

            if (!entry.translation_sjis.empty()) {
                node.strs[2].raw = entry.translation_sjis;
                node.strs[2].len = static_cast<uint32_t>(entry.translation_sjis.size());
                replaced++;

                if (Config.RECOMPILE_MAIN && node.strs[0].len > 0) {
                    std::string old_main_utf8 = clean_text(sjis_to_utf8(node.strs[0].raw));
                    std::string new_main = make_text_main(entry.character_utf8, entry.translation_utf8, old_main_utf8);
                    std::string new_main_sjis = utf8_to_sjis(ensure_null_terminated(new_main));
                    
                    node.strs[0].raw = new_main_sjis;
                    node.strs[0].len = static_cast<uint32_t>(new_main_sjis.size());
                }
            }
        }

        write_script(output_path, *scr);
        compiled_count++;
        std::println("[RECOMPILE] {} -> {} ({} replacements, {} warnings)", input_path.filename().string(), output_path.filename().string(), replaced, warned);
    });

    if (skipped_count > 0) std::println("[INFO] Skipped {} unmodified file(s) to save time.", skipped_count.load());
    if (compiled_count == 0) {
        std::println("No scene files updated, skipping copying");
        return;
    }
    std::println("\n[DONE] Successfully recompiled {} file(s).", compiled_count.load());
}

void trace_scene_execution(const fs::path& input_path, int missing_node_id) {
    auto buffer = read_file_bytes(input_path);
    if (!buffer) return;

    auto scr = parse_script_buffer(*buffer);
    if (!scr || scr->nodes.empty()) return;

    std::unordered_map<uint32_t, Node*> nodes_by_id;
    for (auto& node : scr->nodes) nodes_by_id[node.id] = &node;

    std::vector<std::string> trace_log;
    std::unordered_map<uint32_t, bool> visited;
    Node* current_node = &scr->nodes[0];

    trace_log.push_back(std::format("--- TRACING EXECUTION FOR {} ---", input_path.filename().string()));

    while (current_node) {
        if (visited[current_node->id]) {
            trace_log.push_back(std::format("\n[LOOP DETECTED] Node {} was already visited. Halting trace.", current_node->id));
            break;
        }

        visited[current_node->id] = true;
        std::string preview = "";

        if (current_node->opcode == 5 && current_node->strs[2].len > 0) {
            std::string text = clean_text(sjis_to_utf8(current_node->strs[2].raw));
            preview = std::format(" | TEXT: {}...", text.substr(0, std::min(text.size(), static_cast<size_t>(40))));
        } else {
            for (const auto& str : current_node->strs) {
                if (str.len > 0) {
                    std::string data = clean_text(sjis_to_utf8(str.raw));
                    if (!data.empty()) {
                        preview = std::format(" | DATA: {}", data.substr(0, std::min(data.size(), static_cast<size_t>(40))));
                        break;
                    }
                }
            }
        }

        trace_log.push_back(std::format("ID: {:<5} -> NEXT: {:<5} | OPCODE: {:<3}{}", 
            current_node->id, current_node->next_id, current_node->opcode, preview));

        if (nodes_by_id.count(current_node->next_id)) {
            if (current_node->next_id == current_node->id) {
                trace_log.push_back("\n[END OF SCRIPT] Reached terminal node.");
                break;
            }
            current_node = nodes_by_id[current_node->next_id];
        } else {
            trace_log.push_back(std::format("\n[DEAD END] next_id {} does not exist in the file.", current_node->next_id));
            break;
        }
    }

    fs::path trace_output = input_path;
    trace_output.replace_extension(".trace.txt");
    std::ofstream out(trace_output);
    for (const auto& line : trace_log) out << line << "\n";

    std::println("[TRACE] Saved execution map to {}", trace_output.filename().string());
    std::println("        Total nodes in file: {}", scr->nodes.size());
    std::println("        Nodes executed in default path: {}", visited.size());

    if (missing_node_id != -1) {
        if (visited[missing_node_id]) {
            std::println("🔍 Result: Missing Node {} IS in the default execution path!", missing_node_id);
        } else {
            std::println("🔍 Result: Missing Node {} is NOT in the default execution path. It is being skipped.", missing_node_id);
        }
    }
}

// =============================================================================
// Main Entrypoint
// =============================================================================
int main() {
    load_or_create_config();

    fs::create_directories(Config.SCRIPT_DIR);
    fs::create_directories(Config.JSON_DIR);
    fs::create_directories(Config.OUTPUT_DIR);

    std::string command;
    std::print("extract (et), recompile (re), force-recompile (fre), or trace (tr): ");
    std::cin >> command;

    auto start = std::chrono::high_resolution_clock::now();

    if (command == "extract" || command == "et") {
        extract_all();
    } else if (command == "recompile" || command == "re") {
        recompile_all(false);
    } else if (command == "force-recompile" || command == "fre") {
        std::println("\n[WARNING] Forcing recompilation of ALL files regardless of modification dates...\n");
        recompile_all(true);
    } else if (command == "trace" || command == "tr") {
        std::string target_file;
        std::print("Enter the exact filename to trace (e.g. ep_006_012_000.txt): ");
        std::cin >> target_file;
        fs::path target_path = Config.SCRIPT_DIR / target_file;

        if (!fs::exists(target_path)) {
            std::println("[ERROR] File '{}' not found in '{}' folder.", target_file, Config.SCRIPT_DIR.string());
        } else {
            std::string node_input;
            std::print("Enter the missing node ID (or -1 to skip): ");
            std::cin >> node_input;
            int missing_id = -1;
            try { missing_id = std::stoi(node_input); } catch (...) {}
            trace_scene_execution(target_path, missing_id);
        }
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
