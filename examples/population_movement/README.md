# Population Movement Analysis Example

This example application demonstrates how to use S1-SEE events to generate aggregated population movement visualizations on an interactive map.

## Overview

The application processes mobility events from S1-SEE and:
1. **Tracks individual journeys** - Sequences of cell site visits for each subscriber
2. **Aggregates shared segments** - Identifies common movement patterns across multiple subscribers
3. **Visualizes on a map** - Creates an interactive web-based map showing:
   - Cell site locations
   - Movement segments between cells
   - Traffic volume (heatmap and line thickness)
   - Statistics and analytics

## Architecture

```
S1-SEE Events (JSONL)
    ↓
Journey Tracker
    ↓
Individual Journeys
    ↓
Aggregator
    ↓
Shared Segments & Statistics
    ↓
Map Visualizer
    ↓
Interactive HTML Map
```

## Components

### 1. Cell Site Database (`cell_site_db.py`)
- SQLite database for storing cell site locations
- Maps ECGI (E-UTRAN Cell Global Identifier) to geographic coordinates
- Supports adding, querying, and searching cell sites

### 2. Journey Tracker (`journey_tracker.py`)
- Processes S1-SEE mobility events
- Tracks individual subscriber journeys
- Groups consecutive cell visits into journeys
- Handles time gaps between visits

### 3. Aggregator (`aggregator.py`)
- Aggregates multiple journeys to find shared segments
- Calculates traffic statistics per segment and cell
- Identifies most traveled routes

### 4. Map Visualizer (`map_visualizer.py`)
- Creates interactive Leaflet.js maps using Folium
- Visualizes cell sites, movement segments, and heatmaps
- Shows traffic statistics and analytics

### 5. Main Application (`population_movement_app.py`)
- Orchestrates the entire pipeline
- Command-line interface for processing events
- Generates the final visualization

## Installation

### Prerequisites

- Python 3.8 or higher
- S1-SEE events in JSONL format

### Install Dependencies

```bash
pip install -r requirements.txt
```

## Usage

### 1. Initialize Cell Site Database

First, you need to populate the cell site database with locations. You can either:

**Option A: Use sample data**
```bash
python cell_site_db.py
```

**Option B: Add your own cell sites programmatically**
```python
from cell_site_db import CellSiteDB

db = CellSiteDB("cell_sites.db")
db.add_cell_site(
    ecgi="001001:0000001",
    plmn_identity="001001",
    cell_id="0000001",
    latitude=51.5074,
    longitude=-0.1278,
    enb_id="ENB001",
    cell_name="Central Tower 1",
    coverage_radius_meters=2000
)
db.close()
```

### 2. Process S1-SEE Events

Run the main application:

```bash
python population_movement_app.py \
    --events /path/to/events.jsonl \
    --output population_movement.html \
    --db cell_sites.db \
    --init-db
```

**Arguments:**
- `--events`: Path to JSONL file containing S1-SEE events (required)
- `--output`: Output HTML file path (default: `population_movement.html`)
- `--db`: Path to cell site database (default: `cell_sites.db`)
- `--init-db`: Initialize database with sample cell sites
- `--max-journey-gap`: Maximum time gap (seconds) between visits to consider same journey (default: 3600)
- `--center-lat`: Map center latitude (auto-calculated if not specified)
- `--center-lon`: Map center longitude (auto-calculated if not specified)
- `--zoom`: Initial map zoom level (default: 12)

### 3. View the Visualization

Open the generated HTML file in a web browser:

```bash
open population_movement.html
# or
firefox population_movement.html
# or
chrome population_movement.html
```

## Event Format

The application expects S1-SEE events in JSONL format with the following structure:

```json
{
  "name": "Mobility.Handover.Notified",
  "ts": 1609459200000000000,
  "subscriber_key": "IMSI:123456789012345",
  "attributes": {
    "source_cell_id": "001001:0000001",
    "target_cell_id": "001001:0000002"
  },
  "confidence": 1.0,
  "ruleset_id": "mobility",
  "ruleset_version": "1.0"
}
```

