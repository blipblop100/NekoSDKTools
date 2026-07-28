# coding: utf-8
import io
import json
import struct
from pathlib import Path
import kaitaistruct
from kaitaistruct import KaitaiStruct, KaitaiStream

encd = 'sjis'

if getattr(kaitaistruct, 'API_VERSION', (0, 9)) < (0, 9):
    raise Exception(
        "Incompatible Kaitai Struct Python API: 0.9 or later is required, "
        f"but you have {kaitaistruct.__version__}"
    )

class NekosdkAdvscript2(KaitaiStruct):
    def __init__(self, _io: KaitaiStream, _parent: KaitaiStruct | None=None, _root: KaitaiStruct | None=None):
        self._io = _io
        self._parent = _parent
        self._root = _root if _root else self
        self._read()

    def _read(self):
        self.magic = self._io.read_bytes(19)
        if self.magic != b"\x4E\x45\x4B\x4F\x53\x44\x4B\x5F\x41\x44\x56\x53\x43\x52\x49\x50\x54\x32\x00":
            raise kaitaistruct.ValidationNotEqualError(
                b"\x4E\x45\x4B\x4F\x53\x44\x4B\x5F\x41\x44\x56\x53\x43\x52\x49\x50\x54\x32\x00",
                self.magic,
                self._io,
                u"/seq/0"
            )
        self.nodes_qty = self._io.read_u4le()
        self.nodes: list[NekosdkAdvscript2.Node] = []
        for _ in range(self.nodes_qty):
            self.nodes.append(NekosdkAdvscript2.Node(self._io, self, self._root))

    def write(self, of: io.BufferedWriter):
        of.write(self.magic)
        of.write(struct.pack('<I', len(self.nodes)))
        for node in self.nodes:
            node.write(of)

    class Nekostr(KaitaiStruct):
        def __init__(self, _io: KaitaiStream, _parent: KaitaiStruct | None=None, _root: KaitaiStruct | None=None):
            self._io = _io
            self._parent = _parent
            self._root = _root if _root else self
            self._read()

        def _read(self):
            self.len: int = self._io.read_u4le()
            raw: bytes = self._io.read_bytes(self.len)
            self.raw = raw
            self.value = raw.decode(encd, errors="replace")

        def write(self, of: io.BufferedWriter, encoding=encd, preserve_len=False):
            text = self.value or ""
            if not text.endswith("\x00"):
                text += "\x00"

            try:
                str_b = text.encode(encoding)
            except UnicodeEncodeError:
                str_b = text.encode(encoding, errors="replace")

            if preserve_len:
                target_len = self.len
                if len(str_b) > target_len:
                    print(f"[WARN] truncating string {len(str_b)} -> {target_len}")
                    str_b = str_b[:target_len]
                elif len(str_b) < target_len:
                    str_b += b"\x00" * (target_len - len(str_b))
            else:
                target_len = len(str_b)

            of.write(struct.pack('<I', target_len))
            of.write(str_b)

    class Node(KaitaiStruct):
        def __init__(self, _io: KaitaiStream, _parent: KaitaiStruct | None=None, _root: KaitaiStruct | None=None):
            self._io = _io
            self._parent = _parent
            self._root = _root if _root else self
            self._read()

        def _read(self):
            self.id: int = self._io.read_u4le()
            self.type1: int = self._io.read_u4le()
            self.some_ofs: int = self._io.read_u4le()
            self.opcode: int = self._io.read_u4le()
            self.spacer1: bytes = self._io.read_bytes(128)
            self.next_id: int = self._io.read_u4le()
            self.spacer2: bytes = self._io.read_bytes(64)
            self.strs: list[NekosdkAdvscript2.Nekostr] = []
            for _ in range(33):
                self.strs.append(NekosdkAdvscript2.Nekostr(self._io, self, self._root))

        def write(self, of: io.BufferedWriter):
            of.write(struct.pack('<I', self.id))
            of.write(struct.pack('<I', self.type1))
            of.write(struct.pack('<I', self.some_ofs))
            of.write(struct.pack('<I', self.opcode))
            of.write(self.spacer1)
            of.write(struct.pack('<I', self.next_id))
            of.write(self.spacer2)

            for i, s in enumerate(self.strs):
                s.write(of, encoding=encd, preserve_len=False)

def clean_text(s: str) -> str:
    if s is None:
        return ""
    return str(s).replace("\r\n", "\n").replace("\x00", "").strip()

def ensure_null_terminated(s: str) -> str:
    if s is None:
        return ""
    return s if s.endswith("\x00") else s + "\x00"

def load_json(path: Path):
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)

def save_json(path: Path, data):
    with path.open("w", encoding="utf-8") as f:
        json.dump(data, f, ensure_ascii=False, indent=2)

