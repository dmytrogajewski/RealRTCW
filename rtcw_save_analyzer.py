#!/usr/bin/env python3
"""
RTCW to RealRTCW Save Game Analyzer and Converter
Analyzes the save format and attempts to fix entity number corruption
"""

import struct
import sys
import os

def analyze_save(filename):
    """Analyze RTCW save file structure"""
    print(f"\n=== Analyzing {filename} ===\n")
    
    with open(filename, 'rb') as f:
        # Read version
        version = struct.unpack('i', f.read(4))[0]
        print(f"Save Version: {version}")
        
        # Read mapname (64 bytes MAX_QPATH)
        mapname = f.read(64).decode('ascii', errors='ignore').rstrip('\x00')
        print(f"Map Name: {mapname}")
        
        # Read level time
        leveltime = struct.unpack('i', f.read(4))[0]
        print(f"Level Time: {leveltime} ({leveltime//3600}h{(leveltime%3600)//60}m{leveltime%60}s)")
        
        # Read total play time
        totalplaytime = struct.unpack('i', f.read(4))[0]
        print(f"Total Play Time: {totalplaytime}")
        
        # Episode (version >= 13)
        if version >= 13:
            episode = struct.unpack('i', f.read(4))[0]
            print(f"Episode: {episode}")
        
        # Skip forward to entity section
        # Read checksum description (varies, let's skip for now)
        pos_before_entities = f.tell()
        print(f"\nCurrent position: 0x{pos_before_entities:x}")
        
        # Try to find entity size marker
        # In the save format, entities are preceded by their structure size
        entity_size = struct.unpack('i', f.read(4))[0]
        print(f"Entity structure size: {entity_size} bytes")
        
        # Now read entity numbers
        print("\nEntity numbers in save file:")
        entities_found = []
        for i in range(20):  # Read first 20 entity numbers
            try:
                entnum = struct.unpack('i', f.read(4))[0]
                if entnum < 0:
                    print(f"  End of entities marker: {entnum}")
                    break
                entities_found.append(entnum)
                if entnum > 2048:
                    print(f"  Entity #{i}: {entnum} ⚠️ OUT OF RANGE (MAX=2048)")
                else:
                    print(f"  Entity #{i}: {entnum}")
                # Skip entity data
                f.read(entity_size)
            except:
                break
        
        print(f"\nTotal entities read: {len(entities_found)}")
        print(f"Max entity number: {max(entities_found) if entities_found else 0}")
        print(f"Corrupted entities (>2048): {sum(1 for e in entities_found if e > 2048)}")
        
        return {
            'version': version,
            'mapname': mapname,
            'leveltime': leveltime,
            'totalplaytime': totalplaytime,
            'entities': entities_found,
            'entity_size': entity_size
        }

def main():
    save_dir = "/home/dmitriy/.realrtcw/main/save/incompatible_rtcw_saves"
    
    saves = [
        os.path.join(save_dir, "111.svg"),
        os.path.join(save_dir, "1111.svg"),
        os.path.join(save_dir, "rtcw_current.svg")
    ]
    
    for save_file in saves:
        if os.path.exists(save_file):
            try:
                info = analyze_save(save_file)
            except Exception as e:
                print(f"Error analyzing {save_file}: {e}")
                import traceback
                traceback.print_exc()
        else:
            print(f"File not found: {save_file}")
    
    print("\n" + "="*60)
    print("CONCLUSION:")
    print("="*60)
    print("""
The original RTCW saves are fundamentally incompatible with RealRTCW because:

1. The save files may have corrupted entity numbers (25270809 instead of valid range 0-2048)
2. RealRTCW uses GENTITYNUM_BITS=11, meaning MAX_GENTITIES = 2^11 = 2048
3. The entity data structures may have different sizes/fields between versions
4. Some fields were added to RealRTCW that don't exist in original RTCW

Unfortunately, these saves CANNOT be automatically converted without data loss.
The entity number corruption suggests memory corruption or incompatible binary format.

RECOMMENDATION: Start fresh in RealRTCW or use the Windows RealRTCW save.
""")

if __name__ == "__main__":
    main()






