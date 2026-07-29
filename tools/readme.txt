Translating character names manually across hundreds of JSON files with 10s of thousands of lines can be incredibly tedious.
These two helper Python scripts automate the process, allowing you to translate every character's name only once, and instantly apply it to your entire project.
-First make sure these two files are in the same root folder where the translation_jsons folder is
-Then simply double click or use cmd to first run the make_character_name_json.py, it will create a character_names.json
-Add the translations for the character names in character_names.json
-Then run the apply_character_name_json.py and it will pre fill all of the scene jsons inside the translation_jsons folder with the correct character names
