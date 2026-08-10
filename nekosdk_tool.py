# coding: utf-8
from __future__ import annotations

import re
import json
import struct
from dataclasses import dataclass
from pathlib import Path


#  MAIN CONFIG
SCRIPT_DIR = "scr"
JSON_DIR = "translation_jsons"
OUTPUT_DIR = "output"

# TEXT WRAPPER CONFIG
TEXT_WRAP_CH = 77 # Maximum characters per line
# Optional
CLEAN_EXISTING_NEWLINES = True  # Normalize existing line breaks before wrapping
FILL_WINDOW = 0.65  # Prefer sentence breaks within the final 35% of each line

# OPTIONAL CONFIG
RECOMPILE_MAIN = True  # Set to True to recompile hidden developer commands (node.strs[0])


# ENGINE CONSTANTS & SETUP
MAGIC = b"NEKOSDK_ADVSCRIPT2\x00"
ENCODING = "sjis"
HEADER_FORMAT = "<4I 128s I 64s"  # 16 + 128 + 4 + 64 = 212 bytes
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
STRS_PER_NODE = 33

# FAST BINARY DATA MODELS
@dataclass(slots=True)
class Node:
    id: int
    type1: int
    some_ofs: int
    opcode: int
    spacer1: bytes  # 128 bytes
    next_id: int
    spacer2: bytes  # 64 bytes
    strs: list[bytes]  # Stored as raw bytes for lazy decoding

    def get_str(self, index: int) -> str:
        """Lazy-decodes a specific string slot on demand."""
        if index < len(self.strs) and self.strs[index]:
            return self.strs[index].decode(ENCODING, errors="replace")
        return ""

    def set_str(self, index: int, text: str):
        """Encodes and null-terminates a string slot."""
        if not text.endswith("\x00"):
            text += "\x00"
        self.strs[index] = text.encode(ENCODING, errors="replace")


# BINARY PARSER & SERIALIZER
def parse_script(data: bytes) -> list[Node]:
    """Unpacks a raw NekoSDK ADV_SCRIPT2 binary buffer into a list of Node objects."""
    if not data.startswith(MAGIC):
        raise ValueError("Invalid NEKOSDK_ADVSCRIPT2 header.")

    # Read node count at byte offset 19
    (nodes_qty,) = struct.unpack_from("<I", data, 19)
    pos = 23
    nodes: list[Node] = []

    for _ in range(nodes_qty):
        # 1. Unpack the 212-byte fixed header in one C-speed instruction
        n_id, type1, some_ofs, opcode, sp1, next_id, sp2 = struct.unpack_from(
            HEADER_FORMAT, data, pos
        )
        pos += HEADER_SIZE

        # 2. Extract the 33 string slots
        strs: list[bytes] = []
        for _ in range(STRS_PER_NODE):
            (s_len,) = struct.unpack_from("<I", data, pos)
            pos += 4
            strs.append(data[pos : pos + s_len])
            pos += s_len

        nodes.append(
            Node(n_id, type1, some_ofs, opcode, sp1, next_id, sp2, strs)
        )

    return nodes


def serialize_script(nodes: list[Node]) -> bytes:
    """Serializes a list of Node objects back into a raw binary buffer."""
    buf = bytearray(MAGIC)
    buf.extend(struct.pack("<I", len(nodes)))

    for n in nodes:
        # Pack 212-byte header
        buf.extend(
            struct.pack(
                "<4I 128s I 64s",
                n.id,
                n.type1,
                n.some_ofs,
                n.opcode,
                n.spacer1,
                n.next_id,
                n.spacer2,
            )
        )

        # Pack 33 strings
        for s_bytes in n.strs:
            buf.extend(struct.pack("<I", len(s_bytes)))
            buf.extend(s_bytes)

    return bytes(buf)


# TEXT & JSON HELPERS
def clean_text(s: str) -> str:
    if not s:
        return ""
    return str(s).replace("\r\n", "\n").replace("\x00", "").strip()



# Compiled regex to detect sentence-ending punctuation (. ! ? plus optional closing quotes/brackets)
# Negative lookbehind (?<!\.) ensures ellipses (..., .., etc.) are ignored and treated as normal words
# (?:\s+|$) ensures it still matches if the trailing space falls exactly at the max_len boundary
_SENTENCE_END_RE = re.compile(r'(?<!\.)([.!?]["”\')\]]*)(?:\s+|$)')
def wrap_text(text: str, max_len: int | None = None, min_fill: float = FILL_WINDOW) -> str:
    """
    Sentence-aware line wrapper.
    Snaps line breaks to sentence boundaries if they fall within the end window (default: last 35% of max_len).
    """
    if not text or max_len is None:
        return text

    if CLEAN_EXISTING_NEWLINES:
        words = text.split()
        if not words:
            return ""
        text = " ".join(words)

    if len(text) <= max_len:
        return text

    rn = "\n " if text[0] in ["「", "（", "《"] else "\n"

    lines: list[str] = []
    min_len = int(max_len * min_fill)

    while len(text) > max_len:
        search_area = text[:max_len]

        # Search for sentence breaks inside our preferred lookback window [min_len : max_len]
        best_break = -1
        for match in _SENTENCE_END_RE.finditer(search_area):
            break_idx = match.start() + len(match.group(1))
            if break_idx >= min_len:
                best_break = break_idx

        # If a clean sentence break exists in the lookback window, snap to it
        if best_break != -1:
            lines.append(text[:best_break].rstrip())
            text = text[best_break:].lstrip()
            continue

        # Fallback: Greedy word wrap at the last space before max_len
        last_space = search_area.rfind(' ')
        if last_space != -1:
            lines.append(text[:last_space].rstrip())
            text = text[last_space:].lstrip()
        else:
            # Emergency fallback: Single giant word longer than max_len
            lines.append(text[:max_len])
            text = text[max_len:].lstrip()
            
    # Safely append whatever text is leftover
    if text:
        lines.append(text.rstrip())

    return rn.join(lines)

