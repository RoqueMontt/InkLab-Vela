import os

# --- Configuration ---
ROOT_DIR = './Legacy'
UI_TARGET_DIR = './Legacy/inklab-dashboard/src' 
UI_OUTPUT_FILE = 'summary_UI.txt'
MCU_OUTPUT_FILE = 'summary_MCU.txt'

MCU_TARGETS = [
    "app_freertos.c",
    "fpga.c",
    "frontend_api.c",
    "main.c",
    "pinmux.c",
    "sys_power",
    "fpga.h",
    "ota.c",
    "user_diskio_spi.c",
    "user_diskio.c",
    "powerMonitor.c",
    "powerMonitor.h",
    "bq25798.c",
    "bq25798.h",
    "battery_soc.c",
    "diagnostics.c",
    "joystick.c",
    "led_manager.c"
]

def aggregate_all_files(target_dir, output_path):
    """Aggregates all files within a given directory."""
    if not os.path.exists(target_dir):
        print(f"[Warning] Directory not found: {target_dir}")
        return

    with open(output_path, 'w', encoding='utf-8') as outfile:
        for root, dirs, files in os.walk(target_dir):
            for file in files:
                file_path = os.path.join(root, file)
                
                # Create the header format
                relative_path = os.path.relpath(file_path, os.path.dirname(target_dir))
                header = relative_path.replace(os.sep, ' -> ')
                
                outfile.write(f"- {header}\n\n")
                
                try:
                    with open(file_path, 'r', encoding='utf-8') as infile:
                        outfile.write(infile.read())
                except Exception as e:
                    outfile.write(f"[Could not read file: {e}]")
                
                outfile.write("\n\n" + "="*50 + "\n\n")

def aggregate_targeted_files(target_dir, output_path, target_list):
    """Aggregates specific files found anywhere in the target directory."""
    found_count = 0
    with open(output_path, 'w', encoding='utf-8') as outfile:
        for root, dirs, files in os.walk(target_dir):
            for file in files:
                if file in target_list:
                    file_path = os.path.join(root, file)
                    
                    # Create the breadcrumb header
                    relative_path = os.path.relpath(file_path, target_dir)
                    header = relative_path.replace(os.sep, ' -> ')
                    
                    outfile.write(f"- {header}\n\n")
                    
                    try:
                        with open(file_path, 'r', encoding='utf-8') as infile:
                            outfile.write(infile.read())
                        found_count += 1
                    except Exception as e:
                        outfile.write(f"[Error reading file: {e}]")
                    
                    outfile.write("\n\n" + "="*50 + "\n\n")
    return found_count

if __name__ == "__main__":
    print("Generating summaries...")
    
    # 1. Generate UI Summary
    aggregate_all_files(UI_TARGET_DIR, UI_OUTPUT_FILE)
    print(f"✓ UI codebase compiled into {UI_OUTPUT_FILE}")
    
    # 2. Generate MCU Summary
    mcu_count = aggregate_targeted_files(ROOT_DIR, MCU_OUTPUT_FILE, MCU_TARGETS)
    print(f"✓ Found and compiled {mcu_count} out of {len(MCU_TARGETS)} MCU files into {MCU_OUTPUT_FILE}")
    
    print("Done!")