#!/bin/bash
# Quick Start Script for Population Movement Analysis

set -e

echo "=== Population Movement Analysis - Quick Start ==="
echo ""

# Check if Python 3 is available
if ! command -v python3 &> /dev/null; then
    echo "Error: python3 not found. Please install Python 3.8 or higher."
    exit 1
fi

# Check if pip is available
if ! command -v pip3 &> /dev/null && ! command -v pip &> /dev/null; then
    echo "Error: pip not found. Please install pip."
    exit 1
fi

# Install dependencies
echo "1. Installing dependencies..."
pip3 install -q -r requirements.txt || pip install -q -r requirements.txt
echo "   ✓ Dependencies installed"
echo ""

# Initialize cell site database
echo "2. Initializing cell site database..."
python3 cell_site_db.py
echo "   ✓ Database initialized"
echo ""

# Generate sample events
echo "3. Generating sample events..."
python3 generate_sample_events.py --output sample_events.jsonl --subscribers 50 --events-per-subscriber 10
echo "   ✓ Sample events generated"
echo ""

# Process events and create visualization
echo "4. Processing events and creating visualization..."
python3 population_movement_app.py \
    --events sample_events.jsonl \
    --output population_movement.html \
    --init-db
echo ""

echo "=== Complete! ==="
echo ""
echo "To view the visualization, open:"
echo "  population_movement.html"
echo ""
echo "Or run:"
echo "  open population_movement.html"
echo ""