def make_text_main(name: str, message: str, old_main: str) -> str:
    """Reconstructs the hidden developer Main Command Block (Slot 0)."""
    voice = ""
    # Fast scan for voice/audio tags in the original Japanese command block
    for line in old_main.split("\n"):
        if line.startswith("[テキスト表示]"):
            for part in line.split()[1:]:
                if "\\" in part or "/" in part:
                    voice = part
                    break
            break

    speaker = name or ""
    first = f"[テキスト表示] {speaker} {voice}".rstrip()
    display_message = message.replace("\r\n", "\n")
    return f"{first}\n{display_message}\n\n"


def load_json(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def save_json(path: Path, data):
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)


# CORE ENGINE WORKFLOWS
def extract_one(input_path: Path, output_path: Path):
    nodes = parse_script(input_path.read_bytes())
    out = []

    for node in nodes:
        if node.opcode == 5:
            character = clean_text(node.get_str(1)) if len(node.strs) > 1 else ""
            original = clean_text(node.get_str(2)) if len(node.strs) > 2 else ""

            out.append(
                {
                    "node_id": node.id,
                    #"next_id": node.next_id,
                    "character": character,
                    "character_eng": "",
                    "original": original,
                    "translation": "",
                }
            )

    if out:
        save_json(output_path, out)
        print(f"[EXTRACT] {input_path.name} -> {output_path.name} ({len(out)} entries)")
    else:
        print(f"[SKIP] {input_path.name} contained no dialogue nodes.")


def extract_all(script_dir: Path, json_dir: Path):
    json_dir.mkdir(parents=True, exist_ok=True)
    for input_path in sorted(script_dir.glob("*.txt")):
        if not input_path.is_file():
            continue

        output_path = json_dir / (input_path.stem + ".json")
        try:
            extract_one(input_path, output_path)
        except ValueError:
            print(f"[SKIP] {input_path.name} is not a valid NekoSDK binary file (invalid header).")
        except Exception as e:
            print(f"[ERROR] Failed to extract from {input_path.name}: {e}")


def recompile_one(input_path: Path, json_path: Path, output_path: Path, wrap_ch: int | None):
    nodes = parse_script(input_path.read_bytes())
    data = load_json(json_path)

    if not isinstance(data, list):
        raise ValueError(f"{json_path} must contain a JSON array")

    by_id = {}
    for entry in data:
        if isinstance(entry, dict) and entry.get("node_id") is not None:
            by_id[int(entry["node_id"])] = entry

    replaced = 0
    warned = 0

    for node in nodes:
        if node.opcode != 5:
            continue

        entry = by_id.get(node.id)
        if not entry:
            continue

        # Sanity check against original Japanese text
        json_original = clean_text(entry.get("original", ""))
        current_original = clean_text(node.get_str(2)) if len(node.strs) > 2 else ""

        if json_original and current_original and json_original != current_original:
            warned += 1
            print(f"[WARN] Original mismatch at node {node.id}")

        # 1. Update Character Name (Slot 1)
        character_eng = clean_text(entry.get("character_eng", ""))
        character_jp = clean_text(entry.get("character", ""))
        character_to_use = character_eng if character_eng else character_jp

        if character_to_use and len(node.strs) > 1:
            node.set_str(1, character_to_use)

        # 2. Update Translated Dialogue (Slot 2)
        translation = clean_text(entry.get("translation", ""))
        if translation and len(node.strs) > 2:
            translation_wrapped = wrap_text(translation, max_len=wrap_ch)
            node.set_str(2, translation_wrapped)

            # 3. Update Main Command Block (Slot 0) if enabled in config
            if RECOMPILE_MAIN and len(node.strs) > 0:
                old_main = clean_text(node.get_str(0))
                new_main = make_text_main(character_to_use, translation_wrapped, old_main)
                node.set_str(0, new_main)

            replaced += 1

    # Write binary file in one single atomic operation
    output_path.write_bytes(serialize_script(nodes))
    print(f"[RECOMPILE] {input_path.name} -> {output_path.name} ({replaced} replacements, {warned} warnings)")


