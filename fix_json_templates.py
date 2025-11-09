#!/usr/bin/env python3
"""
Script to update JSON format strings in json_templates.c
Adds signal_strength, network_type, and network_quality fields
"""

import sys

def update_json_templates(filepath):
    """Update the JSON templates in the file"""

    with open(filepath, 'r') as f:
        lines = f.readlines()

    # Track changes
    changes_made = 0

    # Update FLOW type (line 115, 0-indexed = 114)
    if lines[114].strip() == '"unit_id":"%s""':
        lines[114] = '                "unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s""\n'
        changes_made += 1
        print("✓ Updated FLOW type format string (line 115)")

    # Update LEVEL type (line 132, 0-indexed = 131)
    if lines[131].strip() == '"unit_id":"%s""':
        lines[131] = '                "unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s""\n'
        changes_made += 1
        print("✓ Updated LEVEL type format string (line 132)")

    # Update RAINGAUGE type (line 149, 0-indexed = 148)
    if lines[148].strip() == '"unit_id":"%s""':
        lines[148] = '                "unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s""\n'
        changes_made += 1
        print("✓ Updated RAINGAUGE type format string (line 149)")

    # Update BOREWELL type (line 166, 0-indexed = 165)
    if lines[165].strip() == '"unit_id":"%s""':
        lines[165] = '                "unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s""\n'
        changes_made += 1
        print("✓ Updated BOREWELL type format string (line 166)")

    # Update ENERGY type (line 207, 0-indexed = 206)
    if lines[206].strip() == '"meter":"%s""':
        lines[206] = '                "meter":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s""\n'
        changes_made += 1
        print("✓ Updated ENERGY type format string (line 207)")

    # Update QUALITY type (line 243, 0-indexed = 242)
    if lines[242].strip() == '"unit_id":"%s""':
        lines[242] = '                "unit_id":"%s","signal_strength":%d,"network_type":"%s","network_quality":"%s""\n'
        changes_made += 1
        print("✓ Updated QUALITY type format string (line 243)")

    # Write back to file
    with open(filepath, 'w') as f:
        f.writelines(lines)

    print(f"\n✅ Total changes made: {changes_made}/6")
    return changes_made

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print("Usage: python3 fix_json_templates.py <json_templates.c>")
        sys.exit(1)

    filepath = sys.argv[1]
    changes = update_json_templates(filepath)

    if changes == 6:
        print("\n🎉 All JSON templates updated successfully!")
        sys.exit(0)
    else:
        print(f"\n⚠️  Warning: Only {changes}/6 templates were updated")
        sys.exit(1)
