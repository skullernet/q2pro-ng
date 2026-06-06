#!/usr/bin/python3

import struct
import sys
import zipfile

def checked_read(file, size):
    res = file.read(size)
    if len(res) != size:
        print("Error reading file")
        sys.exit(1)
    return res

def convert_pak_to_zip(pak_path, zip_path):
    with open(pak_path, 'rb') as pak_file:
        header = checked_read(pak_file, 12)

        magic, directory_offset, directory_size = struct.unpack('<4sII', header)
        if magic != b'PACK':
            print(f"{pak_path} is not a PAK file")
            sys.exit(1)

        num_files = directory_size // 64
        print(f"Found {num_files} files inside {pak_path}")

        pak_file.seek(directory_offset)
        directory_data = checked_read(pak_file, directory_size)

        with zipfile.ZipFile(zip_path, 'w', compression=zipfile.ZIP_DEFLATED) as zip_out:
            for i in range(num_files):
                entry_offset = i * 64
                entry_bytes = directory_data[entry_offset : entry_offset + 64]

                file_name_bytes, file_offset, file_size = struct.unpack('<56sII', entry_bytes)
                file_name = file_name_bytes.split(b'\x00')[0].decode('utf-8', errors='replace')

                pak_file.seek(file_offset)
                file_data = checked_read(pak_file, file_size)

                if file_name.endswith('.mat') or file_name.endswith('.png'):
                    zip_out.writestr(file_name, file_data, compress_type=zipfile.ZIP_STORED)
                else:
                    zip_out.writestr(file_name, file_data)

    print(f"Archive converted and saved to: {zip_path}")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: pak_to_zip.py <path_to_pak_file> <path_to_output_zip>")
        sys.exit(1)

    convert_pak_to_zip(sys.argv[1], sys.argv[2])