def recompile_all(script_dir: Path, json_dir: Path, output_dir: Path, wrap_ch: int | None = None, force: bool = False):
    output_dir.mkdir(parents=True, exist_ok=True)

    skipped_count = 0
    compiled_count = 0

    for input_path in sorted(script_dir.glob("*.txt")):
        if not input_path.is_file():
            continue

        json_path = json_dir / (input_path.stem + ".json")
        if not json_path.exists():
            print(f"[SKIP] Missing JSON for {input_path.name}")
            continue

        output_path = output_dir / input_path.name

        if not force and output_path.exists():
            json_mtime = json_path.stat().st_mtime
            input_mtime = input_path.stat().st_mtime
            output_mtime = output_path.stat().st_mtime

            if json_mtime <= output_mtime and input_mtime <= output_mtime:
                skipped_count += 1
                continue

        try:
            recompile_one(input_path, json_path, output_path, wrap_ch)
            compiled_count += 1
        except ValueError:
            print(f"[SKIP] {input_path.name} is not a valid NekoSDK binary file (invalid header).")
        except Exception as e:
            print(f"[ERROR] Failed to recompile {input_path.name}: {e}")

    if skipped_count > 0:
        print(f"[INFO] Skipped {skipped_count} unmodified file(s) to save time.")
    if compiled_count == 0:
        print("No scene files updated, skipping copying")
        return
    print(f"\n[DONE] Successfully recompiled {compiled_count} file(s).")


# SCENE EXECUTION TRACING
def trace_scene_execution(input_path: Path, missing_node_id: int | None = None):
    """Maps the execution flow of a scene by following next_id pointers."""
    nodes = parse_script(input_path.read_bytes())
    nodes_by_id = {node.id: node for node in nodes}

    trace_log = []
    visited = set()
    current_node = nodes[0]

    trace_log.append(f"--- TRACING EXECUTION FOR {input_path.name} ---")

    while current_node is not None:
        if current_node.id in visited:
            trace_log.append(f"\n[LOOP DETECTED] Node {current_node.id} was already visited. Halting trace.")
            break

        visited.add(current_node.id)

        preview = ""
        if current_node.opcode == 5 and len(current_node.strs) > 2:
            preview = f" | TEXT: {clean_text(current_node.get_str(2))[:40]}..."
        elif len(current_node.strs) > 0:
            for i in range(len(current_node.strs)):
                cleaned = clean_text(current_node.get_str(i))
                if cleaned:
                    preview = f" | DATA: {cleaned[:40]}"
                    break

        trace_log.append(
            f"ID: {current_node.id:<5} -> NEXT: {current_node.next_id:<5} | OPCODE: {current_node.opcode:<3}{preview}"
        )

        if current_node.next_id in nodes_by_id:
            if current_node.next_id == current_node.id:
                trace_log.append("\n[END OF SCRIPT] Reached terminal node.")
                break
            current_node = nodes_by_id[current_node.next_id]
        else:
            trace_log.append(f"\n[DEAD END] next_id {current_node.next_id} does not exist in the file.")
            break

    trace_output = input_path.with_suffix(".trace.txt")
    trace_output.write_text("\n".join(trace_log), encoding="utf-8")

    print(f"[TRACE] Saved execution map to {trace_output.name}")
    print(f"        Total nodes in file: {len(nodes)}")
    print(f"        Nodes executed in default path: {len(visited)}")

    if missing_node_id is not None:
        if missing_node_id in visited:
            print(f"🔍 Result: Missing Node {missing_node_id} IS in the default execution path!")
        else:
            print(f"🔍 Result: Missing Node {missing_node_id} is NOT in the default execution path. It is being skipped.")


if __name__ == "__main__":
    script_dir = Path(SCRIPT_DIR)
    script_dir.mkdir(exist_ok=True)
    json_dir = Path(JSON_DIR)
    json_dir.mkdir(exist_ok=True)
    output_dir = Path(OUTPUT_DIR)
    output_dir.mkdir(exist_ok=True)

    COMMAND = input("extract (et), recompile (re), force-recompile (fre), or trace (tr): ").strip().lower()

    if COMMAND in ["extract", "et"]:
        extract_all(script_dir, json_dir)

    elif COMMAND in ["recompile", "re"]:
        recompile_all(script_dir, json_dir, output_dir, wrap_ch=TEXT_WRAP_CH, force=False)

    elif COMMAND in ["force-recompile", "fre"]:
        print("\n[WARNING] Forcing recompilation of ALL files regardless of modification dates...\n")
        recompile_all(script_dir, json_dir, output_dir, wrap_ch=TEXT_WRAP_CH, force=True)

    elif COMMAND in ["trace", "tr"]:
        target_file = input("Enter the exact filename to trace (e.g. ep_006_012_000.txt): ").strip()
        target_path = script_dir / target_file

        if not target_path.exists():
            print(f"[ERROR] File '{target_file}' not found in '{SCRIPT_DIR}' folder.")
        else:
            node_input = input("Enter the missing node ID (or press Enter to skip): ").strip()
            missing_id = int(node_input) if node_input.isdigit() else None
            trace_scene_execution(target_path, missing_id)
    else:
        print("Invalid COMMAND")
