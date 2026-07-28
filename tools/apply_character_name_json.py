import os
import json

CHARACTER_NAMES_PATH = "character_names.json"
SCENES_DIR = "translation_jsons"

def load_name_map():
    if not os.path.exists(CHARACTER_NAMES_PATH):
        print(f"❌ Error: {CHARACTER_NAMES_PATH} not found!")
        return {}
        
    with open(CHARACTER_NAMES_PATH, "r", encoding="utf-8") as f:
        data = json.load(f)
        
    # Convert list format to a direct dictionary lookup map: {"アスクル": "Askul"}
    return {entry["original"]: entry["translation"] for entry in data if "original" in entry}

def prefill_scenes():
    name_map = load_name_map()
    if not name_map:
        return
        
    files = sorted([f for f in os.listdir(SCENES_DIR) if f.endswith(".json")])
    print(f"Loaded {len(name_map)} character names. Scanning {len(files)} files to prefill...")

    updated_files_count = 0
    
    for filename in files:
        scene_path = os.path.join(SCENES_DIR, filename)
        with open(scene_path, "r", encoding="utf-8") as f:
            scene_data = json.load(f)
            
        modified = False
        for node in scene_data:
            jp_name = node.get("character", "").strip()
            
            if jp_name in name_map and node["character_eng"] != name_map[jp_name]:
                node["character_eng"] = name_map[jp_name]
                modified = True
                    
        if modified:
            with open(scene_path, "w", encoding="utf-8") as f:
                json.dump(scene_data, f, ensure_ascii=False, indent=2)
            updated_files_count += 1

    print(f"🎉 Name pre-filling complete! Updated {updated_files_count} scene files.")

if __name__ == "__main__":
    prefill_scenes()