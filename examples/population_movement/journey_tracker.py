#!/usr/bin/env python3
"""
Journey Tracker Module

Tracks individual user journeys based on S1-SEE mobility events.
Each journey represents a sequence of cell site visits for a subscriber.
"""

import json
from typing import Dict, List, Optional, Tuple
from datetime import datetime, timedelta
from dataclasses import dataclass, asdict
from collections import defaultdict


@dataclass
class CellVisit:
    """Represents a visit to a cell site."""
    cell_id: str
    timestamp: int  # Unix timestamp in nanoseconds
    event_name: str
    subscriber_key: str


@dataclass
class Journey:
    """Represents a complete journey for a subscriber."""
    subscriber_key: str
    visits: List[CellVisit]
    start_time: int
    end_time: int
    journey_id: str  # Unique identifier for this journey
    
    def get_path(self) -> List[str]:
        """Get the ordered list of cell IDs visited."""
        return [visit.cell_id for visit in self.visits]
    
    def get_segments(self) -> List[Tuple[str, str]]:
        """Get journey segments as (from_cell, to_cell) tuples."""
        segments = []
        for i in range(len(self.visits) - 1):
            segments.append((self.visits[i].cell_id, self.visits[i + 1].cell_id))
        return segments


class JourneyTracker:
    """Tracks journeys from S1-SEE events."""
    
    def __init__(self, max_journey_gap_seconds: int = 3600):
        """
        Initialize the journey tracker.
        
        Args:
            max_journey_gap_seconds: Maximum time gap between visits to consider
                                    them part of the same journey (default: 1 hour)
        """
        self.max_journey_gap = timedelta(seconds=max_journey_gap_seconds)
        self.active_journeys: Dict[str, Journey] = {}  # subscriber_key -> Journey
        self.completed_journeys: List[Journey] = []
        self.journey_counter = 0
    
    def _create_journey_id(self, subscriber_key: str) -> str:
        """Generate a unique journey ID."""
        self.journey_counter += 1
        return f"{subscriber_key}_{self.journey_counter}_{int(datetime.now().timestamp())}"
    
    def process_event(self, event: Dict):
        """
        Process an S1-SEE event and update journey tracking.
        
        Expected event format:
        {
            "name": "Mobility.Handover.Notified",
            "ts": 1234567890000000000,
            "subscriber_key": "IMSI:123456789012345",
            "attributes": {
                "source_cell_id": "001001:0000001",
                "target_cell_id": "001001:0000002"
            },
            ...
        }
        """
        event_name = event.get("name", "")
        subscriber_key = event.get("subscriber_key", "")
        timestamp = event.get("ts", 0)
        attributes = event.get("attributes", {})
        
        if not subscriber_key:
            return
        
        # Extract cell IDs from attributes
        target_cell_id = attributes.get("target_cell_id") or attributes.get("cell_id")
        source_cell_id = attributes.get("source_cell_id")
        
        if not target_cell_id:
            return
        
        # Convert timestamp from nanoseconds to datetime
        event_time = datetime.fromtimestamp(timestamp / 1e9)
        
        # Check if we have an active journey for this subscriber
        if subscriber_key in self.active_journeys:
            journey = self.active_journeys[subscriber_key]
            last_visit = journey.visits[-1]
            last_visit_time = datetime.fromtimestamp(last_visit.timestamp / 1e9)
            
            # Check if this event is within the journey time window
            time_gap = event_time - last_visit_time
            
            if time_gap > self.max_journey_gap:
                # Gap too large, start a new journey
                self._complete_journey(subscriber_key)
                self._start_new_journey(subscriber_key, target_cell_id, timestamp, event_name)
            else:
                # Continue existing journey
                # Only add if it's a different cell
                if target_cell_id != last_visit.cell_id:
                    visit = CellVisit(
                        cell_id=target_cell_id,
                        timestamp=timestamp,
                        event_name=event_name,
                        subscriber_key=subscriber_key
                    )
                    journey.visits.append(visit)
                    journey.end_time = timestamp
        else:
            # Start a new journey
            self._start_new_journey(subscriber_key, target_cell_id, timestamp, event_name)
    
    def _start_new_journey(self, subscriber_key: str, cell_id: str, timestamp: int, event_name: str):
        """Start a new journey for a subscriber."""
        journey_id = self._create_journey_id(subscriber_key)
        visit = CellVisit(
            cell_id=cell_id,
            timestamp=timestamp,
            event_name=event_name,
            subscriber_key=subscriber_key
        )
        
        journey = Journey(
            subscriber_key=subscriber_key,
            visits=[visit],
            start_time=timestamp,
            end_time=timestamp,
            journey_id=journey_id
        )
        
        self.active_journeys[subscriber_key] = journey
    
    def _complete_journey(self, subscriber_key: str):
        """Mark a journey as completed."""
        if subscriber_key in self.active_journeys:
            journey = self.active_journeys[subscriber_key]
            # Only save journeys with at least 2 visits (movement)
            if len(journey.visits) >= 2:
                self.completed_journeys.append(journey)
            del self.active_journeys[subscriber_key]
    
    def complete_all_journeys(self):
        """Complete all active journeys (call this at the end of processing)."""
        for subscriber_key in list(self.active_journeys.keys()):
            self._complete_journey(subscriber_key)
    
    def get_completed_journeys(self) -> List[Journey]:
        """Get all completed journeys."""
        return self.completed_journeys
    
    def get_journey_statistics(self) -> Dict:
        """Get statistics about tracked journeys."""
        if not self.completed_journeys:
            return {
                "total_journeys": 0,
                "total_visits": 0,
                "avg_journey_length": 0,
                "unique_subscribers": 0
            }
        
        total_visits = sum(len(j.visits) for j in self.completed_journeys)
        unique_subscribers = len(set(j.subscriber_key for j in self.completed_journeys))
        
        return {
            "total_journeys": len(self.completed_journeys),
            "total_visits": total_visits,
            "avg_journey_length": total_visits / len(self.completed_journeys),
            "unique_subscribers": unique_subscribers
        }


