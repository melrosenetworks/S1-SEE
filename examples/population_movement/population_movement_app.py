#!/usr/bin/env python3
"""
Population Movement Analysis Application

Main application that processes S1-SEE events to generate aggregated population movement visualizations.

Usage:
    python population_movement_app.py --events events.jsonl --output map.html [--db cell_sites.db]
"""

import argparse
import sys
import os
from pathlib import Path

# Add current directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from cell_site_db import CellSiteDB, init_sample_cell_sites
from journey_tracker import JourneyTracker, load_events_from_jsonl
from aggregator import JourneyAggregator
from map_visualizer import MapVisualizer


def process_events(events_file: str, cell_db: CellSiteDB,
                  max_journey_gap_seconds: int = 3600) -> tuple:
    """
    Process events from S1-SEE and track journeys.
    
    Returns:
        Tuple of (journey_tracker, completed_journeys)
    """
    print(f"Loading events from {events_file}...")
    events = load_events_from_jsonl(events_file)
    print(f"Loaded {len(events)} events")
    
    # Initialize journey tracker
    tracker = JourneyTracker(max_journey_gap_seconds=max_journey_gap_seconds)
    
    # Process each event
    print("Processing events and tracking journeys...")
    for i, event in enumerate(events):
        if (i + 1) % 100 == 0:
            print(f"  Processed {i + 1}/{len(events)} events...")
        tracker.process_event(event)
    
    # Complete all active journeys
    tracker.complete_all_journeys()
    journeys = tracker.get_completed_journeys()
    
    print(f"Tracked {len(journeys)} completed journeys")
    
    # Print statistics
    stats = tracker.get_journey_statistics()
    print(f"\nJourney Statistics:")
    print(f"  Total journeys: {stats['total_journeys']}")
    print(f"  Total visits: {stats['total_visits']}")
    print(f"  Average journey length: {stats['avg_journey_length']:.2f} visits")
    print(f"  Unique subscribers: {stats['unique_subscribers']}")
    
    return tracker, journeys


def aggregate_journeys(journeys, cell_db: CellSiteDB) -> JourneyAggregator:
    """Aggregate journeys to find shared segments."""
    print("\nAggregating journeys...")
    
    aggregator = JourneyAggregator()
    aggregator.aggregate_journeys(journeys)
    
    stats = aggregator.get_statistics()
    print(f"\nAggregation Statistics:")
    print(f"  Unique segments: {stats['total_unique_segments']}")
    print(f"  Cell sites: {stats['total_cells']}")
    print(f"  Total journey segments: {stats['total_journey_segments']}")
    print(f"  Average journeys per segment: {stats['avg_journeys_per_segment']:.2f}")
    
    if stats['most_traveled_segment']:
        print(f"\nMost traveled segment:")
        print(f"  {stats['most_traveled_segment']['from']} → "
              f"{stats['most_traveled_segment']['to']}")
        print(f"  {stats['most_traveled_segment']['journey_count']} journeys")
    
    # Print top segments
    top_segments = aggregator.get_top_segments(limit=10)
    if top_segments:
        print(f"\nTop 10 Most Traveled Segments:")
        for i, segment in enumerate(top_segments, 1):
            print(f"  {i}. {segment.from_cell} → {segment.to_cell}: "
                  f"{segment.journey_count} journeys, {segment.subscriber_count} subscribers")
    
    return aggregator


def create_visualization(aggregator: JourneyAggregator, cell_db: CellSiteDB,
                        output_file: str) -> str:
    """Create the map visualization."""
    print(f"\nCreating map visualization...")
    
    visualizer = MapVisualizer(cell_db)
    output_path = visualizer.create_movement_map(aggregator, output_file=output_file)
    
    print(f"Map saved to: {output_path}")
    print(f"Open {output_path} in a web browser to view the visualization")
    
    return output_path


def main():
    """Main application entry point."""
    parser = argparse.ArgumentParser(
        description="Generate aggregated population movement visualization from S1-SEE events"
    )
    parser.add_argument(
        "--events",
        required=True,
        help="Path to JSONL file containing S1-SEE events"
    )
    parser.add_argument(
        "--output",
        default="population_movement.html",
        help="Output HTML file for the map visualization (default: population_movement.html)"
    )
    parser.add_argument(
        "--db",
        default="cell_sites.db",
        help="Path to cell site database (default: cell_sites.db)"
    )
    parser.add_argument(
        "--init-db",
        action="store_true",
        help="Initialize database with sample cell sites"
    )
    parser.add_argument(
        "--max-journey-gap",
        type=int,
        default=3600,
        help="Maximum time gap (seconds) between visits to consider same journey (default: 3600)"
    )
    parser.add_argument(
        "--center-lat",
        type=float,
        help="Map center latitude (auto-calculated if not specified)"
    )
    parser.add_argument(
        "--center-lon",
        type=float,
        help="Map center longitude (auto-calculated if not specified)"
    )
    parser.add_argument(
        "--zoom",
        type=int,
        default=12,
        help="Initial map zoom level (default: 12)"
    )
    
    args = parser.parse_args()
    
    # Check if events file exists
    if not os.path.exists(args.events):
        print(f"Error: Events file not found: {args.events}", file=sys.stderr)
        sys.exit(1)
    
    # Initialize cell site database
    print(f"Initializing cell site database: {args.db}")
    cell_db = CellSiteDB(args.db)
    
    if args.init_db:
        print("Initializing database with sample cell sites...")
        init_sample_cell_sites(cell_db)
    
    # Check if database has cell sites
    all_cells = cell_db.get_all_cell_sites()
    if not all_cells:
        print("Warning: No cell sites found in database.")
        print("  Use --init-db to initialize with sample data, or")
        print("  add cell sites using the cell_site_db.py module")
        response = input("Continue anyway? (y/n): ")
        if response.lower() != 'y':
            sys.exit(1)
    else:
        print(f"Found {len(all_cells)} cell sites in database")
    
    try:
        # Process events
        tracker, journeys = process_events(args.events, cell_db, args.max_journey_gap)
        
        if not journeys:
            print("Warning: No journeys were tracked from the events.")
            print("  Make sure the events contain mobility events with cell_id attributes.")
            sys.exit(1)
        
        # Aggregate journeys
        aggregator = aggregate_journeys(journeys, cell_db)
        
        # Create visualization
        create_visualization(aggregator, cell_db, args.output)
        
        print("\n✓ Analysis complete!")
        
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        cell_db.close()


if __name__ == "__main__":
    main()

