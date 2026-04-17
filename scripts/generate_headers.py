import os
import subprocess

# Define the folder containing your source web files
SOURCE_DIR = "web"
# Define where the .h files should go
INCLUDE_DIR = "include/webgui"

def build_web_headers(target=None, source=None, env=None):

    for filename in os.listdir(SOURCE_DIR):
        # Process specific web file types
        if filename.endswith((".htm", ".css", ".js", ".json")):
            input_path = os.path.join(SOURCE_DIR, filename)

            # Generate the output path: e.g., "include/index.h"
            base_name = os.path.splitext(filename)[0]
            output_path = os.path.join(INCLUDE_DIR, f"{base_name}.h")

            print(f"--- Generating {base_name}.h ---")
            subprocess.run(["python", "scripts/header_tool.py", input_path, output_path])

# PlatformIO Hook
try:
    # Import the PlatformIO environment
    Import("env")

    # We call it once, directly.
    # Because 'extra_scripts = pre:generate_headers.py' is in platformio.ini,
    # this line executes as soon as PlatformIO starts the build environment,
    # but BEFORE it starts compiling any .cpp files.
    build_web_headers()

except NameError:
    # This allows you to still run the script manually via terminal
    build_web_headers()