#!/usr/bin/env python3
"""
Map Visualization Module

Creates an interactive web-based map showing aggregated population movement.
Uses Folium (Leaflet.js) for map rendering.
"""

import json
from typing import List, Dict, Tuple, Optional
from cell_site_db import CellSiteDB
from aggregator import JourneyAggregator, SegmentFlow, CellFlow
import folium
from folium import plugins
import math


class MapVisualizer:
    """Creates interactive maps showing population movement."""
    
    def __init__(self, cell_db: CellSiteDB):
        """
        Initialize the map visualizer.
        
        Args:
            cell_db: CellSiteDB instance for looking up cell site locations
        """
        self.cell_db = cell_db
    
    def create_movement_map(self, aggregator: JourneyAggregator,
                           output_file: str = "population_movement.html",
                           center_lat: Optional[float] = None,
                           center_lon: Optional[float] = None,
                           zoom_start: int = 12) -> str:
        """
        Create an interactive map showing aggregated population movement.
        
        Args:
            aggregator: JourneyAggregator with aggregated data
            output_file: Path to output HTML file
            center_lat: Center latitude (auto-calculated if None)
            center_lon: Center longitude (auto-calculated if None)
            zoom_start: Initial zoom level
        
        Returns:
            Path to the generated HTML file
        """
        # Determine map center
        if center_lat is None or center_lon is None:
            center_lat, center_lon = self._calculate_map_center(aggregator)
        
        # Create base map
        m = folium.Map(
            location=[center_lat, center_lon],
            zoom_start=zoom_start,
            tiles='OpenStreetMap'
        )
        
        # Add cell sites
        self._add_cell_sites(m, aggregator)
        
        # Add movement segments
        self._add_movement_segments(m, aggregator)
        
        # Add heatmap layer
        self._add_heatmap(m, aggregator)
        
        # Add legend
        self._add_legend(m)
        
        # Add statistics panel
        self._add_statistics_panel(m, aggregator)
        
        # Save map
        m.save(output_file)
        return output_file
    
    def _calculate_map_center(self, aggregator: JourneyAggregator) -> Tuple[float, float]:
        """Calculate the center of all cell sites."""
        cells = aggregator.get_all_cells()
        if not cells:
            return (51.5074, -0.1278)  # Default to London
        
        latitudes = []
        longitudes = []
        
        for cell_flow in cells:
            location = self.cell_db.get_cell_location(cell_flow.cell_id)
            if location:
                latitudes.append(location[0])
                longitudes.append(location[1])
        
        if not latitudes:
            return (51.5074, -0.1278)
        
        return (sum(latitudes) / len(latitudes), sum(longitudes) / len(longitudes))
    
    def _add_cell_sites(self, m: folium.Map, aggregator: JourneyAggregator):
        """Add cell site markers to the map."""
        cells = aggregator.get_all_cells()
        
        for cell_flow in cells:
            location = self.cell_db.get_cell_location(cell_flow.cell_id)
            if not location:
                continue
            
            lat, lon = location
            cell_info = self.cell_db.get_cell_site(cell_flow.cell_id)
            
            # Calculate marker size based on traffic
            total_traffic = cell_flow.total_entries + cell_flow.total_exits
            marker_size = min(20, max(5, math.sqrt(total_traffic) * 2))
            
            # Color based on traffic volume
            if total_traffic > 50:
                color = 'red'
            elif total_traffic > 20:
                color = 'orange'
            else:
                color = 'blue'
            
            # Create popup with cell information
            popup_html = f"""
            <div style="width: 200px;">
                <h4>Cell Site: {cell_flow.cell_id}</h4>
                <p><b>Cell Name:</b> {cell_info.get('cell_name', 'N/A') if cell_info else 'N/A'}</p>
                <p><b>Total Entries:</b> {cell_flow.total_entries}</p>
                <p><b>Total Exits:</b> {cell_flow.total_exits}</p>
                <p><b>Unique Subscribers:</b> {len(cell_flow.unique_subscribers)}</p>
            </div>
            """
            
            folium.CircleMarker(
                location=[lat, lon],
                radius=marker_size,
                popup=folium.Popup(popup_html, max_width=300),
                color=color,
                fill=True,
                fillColor=color,
                fillOpacity=0.6,
                weight=2
            ).add_to(m)
    
    def _add_movement_segments(self, m: folium.Map, aggregator: JourneyAggregator):
        """Add lines showing movement between cell sites."""
        segments = aggregator.get_all_segments()
        
        # Sort by journey count for layering (most traveled on top)
        sorted_segments = sorted(segments, key=lambda x: x.journey_count)
        
        for segment_flow in sorted_segments:
            from_location = self.cell_db.get_cell_location(segment_flow.from_cell)
            to_location = self.cell_db.get_cell_location(segment_flow.to_cell)
            
            if not from_location or not to_location:
                continue
            
            # Calculate line width and opacity based on journey count
            max_journeys = max(s.journey_count for s in segments) if segments else 1
            width = max(1, min(10, (segment_flow.journey_count / max_journeys) * 10))
            opacity = min(1.0, 0.3 + (segment_flow.journey_count / max_journeys) * 0.7)
            
            # Color based on journey count
            if segment_flow.journey_count > max_journeys * 0.7:
                color = 'red'
            elif segment_flow.journey_count > max_journeys * 0.4:
                color = 'orange'
            else:
                color = 'blue'
            
            # Create popup
            popup_html = f"""
            <div style="width: 200px;">
                <h4>Movement Segment</h4>
                <p><b>From:</b> {segment_flow.from_cell}</p>
                <p><b>To:</b> {segment_flow.to_cell}</p>
                <p><b>Journeys:</b> {segment_flow.journey_count}</p>
                <p><b>Subscribers:</b> {segment_flow.subscriber_count}</p>
            </div>
            """
            
            folium.PolyLine(
                locations=[[from_location[0], from_location[1]],
                          [to_location[0], to_location[1]]],
                weight=width,
                color=color,
                opacity=opacity,
                popup=folium.Popup(popup_html, max_width=300),
                tooltip=f"{segment_flow.journey_count} journeys"
            ).add_to(m)
    
    def _add_heatmap(self, m: folium.Map, aggregator: JourneyAggregator):
        """Add a heatmap layer showing cell site activity."""
        heat_data = []
        
        cells = aggregator.get_all_cells()
        for cell_flow in cells:
            location = self.cell_db.get_cell_location(cell_flow.cell_id)
            if location:
                # Weight based on total traffic
                weight = cell_flow.total_entries + cell_flow.total_exits
                heat_data.append([location[0], location[1], weight])
        
        if heat_data:
            plugins.HeatMap(
                heat_data,
                min_opacity=0.2,
                max_zoom=18,
                radius=25,
                blur=15,
                gradient={0.2: 'blue', 0.4: 'lime', 0.6: 'orange', 1: 'red'}
            ).add_to(m)
    
    def _add_legend(self, m: folium.Map):
        """Add a legend to the map."""
        legend_html = '''
        <div style="position: fixed; 
                    bottom: 50px; left: 50px; width: 200px; height: 150px; 
                    background-color: white; border:2px solid grey; z-index:9999; 
                    font-size:14px; padding: 10px">
        <h4>Legend</h4>
        <p><i class="fa fa-circle" style="color:red"></i> High Traffic (>50)</p>
        <p><i class="fa fa-circle" style="color:orange"></i> Medium Traffic (20-50)</p>
        <p><i class="fa fa-circle" style="color:blue"></i> Low Traffic (<20)</p>
        <p><hr></p>
        <p><b>Lines:</b> Movement segments</p>
        <p>Thickness = Journey count</p>
        </div>
        '''
        m.get_root().html.add_child(folium.Element(legend_html))
    
    def _add_statistics_panel(self, m: folium.Map, aggregator: JourneyAggregator):
        """Add a statistics panel to the map."""
        stats = aggregator.get_statistics()
        
        stats_html = f'''
        <div style="position: fixed; 
                    top: 50px; right: 50px; width: 250px; height: auto; 
                    background-color: white; border:2px solid grey; z-index:9999; 
                    font-size:12px; padding: 10px">
        <h4>Statistics</h4>
        <p><b>Unique Segments:</b> {stats['total_unique_segments']}</p>
        <p><b>Cell Sites:</b> {stats['total_cells']}</p>
        <p><b>Total Journey Segments:</b> {stats['total_journey_segments']}</p>
        <p><b>Avg Journeys/Segment:</b> {stats['avg_journeys_per_segment']:.1f}</p>
        '''
        
        if stats['most_traveled_segment']:
            stats_html += f'''
        <p><hr></p>
        <p><b>Most Traveled:</b></p>
        <p>{stats['most_traveled_segment']['from']} → {stats['most_traveled_segment']['to']}</p>
        <p>{stats['most_traveled_segment']['journey_count']} journeys</p>
        '''
        
        stats_html += '</div>'
        m.get_root().html.add_child(folium.Element(stats_html))


if __name__ == "__main__":
    # Example usage
    from journey_tracker import JourneyTracker
    from aggregator import JourneyAggregator
    
    # Initialize database
    db = CellSiteDB()
    
    # Create sample journeys (same as aggregator example)
    tracker = JourneyTracker()
    # ... (add sample events)
    
    # Aggregate
    aggregator = JourneyAggregator()
    # aggregator.aggregate_journeys(journeys)
    
    # Visualize
    visualizer = MapVisualizer(db)
    # visualizer.create_movement_map(aggregator)

