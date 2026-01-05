#!/usr/bin/env python3
"""
Aggregation Engine Module

Aggregates individual journeys to find shared journey segments and population movement patterns.
"""

from typing import Dict, List, Tuple, Set, Optional
from collections import defaultdict
from dataclasses import dataclass
from journey_tracker import Journey


@dataclass
class SegmentFlow:
    """Represents aggregated flow on a journey segment."""
    from_cell: str
    to_cell: str
    journey_count: int  # Number of journeys using this segment
    subscriber_count: int  # Number of unique subscribers
    first_seen: int  # Timestamp of first occurrence
    last_seen: int  # Timestamp of last occurrence


@dataclass
class CellFlow:
    """Represents aggregated flow through a cell site."""
    cell_id: str
    total_entries: int
    total_exits: int
    unique_subscribers: Set[str]
    entry_segments: List[Tuple[str, str]]  # (from_cell, to_cell) tuples
    exit_segments: List[Tuple[str, str]]  # (from_cell, to_cell) tuples


class JourneyAggregator:
    """Aggregates journeys to find shared segments and movement patterns."""
    
    def __init__(self):
        """Initialize the aggregator."""
        self.segment_flows: Dict[Tuple[str, str], SegmentFlow] = {}
        self.cell_flows: Dict[str, CellFlow] = {}
        self.journey_segments: List[Tuple[str, str]] = []  # All segments from all journeys
    
    def aggregate_journeys(self, journeys: List[Journey]):
        """
        Aggregate a list of journeys to find shared segments.
        
        Args:
            journeys: List of Journey objects to aggregate
        """
        # Reset state
        self.segment_flows.clear()
        self.cell_flows.clear()
        self.journey_segments.clear()
        
        # Track subscribers per segment
        segment_subscribers: Dict[Tuple[str, str], Set[str]] = defaultdict(set)
        
        # Process each journey
        for journey in journeys:
            segments = journey.get_segments()
            self.journey_segments.extend(segments)
            
            # Track segments
            for segment in segments:
                from_cell, to_cell = segment
                
                # Update segment flow
                if segment not in self.segment_flows:
                    self.segment_flows[segment] = SegmentFlow(
                        from_cell=from_cell,
                        to_cell=to_cell,
                        journey_count=0,
                        subscriber_count=0,
                        first_seen=journey.start_time,
                        last_seen=journey.end_time
                    )
                
                flow = self.segment_flows[segment]
                flow.journey_count += 1
                segment_subscribers[segment].add(journey.subscriber_key)
                
                # Update timestamps
                if journey.start_time < flow.first_seen:
                    flow.first_seen = journey.start_time
                if journey.end_time > flow.last_seen:
                    flow.last_seen = journey.end_time
                
                # Update cell flows
                self._update_cell_flow(to_cell, segment, journey.subscriber_key, is_entry=True)
                self._update_cell_flow(from_cell, segment, journey.subscriber_key, is_entry=False)
        
        # Update subscriber counts
        for segment, subscribers in segment_subscribers.items():
            if segment in self.segment_flows:
                self.segment_flows[segment].subscriber_count = len(subscribers)
    
    def _update_cell_flow(self, cell_id: str, segment: Tuple[str, str],
                         subscriber_key: str, is_entry: bool):
        """Update cell flow statistics."""
        if cell_id not in self.cell_flows:
            self.cell_flows[cell_id] = CellFlow(
                cell_id=cell_id,
                total_entries=0,
                total_exits=0,
                unique_subscribers=set(),
                entry_segments=[],
                exit_segments=[]
            )
        
        flow = self.cell_flows[cell_id]
        flow.unique_subscribers.add(subscriber_key)
        
        if is_entry:
            flow.total_entries += 1
            flow.entry_segments.append(segment)
        else:
            flow.total_exits += 1
            flow.exit_segments.append(segment)
    
    def get_top_segments(self, limit: int = 10) -> List[SegmentFlow]:
        """Get the top N most traveled segments by journey count."""
        sorted_segments = sorted(
            self.segment_flows.values(),
            key=lambda x: x.journey_count,
            reverse=True
        )
        return sorted_segments[:limit]
    
    def get_segment_flow(self, from_cell: str, to_cell: str) -> Optional[SegmentFlow]:
        """Get flow information for a specific segment."""
        segment = (from_cell, to_cell)
        return self.segment_flows.get(segment)
    
    def get_cell_flow(self, cell_id: str) -> Optional[CellFlow]:
        """Get flow information for a specific cell."""
        return self.cell_flows.get(cell_id)
    
    def get_all_segments(self) -> List[SegmentFlow]:
        """Get all segment flows."""
        return list(self.segment_flows.values())
    
    def get_all_cells(self) -> List[CellFlow]:
        """Get all cell flows."""
        return list(self.cell_flows.values())
    
    def get_statistics(self) -> Dict:
        """Get aggregation statistics."""
        total_segments = len(self.segment_flows)
        total_cells = len(self.cell_flows)
        total_journey_segments = len(self.journey_segments)
        
        if total_segments == 0:
            return {
                "total_unique_segments": 0,
                "total_cells": 0,
                "total_journey_segments": 0,
                "avg_journeys_per_segment": 0,
                "most_traveled_segment": None
            }
        
        avg_journeys = sum(f.journey_count for f in self.segment_flows.values()) / total_segments
        top_segment = self.get_top_segments(limit=1)
        
        return {
            "total_unique_segments": total_segments,
            "total_cells": total_cells,
            "total_journey_segments": total_journey_segments,
            "avg_journeys_per_segment": avg_journeys,
            "most_traveled_segment": {
                "from": top_segment[0].from_cell,
                "to": top_segment[0].to_cell,
                "journey_count": top_segment[0].journey_count
            } if top_segment else None
        }


if __name__ == "__main__":
    from journey_tracker import JourneyTracker, CellVisit
    
    # Example: Create some sample journeys
    tracker = JourneyTracker()
    
    # Simulate multiple subscribers taking similar routes
    subscribers = ["IMSI:111", "IMSI:222", "IMSI:333", "IMSI:444"]
    cells = ["001001:0000001", "001001:0000002", "001001:0000003", "001001:0000004"]
    
    base_time = 1609459200000000000
    
    for i, subscriber in enumerate(subscribers):
        for j, cell in enumerate(cells):
            event = {
                "name": "Mobility.Handover.Notified",
                "ts": base_time + (i * 1000 + j) * 1000000000,
                "subscriber_key": subscriber,
                "attributes": {
                    "target_cell_id": cell,
                    "source_cell_id": cells[j-1] if j > 0 else None
                }
            }
            tracker.process_event(event)
    
    tracker.complete_all_journeys()
    journeys = tracker.get_completed_journeys()
    
    # Aggregate
    aggregator = JourneyAggregator()
    aggregator.aggregate_journeys(journeys)
    
    # Print results
    stats = aggregator.get_statistics()
    print("Aggregation Statistics:")
    print(f"  Total unique segments: {stats['total_unique_segments']}")
    print(f"  Total cells: {stats['total_cells']}")
    print(f"  Average journeys per segment: {stats['avg_journeys_per_segment']:.2f}")
    
    print("\nTop 5 Most Traveled Segments:")
    for i, segment in enumerate(aggregator.get_top_segments(limit=5), 1):
        print(f"  {i}. {segment.from_cell} -> {segment.to_cell}: "
              f"{segment.journey_count} journeys, {segment.subscriber_count} subscribers")

