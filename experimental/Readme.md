# NekoSDK Translation Extraction & Recompilation Tool

This is an (LLM converted) experimental C++ version of the NekoSDK tool. It's extraction and recompilation times are significantly faster compared to the Python implementation. This implementation uses 3rd party JSON libraries for maximum JSON parsing performance via [simdjson](https://github.com/simdjson/simdjson) and [nlohmann json](https://github.com/nlohmann/json).

*Note: In my limited testing, it has performed perfectly without errors, but I do not guarantee it will work for everyone. This version is designed specifically for Windows and runs best on it.*

### How to Use

1. Double-click `NekoSDKTool.exe` to open the command prompt.
2. On your first run, three folders will be automatically created:
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

---

### Building from Source

If the `.exe` didn't run on your PC, or if you just want to compile it yourself, follow these steps:

**Prerequisites:**
Make sure you have all the necessary source files in one folder: `NekoSDKTool.cpp`, `simdjson.cpp`, `simdjson.h`, and `json.hpp`.

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
```
g++.exe -std=c++23 -O3 -march=native -flto=auto -s *.cpp -o NekoSDKTool.exe -lstdc++exp -ltbb12

```


3. Wait a few moments for the compiler to finish, and `NekoSDKTool.exe` will appear in your folder.

*(Note: Because the tool relies on TBB for multithreading, if Windows complains about a missing DLL when you run the `.exe`, simply copy `libtbb12.dll` from `C:\msys64\ucrt64\bin` and paste it right next to your new `.exe`).*

