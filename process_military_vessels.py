#!/usr/bin/env python3
"""
Process military_vessels.json to add power, defense, health fields to vessels
and transform armament data into structured Armament objects.
"""

import json
import re
import sys
from typing import Dict, List, Any, Optional, Tuple


def extract_numeric_value(text: str, default: float = 0.0) -> float:
    """Extract the first numeric value from a string."""
    if not text:
        return default
    # Look for numbers including decimals
    match = re.search(r'[\d,]+\.?\d*', text.replace(',', ''))
    if match:
        try:
            return float(match.group())
        except ValueError:
            pass
    return default


def parse_displacement(displacement_str: str) -> float:
    """Extract displacement in tons from string like '120 tons'."""
    return extract_numeric_value(displacement_str, 0.0)


def extract_cost_from_ships_built(ships_built: str) -> float:
    """
    Extract unit cost from ships_built field.
    Examples: '12 Approx. unit cost $20', '3 Built'
    """
    # Look for dollar amounts or numbers after "cost"
    # Pattern: $ followed by digits, possibly with decimal
    cost_match = re.search(r'\$[\d,]+\.?\d*', ships_built)
    if cost_match:
        return extract_numeric_value(cost_match.group())

    # Fallback: look for any number that might be a cost
    numbers = re.findall(r'[\d,]+\.?\d*', ships_built.replace(',', ''))
    if numbers:
        # Often the last number is the cost, or look for context
        for num in reversed(numbers):
            val = float(num)
            # Reasonable cost for a ship (millions)
            if 1 <= val <= 10000:  # $1M to $10B
                return val

    return 0.0


def parse_armament_string(armament_str: str) -> Tuple[int, str]:
    """
    Parse an armament string to extract quantity and description.
    Examples:
    '1 × MSI 30mm DS30M Mark 2 Cannon' -> (1, 'MSI 30mm DS30M Mark 2 Cannon')
    '2 × 7.62mm MGs' -> (2, '7.62mm MGs')
    '1 × 50 cal/ 12.7mm MG' -> (1, '50 cal/ 12.7mm MG')
    If no × found, assume quantity=1 and whole string is description.
    """
    # Pattern for quantity × description
    pattern = r'^(\d+)\s*[×x]\s*(.+)$'
    match = re.match(pattern, armament_str.strip())
    if match:
        quantity = int(match.group(1))
        description = match.group(2).strip()
        return quantity, description

    # No quantity specified
    return 1, armament_str.strip()


def determine_weapon_type(description: str) -> str:
    """
    Determine weapon type from description.
    Returns: machine gun, torpedo, rocket, missile, gun
    """
    desc_lower = description.lower()

    # Check for specific types in order of specificity
    if any(word in desc_lower for word in ['torpedo', 'torpedo tube']):
        return 'torpedo'
    elif any(word in desc_lower for word in ['rocket', 'rocket launcher']):
        return 'rocket'
    elif any(word in desc_lower for word in ['missile', 'sam', 'cruise missile', 'anti-ship']):
        return 'missile'
    elif any(word in desc_lower for word in ['gun', 'cannon', 'howitzer', 'artillery']):
        # But exclude machine guns and autocannons from general gun category
        if not any(word in desc_lower for word in ['machine gun', 'mg', 'auto cannon', 'autocannon']):
            return 'gun'
    elif any(word in desc_lower for word in ['machine gun', 'mg', '.50 cal', '12.7mm', '7.62mm', '30mm', '20mm', 'auto cannon', 'autocannon']):
        return 'machine gun'

    # Default fallback
    return 'machine gun'


def determine_fire_category(weapon_type: str) -> str:
    """
    Determine FireCategory based on weapon type.
    Returns: light, medium, heavy
    """
    if weapon_type in ['machine gun']:
        return 'light'
    elif weapon_type in ['rocket', 'gun']:
        return 'medium'
    elif weapon_type in ['torpedo', 'missile']:
        return 'heavy'
    else:
        return 'light'  # default


