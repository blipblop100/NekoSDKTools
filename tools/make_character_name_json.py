import json
from pathlib import Path

# ==================== CONFIGURATION ====================
# Folder containing your JSON files (array format)
INPUT_FOLDER = "translation_jsons"

# Output JSON file for character names
OUTPUT_FILE = "character_names.json"

# Set to True to search subfolders recursively
RECURSIVE = True

# Set to True to show progress while scanning
VERBOSE = True
# =======================================================

def find_json_files(directory, recursive):
    """Find all JSON files in directory"""
    path = Path(directory)
    if not path.exists():
        return []
    if recursive:
        return list(path.rglob("*.json"))
    else:
        return list(path.glob("*.json"))

def extract_characters(json_files, verbose):
    """Extract unique character names from JSON files"""
    characters = set()
    
    for file_path in json_files:
        if verbose:
            print(f"Scanning: {file_path.name}")
        try:
            with open(file_path, 'r', encoding='utf-8') as f:
                data = json.load(f)
            
            if not isinstance(data, list):
                if verbose:
                    print(f"  ⚠ Skipping: not a JSON array")
                continue
            
            for item in data:
                character = item.get('character', '').strip()
                if character:
                    characters.add(character)
                    
        except Exception as e:
            print(f"  ✗ Error reading {file_path.name}: {e}")
    
    return sorted(characters)

def main():
    print("=" * 60)
    print("Character Name Extractor from JSON Files")
    print("=" * 60)
    
    script_dir = Path(__file__).parent
    input_path = script_dir / INPUT_FOLDER
    
    if not input_path.exists():
        print(f"Error: Folder '{INPUT_FOLDER}' not found!")
        return
    
    # Find all JSON files
    json_files = find_json_files(input_path, RECURSIVE)
    if not json_files:
        print(f"No JSON files found in '{INPUT_FOLDER}'")
        return
    
    print(f"Found {len(json_files)} JSON file(s)\n")
    
    # Extract unique character names
    characters = extract_characters(json_files, VERBOSE)
    
    if not characters:
        print("No character names found.")
        return
    
    # Create output array
    output_data = [{"original": name, "translation": ""} for name in characters]
    
    # Save to JSON
    output_path = script_dir / OUTPUT_FILE
    with open(output_path, 'w', encoding='utf-8') as f:
        json.dump(output_data, f, ensure_ascii=False, indent=2)
    
    print(f"\n✓ Found {len(characters)} unique character(s)")
    print(f"✓ Saved to: {OUTPUT_FILE}")
    
    # Show first 10 characters as preview
    if VERBOSE:
        print("\nFirst 10 characters:")
        for name in characters[:10]:
            print(f"  - {name}")
        if len(characters) > 10:
            print(f"  ... and {len(characters) - 10} more")

if __name__ == "__main__":
    main()