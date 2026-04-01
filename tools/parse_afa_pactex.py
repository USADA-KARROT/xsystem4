#!/usr/bin/env python3
"""
Parse AFA v2 archive and extract .pactex files for format analysis.
"""
import struct, zlib, os

AFA_PATH = "/Users/SKYLINE/Downloads/\u591a\u5a1c\u591a\u5a1c \u4e00\u8d77\u5e72\u574f\u4e8b\u5427/dohnadohnaPact.afa"

def read_u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]

def hexdump(data, length=256, width=16):
    result = []
    for i in range(0, min(len(data), length), width):
        chunk = data[i:i+width]
        hex_part = ' '.join(f'{b:02x}' for b in chunk)
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        result.append(f'  {i:08x}  {hex_part:<{width*3}}  |{ascii_part}|')
    return '\n'.join(result)

def main():
    print(f"=== Parsing AFA v2 ===")
    file_size = os.path.getsize(AFA_PATH)
    print(f"File size: {file_size:,} bytes ({file_size / 1024 / 1024:.1f} MB)")

    with open(AFA_PATH, 'rb') as f:
        hdr = f.read(28)
        magic = hdr[0:4]
        hdr_size = read_u32(hdr, 4)
        alic_arch = hdr[8:16]
        version = read_u32(hdr, 16)
        unknown = read_u32(hdr, 20)
        data_start = read_u32(hdr, 24)

        print(f"\n--- AFAH Header ---")
        print(f"Magic: {magic}")
        print(f"Header size: {hdr_size}")
        print(f"AlicArch: {alic_arch}")
        print(f"Version: {version}")
        print(f"Unknown: {unknown}")
        print(f"Data start: {data_start} (0x{data_start:x})")

        assert magic == b'AFAH', f"Bad magic: {magic}"
        assert alic_arch == b'AlicArch', f"Bad AlicArch: {alic_arch}"

        info_tag = f.read(4)
        assert info_tag == b'INFO', f"Expected INFO, got {info_tag}"
        info_compressed_size = read_u32(f.read(4), 0)
        info_uncompressed_size = read_u32(f.read(4), 0)
        info_file_count = read_u32(f.read(4), 0)

        print(f"\n--- INFO Section ---")
        print(f"Compressed size (field): {info_compressed_size}")
        print(f"Actual zlib size: {info_compressed_size - 16}")
        print(f"Uncompressed size: {info_uncompressed_size}")
        print(f"File count: {info_file_count}")

        zlib_size = info_compressed_size - 16
        zlib_data = f.read(zlib_size)
        info_data = zlib.decompress(zlib_data)
        print(f"Decompressed INFO: {len(info_data)} bytes (expected {info_uncompressed_size})")
        assert len(info_data) == info_uncompressed_size

        entries = []
        pos = 0
        for i in range(info_file_count):
            name_len = read_u32(info_data, pos); pos += 4
            padded_len = read_u32(info_data, pos); pos += 4
            name_bytes = info_data[pos:pos+padded_len]; pos += padded_len
            try:
                name = name_bytes[:name_len].decode('gb18030')
            except:
                name = name_bytes[:name_len].decode('shift_jis', errors='replace')
            unk0 = read_u32(info_data, pos); pos += 4
            unk1 = read_u32(info_data, pos); pos += 4
            data_offset = read_u32(info_data, pos); pos += 4
            data_size = read_u32(info_data, pos); pos += 4
            entries.append({
                'name': name, 'offset': data_offset, 'size': data_size,
                'unk0': unk0, 'unk1': unk1,
            })

        pactex_files = [e for e in entries if e['name'].lower().endswith('.pactex')]
        other_exts = set()
        for e in entries:
            _, ext = os.path.splitext(e['name'])
            other_exts.add(ext.lower())

        print(f"\n--- Archive Contents Summary ---")
        print(f"Total files: {len(entries)}")
        print(f"File extensions found: {sorted(other_exts)}")
        print(f".pactex files: {len(pactex_files)}")

        pactex_files.sort(key=lambda e: e['size'])

        print(f"\n--- All .pactex files (sorted by size) ---")
        for e in pactex_files:
            print(f"  {e['name']:50s}  size={e['size']:>10,}  offset=0x{e['offset']:08x}  unk0={e['unk0']}  unk1={e['unk1']}")

        print(f"\n{'='*80}")
        print(f"=== Extracting and analyzing smallest .pactex files ===")
        print(f"{'='*80}")

        for e in pactex_files[:5]:
            abs_offset = data_start + e['offset']
            f.seek(abs_offset)
            file_data = f.read(e['size'])

            print(f"\n--- {e['name']} ({e['size']} bytes) ---")
            print(f"Absolute offset in AFA: 0x{abs_offset:08x}")
            print(f"First 16 bytes: {file_data[:16].hex(' ')}")

            magic4 = file_data[:4]
            print(f"Magic (4 bytes): {magic4} / hex={magic4.hex()}")

            if file_data[0:2] in (b'\x78\x9c', b'\x78\x01', b'\x78\xda'):
                print(">>> Appears to be zlib compressed!")
                try:
                    decompressed = zlib.decompress(file_data)
                    print(f">>> Decompressed size: {len(decompressed)} bytes")
                    print(f">>> Decompressed first 16: {decompressed[:16].hex(' ')}")
                    print(f"Hex dump (decompressed, first 256 bytes):")
                    print(hexdump(decompressed, 256))
                except Exception as ex:
                    print(f">>> zlib decompress failed: {ex}")

            sigs = {
                b'PK': 'ZIP', b'\x89PNG': 'PNG', b'RIFF': 'RIFF', b'BM': 'BMP',
                b'QNT\x00': 'QNT', b'AJP\x00': 'AJP', b'DCF\x00': 'DCF',
                b'PMX\x00': 'PMX', b'POL\x00': 'POL',
            }
            for sig, name in sigs.items():
                if file_data[:len(sig)] == sig:
                    print(f">>> Matches {name} signature!")

            print(f"\nHex dump (raw, first 256 bytes):")
            print(hexdump(file_data, min(256, e['size'])))

            print(f"\nReadable strings (>= 4 chars):")
            current = []
            strings_found = []
            for j, b in enumerate(file_data[:min(512, e['size'])]):
                if 32 <= b < 127:
                    current.append(chr(b))
                else:
                    if len(current) >= 4:
                        strings_found.append((j - len(current), ''.join(current)))
                    current = []
            if len(current) >= 4:
                strings_found.append((min(512, e['size']) - len(current), ''.join(current)))
            for off, s in strings_found:
                print(f"  offset 0x{off:04x}: '{s}'")

        # Medium-sized
        if len(pactex_files) > 10:
            mid_idx = len(pactex_files) // 2
            e = pactex_files[mid_idx]
            abs_offset = data_start + e['offset']
            f.seek(abs_offset)
            file_data = f.read(min(e['size'], 512))
            print(f"\n{'='*80}")
            print(f"=== Medium-sized .pactex for comparison ===")
            print(f"--- {e['name']} ({e['size']:,} bytes) ---")
            print(f"First 16 bytes: {file_data[:16].hex(' ')}")
            print(f"Magic (4 bytes): {file_data[:4]} / hex={file_data[:4].hex()}")
            print(f"\nHex dump (first 256 bytes):")
            print(hexdump(file_data, 256))
            print(f"\nReadable strings (>= 4 chars):")
            current = []; strings_found = []
            for j, b in enumerate(file_data[:512]):
                if 32 <= b < 127: current.append(chr(b))
                else:
                    if len(current) >= 4: strings_found.append((j - len(current), ''.join(current)))
                    current = []
            if len(current) >= 4: strings_found.append((len(file_data[:512]) - len(current), ''.join(current)))
            for off, s in strings_found:
                print(f"  offset 0x{off:04x}: '{s}'")

        # Largest
        e = pactex_files[-1]
        abs_offset = data_start + e['offset']
        f.seek(abs_offset)
        file_data = f.read(min(e['size'], 512))
        print(f"\n{'='*80}")
        print(f"=== Largest .pactex ===")
        print(f"--- {e['name']} ({e['size']:,} bytes) ---")
        print(f"First 16 bytes: {file_data[:16].hex(' ')}")
        print(f"Magic (4 bytes): {file_data[:4]} / hex={file_data[:4].hex()}")
        print(f"\nHex dump (first 256 bytes):")
        print(hexdump(file_data, 256))
        print(f"\nReadable strings (>= 4 chars):")
        current = []; strings_found = []
        for j, b in enumerate(file_data[:512]):
            if 32 <= b < 127: current.append(chr(b))
            else:
                if len(current) >= 4: strings_found.append((j - len(current), ''.join(current)))
                current = []
        if len(current) >= 4: strings_found.append((len(file_data[:512]) - len(current), ''.join(current)))
        for off, s in strings_found:
            print(f"  offset 0x{off:04x}: '{s}'")

if __name__ == '__main__':
    main()
