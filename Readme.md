# NekoSDK Translation Extraction & Recompilation Tool (Python)

This is a Python-based utility to parse, extract, and recompile NekoSDK `ADV_SCRIPT2` binary scene files.
It is based on the [nekosdk_tools](https://github.com/adsf0427/nekosdk_tools) by skyler sora (adsf0427). 
It uses the [Kaitai Struct](https://kaitai.io/) framework to extract the character and text data from the game scripts (.txt) into editable JSON formats.

### Prerequisites & Setup

You will need Python installed on your system to run this tool.
Before running the script for the first time, you must install the required Kaitai Struct library. Open your command prompt or terminal and run:

```
pip install kaitaistruct

```

*(Note: Kaitai Struct version 0.9 or later is required).*

### How to Use

1. Run the script by double-clicking it or running `python nekosdk_tool.py` in your terminal.
2. On your first run, three folders will be automatically created in the same directory:
* **`scr`**: Place all of your original source scripts (`.txt` files) in here.
* **`translation_jsons`**: The tool will output the extracted `.json` text data here.
* **`output`**: Your newly translated and recompiled `.txt` scripts will be generated here.



### Commands

When prompted, type one of the following commands and press Enter:

* `extract` (or `et`):
Extracts the character names and dialogue lines from the source `.txt` files into individual `.json` files.
Each extracted JSON is formatted like this:
```json
[
    {
        "node_id": <Unique ID for this line node>,
        "character": "<Original JP Character Name>",
        "character_eng": "<EN Character Name>",
        "original": "<Original JP text>",
        "translation": "<Your translation goes here>"
    }
]

```


* `recompile` (or `re`):
Packages the translated JSONs back into the `.txt` binary format the game expects. On the first run, it will compile everything. On subsequent runs, it only recompiles JSONs that have been modified since the last run to save time.
* `force-recompile` (or `fre`):
Forces the recompilation of all JSON files regardless of their last modification date.