**Required fields:**
- `name`: Event name (e.g., "Mobility.Handover.Notified")
- `ts`: Timestamp in nanoseconds
- `subscriber_key`: Subscriber identifier (e.g., "IMSI:...")
- `attributes`: Dictionary containing at least one of:
  - `target_cell_id`: Target cell ECGI
  - `source_cell_id`: Source cell ECGI
  - `cell_id`: Current cell ECGI

## Example Workflow

1. **Generate S1-SEE events** (using S1-SEE processor):
   ```bash
   cd /path/to/S1-SEE/build
   ./s1see_processor spool_data config/rulesets/mobility.yaml events.jsonl true
   ```

2. **Initialize cell site database**:
   ```bash
   cd examples/population_movement
   python cell_site_db.py
   ```

3. **Process events and generate map**:
   ```bash
   python population_movement_app.py \
       --events ../../build/events.jsonl \
       --output population_movement.html \
       --init-db
   ```

4. **View the map**:
   ```bash
   open population_movement.html
   ```

## Map Features

The generated map includes:

- **Cell Site Markers**: Colored circles showing cell locations
  - Size indicates traffic volume
  - Color: Red (high), Orange (medium), Blue (low)
  - Click to see detailed statistics

- **Movement Segments**: Lines connecting cell sites
  - Thickness indicates number of journeys
  - Color indicates traffic volume
  - Hover/click for segment details

- **Heatmap Layer**: Shows overall activity density

- **Statistics Panel**: Real-time statistics about:
  - Total unique segments
  - Number of cell sites
  - Most traveled routes
  - Average journeys per segment

- **Legend**: Explains map symbols and colors

## Customization

### Adding Custom Cell Sites

You can add cell sites to the database using the Python API:

```python
from cell_site_db import CellSiteDB

db = CellSiteDB("cell_sites.db")

# Add a cell site
db.add_cell_site(
    ecgi="001001:0000001",  # Must match ECGI in events
    plmn_identity="001001",
    cell_id="0000001",
    latitude=51.5074,        # Your coordinates
    longitude=-0.1278,
    enb_id="ENB001",
    cell_name="My Cell Site",
    coverage_radius_meters=2000
)

db.close()
```

### Adjusting Journey Tracking

Modify the `max_journey_gap_seconds` parameter to control how visits are grouped into journeys:

- **Smaller values** (e.g., 600 seconds = 10 minutes): More granular journeys
- **Larger values** (e.g., 7200 seconds = 2 hours): Longer, continuous journeys

### Customizing Map Appearance

Edit `map_visualizer.py` to customize:
- Map tiles (OpenStreetMap, satellite, etc.)
- Marker colors and sizes
- Line styles and colors
- Heatmap parameters

## Troubleshooting

### No journeys tracked

- Check that events contain `target_cell_id` or `cell_id` in attributes
- Verify that cell IDs in events match ECGI values in the database
- Ensure events have valid `subscriber_key` and `ts` fields

### Cell sites not appearing on map

- Verify cell sites are in the database: `python cell_site_db.py` (check output)
- Ensure ECGI format matches between events and database
- Check that coordinates are valid (latitude: -90 to 90, longitude: -180 to 180)

### Map shows wrong location

- Use `--center-lat` and `--center-lon` to manually set map center
- Adjust `--zoom` level for better view
- Check that cell site coordinates are correct

## Future Enhancements

Potential improvements:
- Real-time event processing (streaming)
- Time-based animation of movement
- Route prediction and analysis
- Integration with external mapping services
- Export to GeoJSON/KML formats
- Statistical analysis and reporting
- Multi-timeframe comparisons

## License

This example application is part of the S1-SEE project and follows the same license (Apache License 2.0).

