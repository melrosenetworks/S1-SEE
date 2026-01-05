#!/usr/bin/env python3
"""
Sample Event Generator

Generates sample S1-SEE events for testing the population movement application.
Creates realistic mobility patterns with multiple subscribers moving between cell sites.
"""

import json
import random
import argparse
from datetime import datetime, timedelta


def generate_sample_events(output_file: str, num_subscribers: int = 50,
                          num_events_per_subscriber: int = 10,
                          num_journeys: int = None,
                          cell_sites: list = None):
    """
    Generate sample S1-SEE mobility events.
    
    Args:
        output_file: Path to output JSONL file
        num_subscribers: Number of unique subscribers (ignored if num_journeys is set)
        num_events_per_subscriber: Average number of events per subscriber (ignored if num_journeys is set)
        num_journeys: Target number of journeys to generate (takes precedence)
        cell_sites: List of cell site ECGIs (uses defaults if None)
    """
    if cell_sites is None:
        # Default cell sites (matching sample database)
        cell_sites = [
            "001001:0000001",
            "001001:0000002",
            "001001:0000003",
            "001001:0000004",
            "001001:0000005",
        ]
    
    # If num_journeys is specified, calculate subscribers needed
    # Each journey needs at least 2 events (2 cell visits), average ~3-4 events per journey
    if num_journeys is not None:
        avg_events_per_journey = 3.5
        total_events_needed = int(num_journeys * avg_events_per_journey)
        # Distribute across subscribers (each subscriber can have multiple journeys)
        num_subscribers = max(100, int(num_journeys / 2))  # At least 2 journeys per subscriber on average
        num_events_per_subscriber = max(3, int(total_events_needed / num_subscribers))
        print(f"Generating {num_journeys} journeys with ~{num_subscribers} subscribers")
    
    # Base timestamp (current time)
    base_time = int(datetime.now().timestamp() * 1e9)
    
    # For large datasets, write incrementally to avoid memory issues
    use_incremental = num_journeys is not None and num_journeys > 1000
    
    if use_incremental:
        # Write events incrementally
        events_written = 0
        journey_count = 0
        all_events = []  # Small buffer for sorting
        
        with open(output_file, 'w') as f:
            subscriber_idx = 0
            event_time = base_time
            
            while journey_count < num_journeys:
                subscriber_key = f"IMSI:{123456789000000 + subscriber_idx}"
                
                # Determine route pattern
                route_type = random.random()
                if route_type < 0.3:
                    # 30% follow a common route: 1 -> 2 -> 3 -> 4
                    route = cell_sites[:4]
                elif route_type < 0.6:
                    # 30% follow another common route: 1 -> 3 -> 5
                    route = [cell_sites[0], cell_sites[2], cell_sites[4]]
                else:
                    # 40% have random routes
                    route = random.sample(cell_sites, min(len(cell_sites), random.randint(2, 5)))
                
                # Generate events along the route
                current_cell = None
                journey_events = []
                
                for i, next_cell in enumerate(route):
                    # Time between handovers (1-10 minutes)
                    if i > 0:
                        gap_seconds = random.randint(60, 600)
                        event_time += gap_seconds * 1_000_000_000
                    
                    event = {
                        "name": "Mobility.Handover.Notified",
                        "ts": event_time,
                        "subscriber_key": subscriber_key,
                        "attributes": {
                            "target_cell_id": next_cell,
                        },
                        "confidence": 1.0,
                        "ruleset_id": "mobility",
                        "ruleset_version": "1.0"
                    }
                    
                    if current_cell:
                        event["attributes"]["source_cell_id"] = current_cell
                    
                    journey_events.append(event)
                    current_cell = next_cell
                
                # Add some random additional movements (0-2 extra events)
                num_additional = random.randint(0, 2)
                for _ in range(num_additional):
                    gap_seconds = random.randint(60, 600)
                    event_time += gap_seconds * 1_000_000_000
                    
                    if random.random() < 0.7:  # 70% chance of moving
                        next_cell = random.choice(cell_sites)
                    else:
                        next_cell = current_cell
                    
                    event = {
                        "name": "Mobility.Handover.Notified",
                        "ts": event_time,
                        "subscriber_key": subscriber_key,
                        "attributes": {
                            "source_cell_id": current_cell,
                            "target_cell_id": next_cell,
                        },
                        "confidence": 1.0,
                        "ruleset_id": "mobility",
                        "ruleset_version": "1.0"
                    }
                    
                    journey_events.append(event)
                    current_cell = next_cell
                
                # Add to buffer
                all_events.extend(journey_events)
                journey_count += 1
                
                # Write in batches to maintain approximate chronological order
                if len(all_events) >= 1000:
                    all_events.sort(key=lambda x: x["ts"])
                    for event in all_events:
                        f.write(json.dumps(event) + '\n')
                        events_written += 1
                    all_events = []
                
                # Move to next subscriber periodically
                if random.random() < 0.3:  # 30% chance to switch subscriber
                    subscriber_idx += 1
                    # Add time gap between different subscribers
                    event_time += random.randint(0, 3600) * 1_000_000_000
            
            # Write remaining events
            if all_events:
                all_events.sort(key=lambda x: x["ts"])
                for event in all_events:
                    f.write(json.dumps(event) + '\n')
                    events_written += 1
        
        # Final sort of the entire file to ensure chronological order
        print("Sorting events chronologically...")
        with open(output_file, 'r') as f:
            events = [json.loads(line) for line in f]
        
        events.sort(key=lambda x: x["ts"])
        
        with open(output_file, 'w') as f:
            for event in events:
                f.write(json.dumps(event) + '\n')
        
        print(f"Generated {len(events)} events for {journey_count} journeys")
        print(f"Events written to: {output_file}")
        if events:
            print(f"Time range: {datetime.fromtimestamp(events[0]['ts'] / 1e9)} to {datetime.fromtimestamp(events[-1]['ts'] / 1e9)}")
        
    else:
        # Original approach for smaller datasets
        events = []
        
        # Generate events for each subscriber
        for subscriber_idx in range(num_subscribers):
            subscriber_key = f"IMSI:{123456789000000 + subscriber_idx}"
            
            # Each subscriber follows a route (some shared, some unique)
            if subscriber_idx < num_subscribers * 0.3:
                # 30% follow a common route: 1 -> 2 -> 3 -> 4
                route = cell_sites[:4]
            elif subscriber_idx < num_subscribers * 0.6:
                # 30% follow another common route: 1 -> 3 -> 5
                route = [cell_sites[0], cell_sites[2], cell_sites[4]]
            else:
                # 40% have random routes
                route = random.sample(cell_sites, min(len(cell_sites), random.randint(2, 5)))
            
            # Generate events along the route
            current_cell = None
            event_time = base_time
            
            for i, next_cell in enumerate(route):
                # Time between handovers (1-10 minutes)
                if i > 0:
                    gap_seconds = random.randint(60, 600)
                    event_time += gap_seconds * 1_000_000_000
                
                # Create handover event
                event = {
                    "name": "Mobility.Handover.Notified",
                    "ts": event_time,
                    "subscriber_key": subscriber_key,
                    "attributes": {
                        "target_cell_id": next_cell,
                    },
                    "confidence": 1.0,
                    "ruleset_id": "mobility",
                    "ruleset_version": "1.0"
                }
                
                if current_cell:
                    event["attributes"]["source_cell_id"] = current_cell
                
                events.append(event)
                current_cell = next_cell
            
            # Add some random additional movements
            num_additional = max(0, random.randint(0, max(0, num_events_per_subscriber - len(route))))
            for _ in range(num_additional):
                gap_seconds = random.randint(60, 600)
                event_time += gap_seconds * 1_000_000_000
                
                # Random next cell (could be same or different)
                if random.random() < 0.7:  # 70% chance of moving
                    next_cell = random.choice(cell_sites)
                else:
                    next_cell = current_cell
                
                event = {
                    "name": "Mobility.Handover.Notified",
                    "ts": event_time,
                    "subscriber_key": subscriber_key,
                    "attributes": {
                        "source_cell_id": current_cell,
                        "target_cell_id": next_cell,
                    },
                    "confidence": 1.0,
                    "ruleset_id": "mobility",
                    "ruleset_version": "1.0"
                }
                
                events.append(event)
                current_cell = next_cell
        
        # Sort events by timestamp
        events.sort(key=lambda x: x["ts"])
        
        # Write to JSONL file
        with open(output_file, 'w') as f:
            for event in events:
                f.write(json.dumps(event) + '\n')
        
        print(f"Generated {len(events)} events for {num_subscribers} subscribers")
        print(f"Events written to: {output_file}")
        if events:
            print(f"Time range: {datetime.fromtimestamp(events[0]['ts'] / 1e9)} to {datetime.fromtimestamp(events[-1]['ts'] / 1e9)}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate sample S1-SEE events for testing"
    )
    parser.add_argument(
        "--output",
        default="sample_events.jsonl",
        help="Output JSONL file (default: sample_events.jsonl)"
    )
    parser.add_argument(
        "--subscribers",
        type=int,
        default=50,
        help="Number of subscribers (default: 50)"
    )
    parser.add_argument(
        "--events-per-subscriber",
        type=int,
        default=10,
        help="Average number of events per subscriber (default: 10)"
    )
    parser.add_argument(
        "--cell-sites",
        nargs="+",
        help="List of cell site ECGIs (uses defaults if not specified)"
    )
    parser.add_argument(
        "--journeys",
        type=int,
        help="Target number of journeys to generate (takes precedence over --subscribers)"
    )
    
    args = parser.parse_args()
    
    generate_sample_events(
        output_file=args.output,
        num_subscribers=args.subscribers,
        num_events_per_subscriber=args.events_per_subscriber,
        num_journeys=args.journeys,
        cell_sites=args.cell_sites
    )


if __name__ == "__main__":
    main()

