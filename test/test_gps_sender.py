#!/usr/bin/env python3
"""
Simple NMEA GPS sentence sender for testing
Sends GPRMC and GPGGA sentences to simulate a GPS receiver
Supports both TCP and UDP modes
"""

import socket
import time
import sys
from datetime import datetime

HOST = "192.168.183.46"
PORT = 2000
MODE = "UDP"  # Change to "TCP" for TCP connection

def calculate_checksum(sentence):
    """Calculate NMEA checksum (XOR of all characters between $ and *)"""
    checksum = 0
    for char in sentence[1:]:  # Skip the $
        checksum ^= ord(char)
    return f"{checksum:02X}"

def create_gprmc(lat=48.137154, lon=11.576124, sog=5.5, cog=245.0):
    """Create GPRMC sentence with position, speed, and course"""
    now = datetime.utcnow()
    time_str = now.strftime("%H%M%S.00")
    date_str = now.strftime("%d%m%y")
    
    # Convert lat/lon to NMEA format (DDMM.MMMM)
    lat_deg = int(abs(lat))
    lat_min = (abs(lat) - lat_deg) * 60
    lat_str = f"{lat_deg:02d}{lat_min:07.4f}"
    lat_dir = "N" if lat >= 0 else "S"
    
    lon_deg = int(abs(lon))
    lon_min = (abs(lon) - lon_deg) * 60
    lon_str = f"{lon_deg:03d}{lon_min:07.4f}"
    lon_dir = "E" if lon >= 0 else "W"
    
    sentence = f"$GPRMC,{time_str},A,{lat_str},{lat_dir},{lon_str},{lon_dir},{sog:.1f},{cog:.1f},{date_str},0.0,E"
    checksum = calculate_checksum(sentence)
    return f"{sentence}*{checksum}\r\n"

def create_gpgga(lat=48.137154, lon=11.576124, satellites=8):
    """Create GPGGA sentence with position and fix quality"""
    now = datetime.utcnow()
    time_str = now.strftime("%H%M%S.00")
    
    # Convert lat/lon to NMEA format
    lat_deg = int(abs(lat))
    lat_min = (abs(lat) - lat_deg) * 60
    lat_str = f"{lat_deg:02d}{lat_min:07.4f}"
    lat_dir = "N" if lat >= 0 else "S"
    
    lon_deg = int(abs(lon))
    lon_min = (abs(lon) - lon_deg) * 60
    lon_str = f"{lon_deg:03d}{lon_min:07.4f}"
    lon_dir = "E" if lon >= 0 else "W"
    
    sentence = f"$GPGGA,{time_str},{lat_str},{lat_dir},{lon_str},{lon_dir},1,{satellites:02d},1.2,450.0,M,46.0,M,,"
    checksum = calculate_checksum(sentence)
    return f"{sentence}*{checksum}\r\n"

def create_gpvtg(cog=245.0, sog=5.5):
    """Create GPVTG sentence with course and speed"""
    sog_kmh = sog * 1.852  # Convert knots to km/h
    sentence = f"$GPVTG,{cog:.1f},T,,M,{sog:.1f},N,{sog_kmh:.1f},K,A"
    checksum = calculate_checksum(sentence)
    return f"{sentence}*{checksum}\r\n"

def main():
    print(f"GPS NMEA Sender - {MODE} mode to {HOST}:{PORT}")
    print("Press Ctrl+C to stop\n")
    
    # Simulate a moving GPS track around Munich
    lat = 48.137154
    lon = 11.576124
    cog = 245.0
    sog = 5.5
    satellites = 8
    
    sock = None
    
    try:
        if MODE == "TCP":
            # Create TCP connection
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.connect((HOST, PORT))
            print(f"Connected to {HOST}:{PORT} via TCP\n")
        else:
            # Create UDP socket
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            print(f"Sending to {HOST}:{PORT} via UDP\n")
        
        counter = 0
        base_lat = 48.137154  # Munich base position
        base_lon = 11.576124
        while True:
            # Simulate movement in a small area around Munich (±0.001° = ~100m)
            lat = base_lat + 0.0001 * ((counter % 20) - 10)
            lon = base_lon + 0.0001 * ((counter % 20) - 10)
            cog = (cog + 1) % 360
            sog = 5.0 + (counter % 5)
            
            # Create sentences
            rmc = create_gprmc(lat, lon, sog, cog)
            gga = create_gpgga(lat, lon, satellites)
            vtg = create_gpvtg(cog, sog)
            
            # Send sentences
            if MODE == "TCP":
                sock.sendall(rmc.encode())
                sock.sendall(gga.encode())
                sock.sendall(vtg.encode())
            else:
                # UDP - combine into one packet
                data = rmc + gga + vtg
                sock.sendto(data.encode(), (HOST, PORT))
            
            # Print what was sent
            print(f"[{counter}] Sent:")
            print(f"  {rmc.strip()}")
            print(f"  {gga.strip()}")
            print(f"  {vtg.strip()}")
            print(f"  COG: {cog:.1f}°, SOG: {sog:.1f} kts, Pos: {lat:.6f}, {lon:.6f}\n")
            
            counter += 1
            time.sleep(1)
            
    except KeyboardInterrupt:
        print("\nStopped by user")
    except Exception as e:
        print(f"Error: {e}")
        print(f"\nTroubleshooting:")
        print(f"  1. Check ESP32 is powered on and connected")
        print(f"  2. Verify IP address: {HOST}")
        print(f"  3. Enable {MODE} input in ESP32 web UI (NMEA tab)")
        print(f"  4. For TCP: Enable TCP Server on ESP32")
        print(f"  5. Try changing MODE to 'UDP' in this script")
    finally:
        if sock:
            sock.close()
        print("Connection closed")

if __name__ == "__main__":
    main()