def estimate_armament_values(weapon_type: str) -> Dict[str, Any]:
    """
    Estimate reasonable values for armament fields based on weapon type.
    Returns a dictionary with damage, power, reload_time, ammo,
    is_efficient_against_ships, is_efficient_against_submarine, radius, cost.

    `ammo` is one magazine, not the ship's whole supply, so it is deliberately not
    multiplied by how many mounts the vessel carries: each mount cycles its own magazine.
    `reload_time` is the time to change that magazine, not the time between two rounds.
    """
    # Base values per weapon type (per unit)
    base_values = {
        'machine gun': {
            'damage': 10.0,
            'power': 8.0,
            'reload_time': 4.0,  # seconds to change the belt/box
            'ammo': 1000.0,
            'is_efficient_against_ships': True,
            'is_efficient_against_submarine': False,
            'radius': 3.0,  # nautical miles effective range
            'cost': 5  # cost per ammo unit
        },
        'torpedo': {
            'damage': 150.0,
            'power': 40.0,
            'reload_time': 30.0,  # seconds (slow reload)
            'ammo': 8.0,
            'is_efficient_against_ships': True,
            'is_efficient_against_submarine': True,
            'radius': 15.0,
            'cost': 200
        },
        'rocket': {
            'damage': 40.0,
            'power': 20.0,
            'reload_time': 10.0,
            'ammo': 24.0,
            'is_efficient_against_ships': True,
            'is_efficient_against_submarine': False,
            'radius': 8.0,
            'cost': 50
        },
        'missile': {
            'damage': 250.0,
            'power': 60.0,
            'reload_time': 60.0,
            'ammo': 6.0,
            'is_efficient_against_ships': True,
            'is_efficient_against_submarine': False,  # unless specifically ASW
            'radius': 50.0,
            'cost': 1000
        },
        'gun': {
            'damage': 60.0,
            'power': 25.0,
            'reload_time': 5.0,
            'ammo': 400.0,
            'is_efficient_against_ships': True,
            'is_efficient_against_submarine': False,
            'radius': 12.0,
            'cost': 20
        }
    }

    # Get base values for weapon type, default to machine gun. `ammo` stays one magazine for
    # a single mount; the vessel's mount count does not change how many rounds a magazine holds.
    values = base_values.get(weapon_type, base_values['machine gun']).copy()

    # Special case: anti-submarine weapons
    if 'anti-submarine' in weapon_type or 'asw' in weapon_type:
        values['is_efficient_against_submarine'] = True
        values['is_efficient_against_ships'] = False

    return values


def generate_armament_name(description: str, weapon_type: str, index: int) -> str:
    """
    Generate a short name for the armament.
    """
    # Try to extract a meaningful name from description
    # Remove common prefixes and suffixes
    name = description.strip()

    # Remove leading numbers and symbols
    name = re.sub(r'^[\d\s×x\.]+', '', name)

    # Take first few meaningful words
    words = name.split()
    if len(words) > 3:
        # Try to keep caliber/type info
        name = ' '.join(words[:3])
    else:
        name = ' '.join(words)

    # If name is empty or too short, use weapon type + index
    if not name or len(name) < 2:
        name = f"{weapon_type.title()} {index + 1}"

    # Limit length
    if len(name) > 50:
        name = name[:47] + "..."

    return name


