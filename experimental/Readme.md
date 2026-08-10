# NekoSDK Translation Extraction & Recompilation Tool (C++)

This is an (LLM converted) experimental C++ version of the NekoSDK tool. It's extraction and recompilation times are significantly faster compared to the Python implementation. This implementation uses the [simdjson library](https://github.com/simdjson/simdjson) for maximum JSON parsing performance

*Note: In my limited testing, it has performed perfectly without errors, but I do not guarantee it will work for everyone. This version is designed specifically for Windows and runs best on it.*


### How to Use

1. Double-click `NekoSDKTool.exe` to open the command prompt.
2. On your first run, a `config.ini` file will be generated alongside three default folders:
* **`scr`**: Place all of your source scripts (`.txt` files) in here.
* **`translation_jsons`**: The tool will output the extracted `.json` text data here.
* **`output`**: Your newly translated and recompiled `.txt` scripts will be saved here.



### Commands

* `extract` (or `et`):
Extracts the character names and dialogue lines from the source `.txt` files into individual `.json` files.
Each extracted JSON is formatted like this:
```json
[
    {
        "character": "<Original JP Character Name>",
        "character_eng": "<EN Character Name>",
        "node_id": <Unique node ID for this line>,
        "original": "<Original JP text>",
        "translation": "<Your translation goes here>"
    }
]

```


* `recompile` (or `re`):
Packages the translated JSONs back into the `.txt` format the game expects. On the first run, it will compile everything. On subsequent runs, it only recompiles JSONs that have been modified to save time.
* `force-recompile` (or `fre`):
Forces the recompilation of all JSON files regardless of their last modification date.
* `trace` (or `tr`):
Maps out the internal execution flow of a specific scene to help debug branching choices or missing dialogue nodes.

---



### Configuration (`config.ini`)

When you run the tool for the first time, it generates a `config.ini` file. You can open this in any text editor to customize how the tool behaves:

* **`SCRIPT_DIR` / `JSON_DIR` / `OUTPUT_DIR**`: Change the names of the folders the tool looks for.
* **`TEXT_WRAP_CH`**: The maximum number of characters allowed per line before the tool automatically wraps the English text (Default: `77`).
* **`CLEAN_EXISTING_NEWLINES`**: If `true`, the tool deletes the existing line breaks before applying the English word-wrapper (Default: `false`).
* **`FILL_WINDOW`**: Controls how aggressively the tool looks for clean sentence breaks (periods, question marks) to wrap text nicely instead of splitting in the middle of a sentence. `0.65` means it looks in the last 35% of the line (Default: `0.65`).
* **`RECOMPILE_MAIN`**: If `true`, the tool also safely rebuilds the hidden developer command blocks (Slot 0) with your translated texts (Default: `false`).

---



### Building from Source

If you want to compile it yourself from the source code, follow these steps:

**Prerequisites:**
Make sure you have all the necessary source files in one folder: `NekoSDKTool.cpp`, `simdjson.cpp`, and `simdjson.h`.

**Setup MSYS2 & GCC:**

1. Install [MSYS2](https://www.msys2.org/).
2. Open the **MSYS2 UCRT64** terminal and install the GCC compiler:
`pacman -S mingw-w64-ucrt-x86_64-gcc`
3. Install the Intel TBB library (required for multithreading):
`pacman -S mingw-w64-ucrt-x86_64-tbb`
4. Add `C:\msys64\ucrt64\bin` to your Windows System Environment `PATH` variables.

**Compile:**

1. Open a normal Windows command prompt inside your source code folder (easiest way: type `cmd` in the folder's address bar and hit Enter).
2. Run this release build command:
```bash
g++.exe -std=c++23 -O3 -march=native -flto -s *.cpp -o NekoSDKTool.exe -static -static-libgcc -static-libstdc++ -lstdc++exp -Wl,-Bdynamic -ltbb12

```

3. Wait a few moments for the compiler to finish, and `NekoSDKTool.exe` will appear in your folder.



### ⚠️ Important Note on Portability (DLLs)

If you want to run this tool on a PC where MSYS2 and GCC are not installed, you **must** copy the following four files into the exact same folder as your `.exe`:

* `libstdc++-6.dll`
* `libtbb12.dll`
* `libwinpthread-1.dll`
* `libgcc_s_seh-1.dll`

*(You can find all of these inside your compiler's `bin` directory, e.g., `C:\msys64\ucrt64\bin`).*

