import os
import gzip
import argparse
import re

def file_to_gzipped_c_header(input_path, output_path=None, add_version=False):
    if not os.path.exists(input_path):
        print(f"Error: File '{input_path}' not found.")
        return

    # Determine output filename if not provided
    if output_path is None:
        base_name = os.path.splitext(input_path)[0]
        output_path = f"{base_name}.h"

    # Read the raw file
    with open(input_path, 'rb') as f:
        raw_data = f.read()

    # Compress the data using gzip
    compressed_data = gzip.compress(raw_data)

    # Generate a valid C variable name from the filename
    filename = os.path.basename(input_path)
    var_name = re.sub(r'\W+', '', filename)

    # Format as hex bytes (0xXX)
    hex_bytes = [f"0x{b:02X}" for b in compressed_data]

    # Chunk into lines of maximum 21 bytes
    lines = []
    for i in range(0, len(hex_bytes), 21):
        lines.append(", ".join(hex_bytes[i:i+21]))

    # Join the lines with a comma and newline
    formatted_array = ",\n".join(lines)

    # Create the C header content
    header_content = (
        f"#include <Arduino.h>\n"
        f"static const uint8_t {var_name}[] = {{\n"
        f"{formatted_array}\n"
        f"}};\n"
    )

    # Write to the output file
    with open(output_path, 'w') as f:
        f.write(header_content)

    print(f"Converted '{input_path}' ({len(raw_data)}) -> '{output_path}' ({len(compressed_data)}).")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Gzip a file and write it as a C header byte array.")
    parser.add_argument("input_file", help="The source file to be gzipped (e.g., index.htm)")
    parser.add_argument("output_file", nargs='?', help="Optional specific output filename (e.g., custom.h)")
    args = parser.parse_args()
    file_to_gzipped_c_header(args.input_file, args.output_file)