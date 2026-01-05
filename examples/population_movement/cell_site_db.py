#!/usr/bin/env python3
"""
Cell Site Database Module

Manages a SQLite database of cell site locations (eNodeB cells).
Each cell site has an ECGI (E-UTRAN Cell Global Identifier) and geographic coordinates.
"""

import sqlite3
import json
from typing import Optional, Tuple, List, Dict
from datetime import datetime


class CellSiteDB:
    """Database for storing and querying cell site locations."""
    
    def __init__(self, db_path: str = "cell_sites.db"):
        """Initialize the database connection and create tables if needed."""
        self.db_path = db_path
        self.conn = sqlite3.connect(db_path)
        self.conn.row_factory = sqlite3.Row
        self._create_tables()
    
    def _create_tables(self):
        """Create the cell_sites table if it doesn't exist."""
        cursor = self.conn.cursor()
        cursor.execute("""
            CREATE TABLE IF NOT EXISTS cell_sites (
                ecgi TEXT PRIMARY KEY,
                plmn_identity TEXT NOT NULL,
                cell_id TEXT NOT NULL,
                latitude REAL NOT NULL,
                longitude REAL NOT NULL,
                enb_id TEXT,
                cell_name TEXT,
                coverage_radius_meters INTEGER,
                created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
            )
        """)
        
        # Create index for faster lookups
        cursor.execute("""
            CREATE INDEX IF NOT EXISTS idx_ecgi ON cell_sites(ecgi)
        """)
        
        cursor.execute("""
            CREATE INDEX IF NOT EXISTS idx_location ON cell_sites(latitude, longitude)
        """)
        
        self.conn.commit()
    
    def add_cell_site(self, ecgi: str, plmn_identity: str, cell_id: str,
                     latitude: float, longitude: float,
                     enb_id: Optional[str] = None,
                     cell_name: Optional[str] = None,
                     coverage_radius_meters: Optional[int] = None):
        """Add or update a cell site."""
        cursor = self.conn.cursor()
        cursor.execute("""
            INSERT OR REPLACE INTO cell_sites 
            (ecgi, plmn_identity, cell_id, latitude, longitude, enb_id, cell_name, coverage_radius_meters, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (ecgi, plmn_identity, cell_id, latitude, longitude, enb_id, cell_name, coverage_radius_meters, datetime.now()))
        self.conn.commit()
    
    def get_cell_site(self, ecgi: str) -> Optional[Dict]:
        """Get cell site information by ECGI."""
        cursor = self.conn.cursor()
        cursor.execute("SELECT * FROM cell_sites WHERE ecgi = ?", (ecgi,))
        row = cursor.fetchone()
        if row:
            return dict(row)
        return None
    
    def get_cell_location(self, ecgi: str) -> Optional[Tuple[float, float]]:
        """Get latitude and longitude for a cell site."""
        cell = self.get_cell_site(ecgi)
        if cell:
            return (cell['latitude'], cell['longitude'])
        return None
    
    def get_all_cell_sites(self) -> List[Dict]:
        """Get all cell sites."""
        cursor = self.conn.cursor()
        cursor.execute("SELECT * FROM cell_sites")
        return [dict(row) for row in cursor.fetchall()]
    
    def search_cell_sites_by_location(self, latitude: float, longitude: float,
                                     radius_km: float = 10.0) -> List[Dict]:
        """Find cell sites within a radius of a location."""
        # Simple bounding box search (for production, use proper geospatial queries)
        cursor = self.conn.cursor()
        # Approximate: 1 degree latitude ≈ 111 km
        lat_delta = radius_km / 111.0
        # Longitude delta depends on latitude
        lon_delta = radius_km / (111.0 * abs(latitude / 90.0) if latitude != 0 else 111.0)
        
        cursor.execute("""
            SELECT * FROM cell_sites
            WHERE latitude BETWEEN ? AND ?
            AND longitude BETWEEN ? AND ?
        """, (latitude - lat_delta, latitude + lat_delta,
              longitude - lon_delta, longitude + lon_delta))
        
        results = [dict(row) for row in cursor.fetchall()]
        # Filter by actual distance (Haversine formula would be better for production)
        return results
    
    def ecgi_to_string(self, ecgi_bytes: bytes) -> str:
        """Convert ECGI bytes to a string representation."""
        if not ecgi_bytes:
            return ""
        # ECGI is typically: PLMN (3 bytes) + Cell ID (28 bits = 3.5 bytes, stored as 4 bytes)
        if len(ecgi_bytes) >= 3:
            plmn = ecgi_bytes[:3].hex()
            cell_id = ecgi_bytes[3:].hex() if len(ecgi_bytes) > 3 else ""
            return f"{plmn}:{cell_id}"
        return ecgi_bytes.hex()
    
    def close(self):
        """Close the database connection."""
        self.conn.close()
    
    def __enter__(self):
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


def init_sample_cell_sites(db: CellSiteDB):
    """Initialize database with sample cell sites (for demonstration)."""
    # Sample cell sites in a metropolitan area (example coordinates)
    sample_sites = [
        {
            "ecgi": "001001:0000001",
            "plmn_identity": "001001",
            "cell_id": "0000001",
            "latitude": 51.5074,  # London coordinates
            "longitude": -0.1278,
            "enb_id": "ENB001",
            "cell_name": "Central London Tower 1",
            "coverage_radius_meters": 2000
        },
        {
            "ecgi": "001001:0000002",
            "plmn_identity": "001001",
            "cell_id": "0000002",
            "latitude": 51.5155,
            "longitude": -0.0922,
            "enb_id": "ENB002",
            "cell_name": "East London Tower 1",
            "coverage_radius_meters": 2000
        },
        {
            "ecgi": "001001:0000003",
            "plmn_identity": "001001",
            "cell_id": "0000003",
            "latitude": 51.5007,
            "longitude": -0.1246,
            "enb_id": "ENB003",
            "cell_name": "Westminster Tower 1",
            "coverage_radius_meters": 2000
        },
        {
            "ecgi": "001001:0000004",
            "plmn_identity": "001001",
            "cell_id": "0000004",
            "latitude": 51.4816,
            "longitude": -0.0481,
            "enb_id": "ENB004",
            "cell_name": "Greenwich Tower 1",
            "coverage_radius_meters": 2000
        },
        {
            "ecgi": "001001:0000005",
            "plmn_identity": "001001",
            "cell_id": "0000005",
            "latitude": 51.5234,
            "longitude": -0.1466,
            "enb_id": "ENB005",
            "cell_name": "Paddington Tower 1",
            "coverage_radius_meters": 2000
        },
    ]
    
    for site in sample_sites:
        db.add_cell_site(**site)
    
    print(f"Initialized {len(sample_sites)} sample cell sites")


if __name__ == "__main__":
    # Initialize database with sample data
    with CellSiteDB() as db:
        init_sample_cell_sites(db)
        print("Cell site database initialized successfully")

