# How to Use Generated Events in the Population Movement App

This guide shows you how to use the generated sample events (or real S1-SEE events) with the population movement analysis application.

## Quick Start (Complete Workflow)

### Step 1: Navigate to the Example Directory

```bash
cd examples/population_movement
```

### Step 2: Install Dependencies

```bash
pip3 install -r requirements.txt
# or
pip install -r requirements.txt
```

### Step 3: Generate Sample Events (Optional)

If you want to generate 25,000 journeys for testing:

```bash
python3 generate_sample_events.py --journeys 25000 --output sample_events_25k.jsonl
```

This creates a file with ~87,500 events representing 25,000 journeys.

**Or use real S1-SEE events:**
If you have events from the S1-SEE processor, use that file instead:
```bash
# Events from S1-SEE processor
--events ../../build/events.jsonl
```

### Step 4: Initialize Cell Site Database

The database needs to know where each cell site is located on the map:

```bash
python3 cell_site_db.py
```

This creates `cell_sites.db` with sample cell site locations (London area coordinates).

**Important:** The ECGI values in your events must match the ECGI values in the database. The sample generator uses:
- `001001:0000001`
- `001001:0000002`
- `001001:0000003`
- `001001:0000004`
- `001001:0000005`

If your events use different ECGI values, you'll need to add those cell sites to the database (see "Adding Custom Cell Sites" below).

### Step 5: Process Events and Generate Map

Run the main application:

```bash
python3 population_movement_app.py \
    --events sample_events_25k.jsonl \
    --output population_movement.html \
    --init-db
```

**Full command with all options:**
```bash
python3 population_movement_app.py \
    --events sample_events_25k.jsonl \
    --output population_movement.html \
    --db cell_sites.db \
    --init-db \
    --max-journey-gap 3600 \
    --zoom 12
```

**What happens:**
1. Loads events from the JSONL file
2. Tracks individual journeys for each subscriber
3. Aggregates journeys to find shared segments
4. Creates an interactive HTML map visualization
5. Displays statistics about the movement patterns

### Step 6: View the Visualization

Open the generated HTML file in your web browser:

```bash
# macOS
open population_movement.html

# Linux
xdg-open population_movement.html
# or
firefox population_movement.html

# Windows
start population_movement.html
```

The map will show:
- **Cell site markers** (colored circles) - size and color indicate traffic volume
- **Movement segments** (lines) - thickness shows number of journeys
- **Heatmap** - overall activity density
- **Statistics panel** - movement analytics

## Complete Example Workflow

Here's a complete example from start to finish:

```bash
# 1. Navigate to the example directory
cd examples/population_movement

# 2. Install dependencies
pip3 install -r requirements.txt

# 3. Generate 25,000 journeys worth of sample events
python3 generate_sample_events.py --journeys 25000 --output sample_events_25k.jsonl

# 4. Initialize cell site database
python3 cell_site_db.py

# 5. Process events and create visualization
python3 population_movement_app.py \
    --events sample_events_25k.jsonl \
    --output population_movement.html \
    --init-db

# 6. View the map
open population_movement.html
```

## Using Real S1-SEE Events

If you have events from the actual S1-SEE processor:

### 1. Generate Events from S1-SEE

```bash
cd ../../build
./s1see_processor spool_data config/rulesets/mobility.yaml events.jsonl true
```

### 2. Process with Population Movement App

```bash
cd ../../examples/population_movement
python3 population_movement_app.py \
    --events ../../build/events.jsonl \
    --output real_movement.html \
    --init-db
```

**Note:** Make sure the ECGI values in your real events match the cell sites in the database, or add your real cell sites to the database.

## Adding Custom Cell Sites

If your events use different ECGI values, add them to the database:

```python
from cell_site_db import CellSiteDB

db = CellSiteDB("cell_sites.db")

# Add your cell sites
db.add_cell_site(
    ecgi="001001:0000001",      # Must match ECGI in events
    plmn_identity="001001",
    cell_id="0000001",
    latitude=51.5074,           # Your coordinates
    longitude=-0.1278,
    enb_id="ENB001",
    cell_name="My Cell Site",
    coverage_radius_meters=2000
)

# Add more cell sites...
db.close()
```

