#!/usr/bin/env python3
"""
Generate PROGMEM header file from HTML files with gzip compression
"""

import os
import gzip
import pathlib
from datetime import datetime

# Configuration
_SCRIPT_DIR = pathlib.Path(__file__).parent
_LIB_HTML_GLOB = ".pio/libdeps/*/BMM350_BMI3XY_Compass/BMM350_BMI3XY_Compass/html"
_matches = sorted(_SCRIPT_DIR.glob(_LIB_HTML_GLOB))
HTML_FOLDER = _matches[0] if _matches else _SCRIPT_DIR / _LIB_HTML_GLOB
OUTPUT_HEADER = _SCRIPT_DIR / "include/Html_compass.h"
NAMESPACE = "WebContent"

def get_mime_type(filename):
    """Get MIME type based on file extension"""
    ext = pathlib.Path(filename).suffix.lower()
    mime_types = {
        '.html': 'text/html',
        '.css': 'text/css',
        '.js': 'application/javascript',
        '.json': 'application/json',
        '.png': 'image/png',
        '.jpg': 'image/jpeg',
        '.jpeg': 'image/jpeg',
        '.gif': 'image/gif',
        '.svg': 'image/svg+xml',
        '.ico': 'image/x-icon',
        '.txt': 'text/plain',
    }
    return mime_types.get(ext, 'application/octet-stream')

def compress_file(file_path):
    """Read and gzip compress a file"""
    with open(file_path, 'rb') as f:
        original_data = f.read()
    
    compressed_data = gzip.compress(original_data, compresslevel=9)
    
    print(f"  {os.path.basename(file_path)}: {len(original_data)} -> {len(compressed_data)} bytes "
          f"({100 * len(compressed_data) / len(original_data):.1f}%)")
    
    return original_data, compressed_data

def bytes_to_c_array(data, var_name, bytes_per_line=16):
    """Convert bytes to C array format"""
    lines = []
    lines.append(f"const uint8_t {var_name}[] PROGMEM = {{")
    
    for i in range(0, len(data), bytes_per_line):
        chunk = data[i:i+bytes_per_line]
        hex_values = ', '.join(f'0x{b:02x}' for b in chunk)
        lines.append(f"  {hex_values},")
    
    # Remove trailing comma from last line
    if lines[-1].endswith(','):
        lines[-1] = lines[-1][:-1]
    
    lines.append("};")
    return '\n'.join(lines)

def sanitize_var_name(filename):
    """Convert filename to valid C variable name"""
    name = os.path.splitext(os.path.basename(filename))[0]
    # Replace non-alphanumeric characters with underscores
    name = ''.join(c if c.isalnum() else '_' for c in name)
    return name

def generate_header():
    """Generate the PROGMEM header file"""
    
    print(f"Generating PROGMEM header from {HTML_FOLDER}/ to {OUTPUT_HEADER}")
    print("=" * 60)
    
    # Find all files in html folder
    html_path = pathlib.Path(HTML_FOLDER)
    if not html_path.exists():
        print(f"Error: {HTML_FOLDER} folder not found!")
        return
    
    # Include only web-related files, exclude system files
    valid_extensions = {'.html', '.css', '.js', '.json', '.svg', '.png', '.jpg', '.jpeg', '.gif', '.ico', '.txt'}
    files = sorted([f for f in html_path.glob('*') 
                   if f.is_file() and f.suffix.lower() in valid_extensions])
    
    if not files:
        print(f"Warning: No files found in {HTML_FOLDER}/ folder")
        return
    
    # Process each file
    file_data = []
    for file_path in files:
        print(f"\nProcessing {file_path.name}...")
        original, compressed = compress_file(file_path)
        
        var_name = sanitize_var_name(file_path.name)
        mime_type = get_mime_type(file_path.name)
        
        file_data.append({
            'filename': file_path.name,
            'var_name': var_name,
            'mime_type': mime_type,
            'original_size': len(original),
            'compressed_size': len(compressed),
            'compressed_data': compressed
        })
    
    # Generate header file
    print(f"\n{'=' * 60}")
    print(f"Writing header file to {OUTPUT_HEADER}...")
    
    header_lines = []
    
    # Header guard and includes
    header_lines.append("// Auto-generated file - DO NOT EDIT MANUALLY")
    header_lines.append(f"// Generated: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    header_lines.append("")
    header_lines.append("#ifndef HTML_COMPASS_H")
    header_lines.append("#define HTML_COMPASS_H")
    header_lines.append("")
    header_lines.append("#include <Arduino.h>")
    header_lines.append("")
    header_lines.append(f"namespace {NAMESPACE} {{")
    header_lines.append("")
    
    # Generate PROGMEM arrays for each file
    for data in file_data:
        header_lines.append(f"// {data['filename']} - Original: {data['original_size']} bytes, "
                          f"Compressed: {data['compressed_size']} bytes")
        header_lines.append(bytes_to_c_array(data['compressed_data'], 
                                             f"{data['var_name']}_gz"))
        header_lines.append(f"const size_t {data['var_name']}_gz_len = {data['compressed_size']};")
        header_lines.append("")
    
    # Generate file info struct
    header_lines.append("// File information structure")
    header_lines.append("struct FileInfo {")
    header_lines.append("  const char* path;")
    header_lines.append("  const char* mimeType;")
    header_lines.append("  const uint8_t* data;")
    header_lines.append("  const size_t size;")
    header_lines.append("  const bool gzipped;")
    header_lines.append("};")
    header_lines.append("")
    
    # Generate file table
    header_lines.append("// File table")
    header_lines.append("const FileInfo files[] = {")
    for data in file_data:
        # Determine the URL path (/ for index.html, otherwise /filename)
        url_path = "/" if data['filename'].lower() == 'index.html' else f"/{data['filename']}"
        header_lines.append(f"  {{\"{url_path}\", \"{data['mime_type']}\", "
                          f"{data['var_name']}_gz, {data['var_name']}_gz_len, true}},")
    header_lines.append("};")
    header_lines.append("")
    header_lines.append(f"const size_t filesCount = {len(file_data)};")
    header_lines.append("")
    
    # Close namespace and header guard
    header_lines.append(f"}} // namespace {NAMESPACE}")
    header_lines.append("")
    header_lines.append("#endif // HTML_COMPASS_H")
    
    # Write to file
    output_path = pathlib.Path(OUTPUT_HEADER)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(header_lines))
    
    # Summary
    print(f"\n{'=' * 60}")
    print("Summary:")
    print(f"  Files processed: {len(file_data)}")
    total_original = sum(d['original_size'] for d in file_data)
    total_compressed = sum(d['compressed_size'] for d in file_data)
    print(f"  Total original size: {total_original:,} bytes")
    print(f"  Total compressed size: {total_compressed:,} bytes")
    print(f"  Compression ratio: {100 * total_compressed / total_original:.1f}%")
    print(f"  Space saved: {total_original - total_compressed:,} bytes")
    print(f"\nHeader file generated: {OUTPUT_HEADER}")
    print("=" * 60)

if __name__ == "__main__":
    generate_header()