def process_vessel(vessel: Dict[str, Any]) -> Dict[str, Any]:
    """
    Process a single vessel to add power, defense, health and transform armament.
    """
    # Make a copy to avoid modifying original during iteration
    processed = vessel.copy()

    # Add power, defense, health
    displacement_str = vessel.get('displacement', '0 tons')
    displacement = parse_displacement(displacement_str)

    ships_built = vessel.get('ships_built', '')
    cost = extract_cost_from_ships_built(ships_built)

    # Power based on cost (if cost is 0, use displacement as fallback)
    if cost > 0:
        power = cost  # Direct mapping for simplicity
    else:
        power = displacement * 0.1  # Fallback scaling

    defense = displacement * 0.5
    health = displacement * 2.0

    processed['power'] = float(power)
    processed['defense'] = float(defense)
    processed['health'] = float(health)

    # Transform armament
    if 'armament' in vessel and isinstance(vessel['armament'], list):
        armament_objects = []
        for idx, armament_str in enumerate(vessel['armament']):
            if not isinstance(armament_str, str):
                # Skip non-string entries
                continue

            quantity, description = parse_armament_string(armament_str)
            weapon_type = determine_weapon_type(description)
            fire_category = determine_fire_category(weapon_type)

            # Generate name
            name = generate_armament_name(description, weapon_type, idx)

            # Get estimated values
            est_values = estimate_armament_values(weapon_type)

            # Create armament object matching the C++ struct
            # Map FireCategory to int: light=0, medium=1, heavy=2
            category_map = {'light': 0, 'medium': 1, 'heavy': 2}
            armament_obj = {
                'id': idx,  # Sequential ID within this vessel
                'name': name,
                'type': weapon_type,
                'category': category_map[fire_category],  # Store as int to match enum
                'description': description,  # Keep original description
                'damage': est_values['damage'],
                'power': est_values['power'],  # Weapon power rating
                'reload_time': est_values['reload_time'],
                'ammo': est_values['ammo'],
                'is_efficient_against_ships': est_values['is_efficient_against_ships'],
                'is_efficient_against_submarine': est_values['is_efficient_against_submarine'],
                'radius': est_values['radius'],
                'cost': est_values['cost']  # Cost per ammo unit
            }

            armament_objects.append(armament_obj)

        # Replace the armament list
        processed['armament'] = armament_objects

    return processed


def main():
    input_file = 'military_vessels.json'
    output_file = 'military_vessels_processed.json'

    print(f"Reading {input_file}...")

    try:
        with open(input_file, 'r', encoding='utf-8') as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"Error: {input_file} not found")
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error: Invalid JSON in {input_file}: {e}")
        sys.exit(1)

    # Extract vessels array
    if 'vessels' not in data or not isinstance(data['vessels'], list):
        print("Error: No 'vessels' array found in JSON")
        sys.exit(1)

    vessels = data['vessels']
    print(f"Found {len(vessels)} vessels to process")

    # Process each vessel
    processed_vessels = []
    for i, vessel in enumerate(vessels):
        if i % 50 == 0 and i > 0:
            print(f"  Processed {i}/{len(vessels)} vessels...")
        processed_vessels.append(process_vessel(vessel))

    # Update the data
    data['vessels'] = processed_vessels

    # Also update the scraped_at timestamp to now
    from datetime import datetime, timezone
    data['scraped_at'] = datetime.now(timezone.utc).isoformat(timespec="seconds")

    # Write processed data
    print(f"Writing processed data to {output_file}...")
    try:
        with open(output_file, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    except Exception as e:
        print(f"Error writing to {output_file}: {e}")
        sys.exit(1)

    print("Processing complete!")
    print(f"Original file: {input_file}")
    print(f"Processed file: {output_file}")

    # Show sample of what was added
    if processed_vessels:
        sample = processed_vessels[0]
        print("\nSample vessel after processing:")
        print(f"  Name: {sample.get('ship_name', 'Unknown')}")
        print(f"  Power: {sample.get('power', 0)}")
        print(f"  Defense: {sample.get('defense', 0)}")
        print(f"  Health: {sample.get('health', 0)}")
        if 'armament' in sample and isinstance(sample['armament'], list):
            print(f"  Armament entries: {len(sample['armament'])}")
            if sample['armament']:
                first_arm = sample['armament'][0]
                print(f"  First armament: {first_arm.get('name', 'Unknown')} ({first_arm.get('type', 'Unknown')})")


if __name__ == '__main__':
    main()