Or modify `cell_site_db.py` to include your cell sites in the `init_sample_cell_sites()` function.

## Command-Line Options

### Main Application Options

```bash
python3 population_movement_app.py --help
```

**Required:**
- `--events`: Path to JSONL file with S1-SEE events

**Optional:**
- `--output`: Output HTML file (default: `population_movement.html`)
- `--db`: Cell site database path (default: `cell_sites.db`)
- `--init-db`: Initialize database with sample cell sites
- `--max-journey-gap`: Max seconds between visits for same journey (default: 3600)
- `--center-lat`: Map center latitude (auto-calculated if not set)
- `--center-lon`: Map center longitude (auto-calculated if not set)
- `--zoom`: Initial map zoom level (default: 12)

### Event Generator Options

```bash
python3 generate_sample_events.py --help
```

- `--journeys`: Target number of journeys (recommended: 25000)
- `--output`: Output JSONL file (default: `sample_events.jsonl`)
- `--subscribers`: Number of subscribers (ignored if --journeys is used)
- `--events-per-subscriber`: Events per subscriber (ignored if --journeys is used)
- `--cell-sites`: Custom cell site ECGIs (uses defaults if not specified)

## Troubleshooting

### No journeys tracked

**Problem:** App says "No journeys were tracked from the events"

**Solutions:**
- Check that events have `target_cell_id` or `cell_id` in `attributes`
- Verify ECGI format matches database (e.g., `001001:0000001`)
- Ensure events have valid `subscriber_key` and `ts` fields
- Check event format matches expected structure

### Cell sites not appearing on map

**Problem:** Map is empty or cell sites don't show

**Solutions:**
- Verify cell sites are in database: `python3 cell_site_db.py` (should print count)
- Check ECGI values in events match database ECGI values
- Ensure coordinates are valid (latitude: -90 to 90, longitude: -180 to 180)

### Map shows wrong location

**Problem:** Map is centered in wrong place

**Solutions:**
- Use `--center-lat` and `--center-lon` to manually set center
- Adjust `--zoom` level (lower = zoomed out, higher = zoomed in)
- Verify cell site coordinates are correct in database

### Large file processing is slow

**Problem:** Processing 25,000 journeys takes a long time

**Solutions:**
- This is normal - 25k journeys = ~87k events
- Processing includes: loading, tracking, aggregating, and visualization
- Typical time: 1-3 minutes depending on system
- The app will show progress messages

## Expected Output

When you run the application, you'll see output like:

```
Loading events from sample_events_25k.jsonl...
Loaded 87500 events
Processing events and tracking journeys...
  Processed 10000/87500 events...
  Processed 20000/87500 events...
  ...
Tracked 25000 completed journeys

Journey Statistics:
  Total journeys: 25000
  Total visits: 87500
  Average journey length: 3.50 visits
  Unique subscribers: 12500

Aggregating journeys...

Aggregation Statistics:
  Unique segments: 20
  Cell sites: 5
  Total journey segments: 62500
  Average journeys per segment: 3125.00

Most traveled segment:
  001001:0000001 → 001001:0000002
  7500 journeys

Top 10 Most Traveled Segments:
  1. 001001:0000001 → 001001:0000002: 7500 journeys, 3750 subscribers
  2. 001001:0000002 → 001001:0000003: 7500 journeys, 3750 subscribers
  ...

Creating map visualization...
Map saved to: population_movement.html
Open population_movement.html in a web browser to view the visualization

✓ Analysis complete!
```

## Next Steps

- Experiment with different `--max-journey-gap` values to see how it affects journey grouping
- Add more cell sites to the database for more realistic coverage
- Use real S1-SEE events from your network
- Customize the map visualization in `map_visualizer.py`
- Analyze the statistics to understand movement patterns

