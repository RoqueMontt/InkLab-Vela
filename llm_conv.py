import os

# --- Configuration ---
# Script is inside 'inklab-firmware'
ZEPHYR_ROOT = '.' 
LEGACY_ROOT = './Legacy'
UI_TARGET_DIR = './Legacy/inklab-dashboard/src' 

# Output filenames
UI_OUTPUT_FILE = 'summary_UI.txt'
LEGACY_MCU_OUTPUT_FILE = 'summary_LEGACY_MCU.txt'
ZEPHYR_OUTPUT_FILE = 'summary_ZEPHYR.txt'

# Selective files from Legacy FreeRTOS
LEGACY_MCU_TARGETS = [
    "app_freertos.c", "fpga.c", "frontend_api.c", "main.c", 
    "pinmux.c", "sys_power.c", "fpga.h", "ota.c", 
    "user_diskio_spi.c", "user_diskio.c", "powerMonitor.c", 
    "powerMonitor.h", "bq25798.c", "bq25798.h", 
    "battery_soc.c", "diagnostics.c", "joystick.c", "led_manager.c"
]

# Selective files from your new Zephyr progress
ZEPHYR_TARGETS = [
    "prj.conf", 
    "CMakeLists.txt", 
    "west.yml", 
    "inklab_v1.overlay",
    "main.c"
]

def aggregate_all_files(target_dir, output_path):
    if not os.path.exists(target_dir): return
    with open(output_path, 'w', encoding='utf-8') as outfile:
        outfile.write(f"=== FULL UI CODEBASE SUMMARY ===\n\n")
        for root, dirs, files in os.walk(target_dir):
            for file in files:
                file_path = os.path.join(root, file)
                relative_path = os.path.relpath(file_path, target_dir)
                outfile.write(f"- {relative_path}\n\n")
                try:
                    with open(file_path, 'r', encoding='utf-8') as infile:
                        outfile.write(infile.read())
                except: outfile.write("[Unreadable file]")
                outfile.write("\n\n" + "="*50 + "\n\n")

def aggregate_targeted_files(target_dir, output_path, target_list, label, exclude_dirs=None):
    """Finds specific filenames but ignores excluded directories."""
    found_count = 0
    if exclude_dirs is None: exclude_dirs = []
    
    if not os.path.exists(target_dir): return 0

    with open(output_path, 'w', encoding='utf-8') as outfile:
        outfile.write(f"=== {label} SELECTIVE SUMMARY ===\n\n")
        for root, dirs, files in os.walk(target_dir):
            
            # --- THE FIX: Prune the search tree ---
            # This prevents the script from entering 'Legacy' or 'build' folders
            dirs[:] = [d for d in dirs if d not in exclude_dirs]
                
            for file in files:
                if file in target_list:
                    file_path = os.path.join(root, file)
                    display_path = os.path.relpath(file_path, target_dir)
                    
                    outfile.write(f"--- FILE: {display_path} ---\n\n")
                    try:
                        with open(file_path, 'r', encoding='utf-8') as infile:
                            outfile.write(infile.read())
                        found_count += 1
                    except Exception as e:
                        outfile.write(f"[Error: {e}]")
                    outfile.write("\n\n" + "="*50 + "\n\n")
    return found_count

if __name__ == "__main__":
    print("🛠️  Filtering and aggregating AI context...")
    
    # 1. UI Summary (Grabs everything in Dashboard)
    aggregate_all_files(UI_TARGET_DIR, UI_OUTPUT_FILE)
    
    # 2. Legacy Summary (Searches specifically inside ./Legacy)
    leg_count = aggregate_targeted_files(LEGACY_ROOT, LEGACY_MCU_OUTPUT_FILE, LEGACY_MCU_TARGETS, "LEGACY")
    print(f"✅ Legacy: {leg_count} files.")
    
    # 3. Zephyr Summary (Searches '.', but IGNORES 'Legacy' and 'build')
    zep_count = aggregate_targeted_files(ZEPHYR_ROOT, ZEPHYR_OUTPUT_FILE, ZEPHYR_TARGETS, "ZEPHYR", exclude_dirs=['Legacy', 'build'])
    print(f"✅ Zephyr: {zep_count} files (Legacy folder ignored).")
    
    print("\n🚀 Summary files cleaned. You are ready to upload.")