def load_events_from_jsonl(file_path: str) -> List[Dict]:
    """Load events from a JSONL file (S1-SEE output format)."""
    events = []
    with open(file_path, 'r') as f:
        for line in f:
            line = line.strip()
            if line:
                try:
                    events.append(json.loads(line))
                except json.JSONDecodeError as e:
                    print(f"Warning: Failed to parse line: {e}")
    return events


if __name__ == "__main__":
    # Example usage
    tracker = JourneyTracker()
    
    # Example events
    sample_events = [
        {
            "name": "Mobility.Handover.Notified",
            "ts": 1609459200000000000,  # 2021-01-01 00:00:00
            "subscriber_key": "IMSI:123456789012345",
            "attributes": {
                "target_cell_id": "001001:0000001"
            }
        },
        {
            "name": "Mobility.Handover.Notified",
            "ts": 1609459260000000000,  # 1 minute later
            "subscriber_key": "IMSI:123456789012345",
            "attributes": {
                "source_cell_id": "001001:0000001",
                "target_cell_id": "001001:0000002"
            }
        },
        {
            "name": "Mobility.Handover.Notified",
            "ts": 1609459320000000000,  # 2 minutes later
            "subscriber_key": "IMSI:123456789012345",
            "attributes": {
                "source_cell_id": "001001:0000002",
                "target_cell_id": "001001:0000003"
            }
        }
    ]
    
    for event in sample_events:
        tracker.process_event(event)
    
    tracker.complete_all_journeys()
    
    journeys = tracker.get_completed_journeys()
    print(f"Tracked {len(journeys)} journeys")
    for journey in journeys:
        print(f"Journey {journey.journey_id}: {journey.get_path()}")
        print(f"  Segments: {journey.get_segments()}")

