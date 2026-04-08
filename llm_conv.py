import os

# --- Configuration ---
# Script is inside 'inklab-firmware'
UI_TARGET_PATHS = ['./Legacy/inklab-dashboard/src']
LEGACY_TARGET_PATHS = ['./Legacy/core']

# Zephyr now includes specific configuration files and the board overlay
ZEPHYR_TARGET_PATHS = [
    './src', 
    './include', 
    './CMakeLists.txt', 
    './prj.conf', 
    './boards/inklab_v1.overlay'
]

# Output filenames
UI_OUTPUT_FILE = 'summary_UI.txt'
LEGACY_MCU_OUTPUT_FILE = 'summary_LEGACY_MCU.txt'
ZEPHYR_OUTPUT_FILE = 'summary_ZEPHYR.txt'

def _write_file_to_summary(file_path, outfile):
    """Helper function to write a single file's contents to the summary."""
    display_path = os.path.relpath(file_path, start='.')
    outfile.write(f"--- FILE: {display_path} ---\n\n")
    success = False
    try:
        with open(file_path, 'r', encoding='utf-8') as infile:
            outfile.write(infile.read())
        success = True
    except Exception as e:
        outfile.write(f"[Error: {e}]")
    outfile.write("\n\n" + "="*50 + "\n\n")
    return success

def aggregate_paths(target_paths, output_path, label, exclude_dirs=None):
    """Aggregates files from directories or individual file paths."""
    if exclude_dirs is None: exclude_dirs = []
    found_count = 0

    with open(output_path, 'w', encoding='utf-8') as outfile:
        outfile.write(f"=== FULL {label} CODEBASE SUMMARY ===\n\n")
        
        for target_path in target_paths:
            if not os.path.exists(target_path):
                print(f"⚠️  Warning: {target_path} does not exist. Skipping.")
                continue

            # If it's a single file, just append it directly
            if os.path.isfile(target_path):
                if _write_file_to_summary(target_path, outfile):
                    found_count += 1
            
            # If it's a directory, walk through it
            elif os.path.isdir(target_path):
                for root, dirs, files in os.walk(target_path):
                    # Prune the search tree to prevent entering excluded folders
                    dirs[:] = [d for d in dirs if d not in exclude_dirs]
                        
                    for file in files:
                        file_path = os.path.join(root, file)
                        if _write_file_to_summary(file_path, outfile):
                            found_count += 1
                    
    return found_count

if __name__ == "__main__":
    print("🛠️  Filtering and aggregating AI context...")
    
    # 1. UI Summary (Grabs everything in Dashboard)
    ui_count = aggregate_paths(UI_TARGET_PATHS, UI_OUTPUT_FILE, "UI")
    print(f"✅ UI: {ui_count} files.")
    
    # 2. Legacy Summary (Grabs everything specifically inside ./Legacy/core)
    leg_count = aggregate_paths(LEGACY_TARGET_PATHS, LEGACY_MCU_OUTPUT_FILE, "LEGACY")
    print(f"✅ Legacy: {leg_count} files.")
    
    # 3. Zephyr Summary (Grabs src, include, and specific config files, ignoring build)
    zep_count = aggregate_paths(ZEPHYR_TARGET_PATHS, ZEPHYR_OUTPUT_FILE, "ZEPHYR", exclude_dirs=['build'])
    print(f"✅ Zephyr: {zep_count} files.")
    
    print("\n🚀 Summary files cleaned. You are ready to upload.")