def extract_one(input_path: Path, output_path: Path):
    with input_path.open("rb") as f:
        scr = NekosdkAdvscript2.from_io(f)

    out = []
    for node in scr.nodes:
        if node.opcode == 5:
            character = ""
            original = ""

            if len(node.strs) > 1:
                character = clean_text(node.strs[1].value)
            if len(node.strs) > 2:
                original = clean_text(node.strs[2].value)

            out.append({
                "node_id": node.id,
                "character": character,
                "character_eng": "",
                "original": original,
                "translation": ""
            })

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
        except kaitaistruct.ValidationNotEqualError:
            print(f"[SKIP] {input_path.name} is not a valid NekoSDK binary file (invalid header).")
        except Exception as e:
            print(f"[ERROR] Failed to extract from {input_path.name}: {e}")

def wrap_text(text: str, max_len=90):
    if not text:
        return text
    text = text.replace("\n", " ")
    words = text.split(" ")
    lines = []
    current_line: str = ""

    for word in words:
        if len(current_line) + len(word) + 1 > max_len:
            lines.append(current_line.rstrip())
            current_line = word + " "
        else:
            current_line += word + " "

    if current_line:
        lines.append(current_line.rstrip())

    return "\r\n ".join(lines)


def recompile_one(input_path: Path, json_path: Path, output_path: Path, wrap_ch = 85):
    with input_path.open("rb") as f:
        scr = NekosdkAdvscript2.from_io(f)

    data = load_json(json_path)
    if not isinstance(data, list):
        raise ValueError(f"{json_path} must contain a JSON array")

    by_id = {}
    for entry in data:
        if not isinstance(entry, dict):
            continue
        node_id = entry.get("node_id")
        if node_id is not None:
            by_id[int(node_id)] = entry

    replaced = 0
    warned = 0

    for node in scr.nodes:
        if node.opcode != 5: 
            continue

        entry = by_id.get(node.id)
        if not entry:
            continue

        # --- Sanity check against original text ---
        json_original = clean_text(entry.get("original", ""))
        current_original = ""
        if len(node.strs) > 2:
            current_original = clean_text(node.strs[2].value)

        if json_original and current_original and json_original != current_original:
            warned += 1
            print(f"[WARN] Original mismatch at node {node.id}")

        # --- 1. Update Character Name (Slot 1) ---
        character_eng = clean_text(entry.get("character_eng", ""))
        character_jp = clean_text(entry.get("character", ""))
        character_to_use = character_eng if character_eng else character_jp

        if character_to_use and len(node.strs) > 1:
            node.strs[1].value = ensure_null_terminated(character_to_use)

        # --- 2. Update Translated Dialogue (Slot 2) ---
        translation = clean_text(entry.get("translation", ""))
        
        if translation and len(node.strs) > 2:
            translation = wrap_text(translation, wrap_ch)
            node.strs[2].value = ensure_null_terminated(translation)
            replaced += 1

    with output_path.open("wb") as f:
        scr.write(f)

    print(f"[RECOMPILE] {input_path.name} -> {output_path.name} ({replaced} replacements, {warned} warnings)")


def recompile_all(script_dir: Path, json_dir: Path, output_dir: Path, wrap_ch=85, force=False):
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
            recompile_one(input_path, json_path, output_path, wrap_ch=wrap_ch)
            compiled_count += 1
        except kaitaistruct.ValidationNotEqualError:
            print(f"[SKIP] {input_path.name} is not a valid NekoSDK binary file (invalid header).")
        except Exception as e:
            print(f"[ERROR] Failed to recompile {input_path.name}: {e}")
  
    if skipped_count > 0:
        print(f"[INFO] Skipped {skipped_count} unmodified file(s) to save time.")
    if compiled_count == 0:
        print("No scene files updated, skipping copying")
        return
    print(f"\n[DONE] Successfully recompiled {compiled_count} file(s).")



# CONFIG
# -------------------------
SCRIPT_DIR = "scr"
JSON_DIR = "translation_jsons"
OUTPUT_DIR = "output"
# -------------------------

if __name__ == "__main__":
    script_dir = Path(SCRIPT_DIR)
    script_dir.mkdir(exist_ok=True)
    json_dir = Path(JSON_DIR)
    json_dir.mkdir(exist_ok=True)
    output_dir = Path(OUTPUT_DIR)
    output_dir.mkdir(exist_ok=True)

    COMMAND = input("extract (et), recompile (re), or force-recompile (fre): ").strip().lower()

    if COMMAND in ["extract", "et"]:
        extract_all(script_dir, json_dir)

    elif COMMAND in ["recompile", "re"]:
        recompile_all(script_dir, json_dir, output_dir, wrap_ch=77, force=False)
            
    elif COMMAND in ["force-recompile", "fre"]:
        print("\n[WARNING] Forcing recompilation of ALL files regardless of modification dates...\n")
        recompile_all(script_dir, json_dir, output_dir, wrap_ch=77, force=True)
    else:
        print("Invalid COMMAND")