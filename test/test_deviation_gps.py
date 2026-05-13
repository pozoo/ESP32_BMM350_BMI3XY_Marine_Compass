#!/usr/bin/env python3
"""
GPS NMEA0183 Test Data Generator for Deviation Calibration

Connects to the device and sends GPS data cycling through all 12 deviation table
headings (0°, 30°, 60°, ..., 330°) for 1 minute each with exact COG values.
"""

import socket
import time
import argparse
from datetime import datetime

# Configuration
HOST = '192.168.183.46'
PORT = 2000
DIRECTIONS = [0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330]
DURATION_PER_DIRECTION = 90  # seconds
INTERVAL = 1.0  # seconds between updates
SOG = 3.5  # knots (fixed value for testing)

# Fixed position (Munich, Germany for testing)
LAT = 48.137154  # degrees
LON = 11.576124  # degrees


def calculate_checksum(sentence):
    """Calculate NMEA checksum (XOR of all characters between $ and *)"""
    checksum = 0
    for char in sentence:
        checksum ^= ord(char)
    return f"{checksum:02X}"


def format_lat(lat):
    """Convert decimal latitude to NMEA format (DDMM.MMMM)"""
    degrees = int(abs(lat))
    minutes = (abs(lat) - degrees) * 60
    hemisphere = 'N' if lat >= 0 else 'S'
    return f"{degrees:02d}{minutes:07.4f}", hemisphere


def format_lon(lon):
    """Convert decimal longitude to NMEA format (DDDMM.MMMM)"""
    degrees = int(abs(lon))
    minutes = (abs(lon) - degrees) * 60
    hemisphere = 'E' if lon >= 0 else 'W'
    return f"{degrees:03d}{minutes:07.4f}", hemisphere


def generate_rmc(cog, sog):
    """Generate NMEA RMC sentence (Recommended Minimum)"""
    now = datetime.utcnow()
    time_str = now.strftime("%H%M%S.00")
    date_str = now.strftime("%d%m%y")
    
    lat_str, lat_hem = format_lat(LAT)
    lon_str, lon_hem = format_lon(LON)
    
    # RMC format: $GPRMC,time,status,lat,N/S,lon,E/W,speed,course,date,mag_var,E/W,mode*checksum
    sentence = (f"GPRMC,{time_str},A,{lat_str},{lat_hem},{lon_str},{lon_hem},"
                f"{sog:.1f},{cog:.1f},{date_str},,A")
    
    checksum = calculate_checksum(sentence)
    return f"${sentence}*{checksum}\r\n"


def generate_vtg(cog, sog):
    """Generate NMEA VTG sentence (Track Made Good and Ground Speed)"""
    # VTG format: $GPVTG,true_track,T,mag_track,M,speed_knots,N,speed_kmh,K,mode*checksum
    speed_kmh = sog * 1.852  # Convert knots to km/h
    
    sentence = f"GPVTG,{cog:.1f},T,{cog:.1f},M,{sog:.1f},N,{speed_kmh:.1f},K,A"
    
    checksum = calculate_checksum(sentence)
    return f"${sentence}*{checksum}\r\n"


def generate_gga():
    """Generate NMEA GGA sentence (Global Positioning System Fix Data)"""
    now = datetime.utcnow()
    time_str = now.strftime("%H%M%S.00")
    
    lat_str, lat_hem = format_lat(LAT)
    lon_str, lon_hem = format_lon(LON)
    
    # GGA format: $GPGGA,time,lat,N/S,lon,E/W,quality,satellites,hdop,altitude,M,geoid,M,dgps_age,dgps_id*checksum
    # Quality: 0=invalid, 1=GPS fix, 2=DGPS fix
    # Note: Empty fields at end are valid (no DGPS data)
    sentence = f"GPGGA,{time_str},{lat_str},{lat_hem},{lon_str},{lon_hem},1,08,1.0,10.0,M,0.0,M,,"
    
    checksum = calculate_checksum(sentence)
    return f"${sentence}*{checksum}\r\n"


def generate_gsa():
    """Generate NMEA GSA sentence (GPS DOP and Active Satellites)"""
    # GSA format: $GPGSA,mode,fix_type,sat1,...,sat12,PDOP,HDOP,VDOP*checksum
    # mode: A=automatic, M=manual
    # fix_type: 1=no fix, 2=2D, 3=3D
    sentence = "GPGSA,A,3,01,02,03,04,05,06,07,08,,,,,2.0,1.0,1.7"
    
    checksum = calculate_checksum(sentence)
    return f"${sentence}*{checksum}\r\n"


def send_gps_data(sock, direction_idx):
    """Send one set of GPS data for the current direction"""
    # Use exact COG value from table (no variation/noise)
    cog = DIRECTIONS[direction_idx]
    sog = SOG
    
    # Drain any incoming data from the server to prevent receive buffer from filling
    # The ESP32 broadcasts NMEA compass data to all clients
    try:
        sock.setblocking(False)
        while True:
            data = sock.recv(4096)
            if len(data) == 0:
                # Connection closed
                print(f"\n\nERROR: Connection closed by server")
                return False
            # Keep draining until no more data
    except BlockingIOError:
        # No more data available, buffer drained
        pass
    except (ConnectionResetError, ConnectionAbortedError, BrokenPipeError) as e:
        print(f"\n\nERROR: Connection lost: {e}")
        return False
    finally:
        sock.setblocking(True)
    
    # Generate and send NMEA sentences
    gga = generate_gga()
    gsa = generate_gsa()
    rmc = generate_rmc(cog, sog)
    vtg = generate_vtg(cog, sog)
    
    try:
        sock.sendall(gga.encode('ascii'))
        sock.sendall(gsa.encode('ascii'))
        sock.sendall(rmc.encode('ascii'))
        sock.sendall(vtg.encode('ascii'))
        
        print(f"[{cog:3d}°] Sent: COG={cog:6.2f}° SOG={sog:.2f}kts", end='')
        print(f" | GGA: {gga.strip()}")
        return True
    except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError) as e:
        print(f"\n\nERROR: Connection lost: {e}")
        print("The server has closed the connection or is unreachable.")
        return False
    except socket.timeout as e:
        print(f"\n\nERROR: Socket timeout: {e}")
        return False
    except OSError as e:
        print(f"\n\nERROR: Socket error: {e}")
        return False
    except Exception as e:
        print(f"\n\nERROR: Unexpected error sending data: {e}")
        return False


def main():
    parser = argparse.ArgumentParser(description='GPS NMEA0183 Test Data Generator for Deviation Calibration')
    parser.add_argument('--start', type=int, default=0, choices=DIRECTIONS,
                        help=f'Starting direction (degrees). Must be one of {DIRECTIONS}. Default: 0')
    parser.add_argument('--host', type=str, default=HOST,
                        help=f'Target host IP address. Default: {HOST}')
    parser.add_argument('--port', type=int, default=PORT,
                        help=f'Target port. Default: {PORT}')
    args = parser.parse_args()
    
    # Find starting index
    start_idx = DIRECTIONS.index(args.start)
    # Create rotated list starting from the specified direction
    directions_to_test = DIRECTIONS[start_idx:] + DIRECTIONS[:start_idx]
    
    print("=" * 70)
    print("GPS NMEA0183 Test Data Generator for Deviation Calibration")
    print("=" * 70)
    print(f"Target: {args.host}:{args.port}")
    print(f"Starting direction: {args.start}°")
    print(f"Directions order: {directions_to_test}")
    print(f"Duration per direction: {DURATION_PER_DIRECTION} seconds")
    print(f"Update interval: {INTERVAL} seconds")
    print(f"COG: Exact table values (no noise)")
    print(f"SOG: {SOG} knots (fixed)")
    print("=" * 70)
    print()
    print("IMPORTANT: Make sure the device NMEA input is configured to TCP Server")
    print("           and is listening on port 2000.")
    print()
    
    try:
        # Connect to device
        print(f"Connecting to {args.host}:{args.port}...")
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(5.0)  # 5 second connection timeout
        sock.connect((args.host, args.port))
        sock.settimeout(10.0)  # 10 second timeout for send/recv operations
        print("Connected successfully!")
        print()
        
        # Send a test burst to verify connection
        print("Sending test data...")
        test_gga = generate_gga()
        test_rmc = generate_rmc(args.start, SOG)
        sock.sendall(test_gga.encode('ascii'))
        sock.sendall(test_rmc.encode('ascii'))
        print(f"  Test GGA: {test_gga.strip()}")
        print(f"  Test RMC: {test_rmc.strip()}")
        print("Test data sent. Check the NMEA tab for GPS status.")
        print()
        
        input("Press Enter to start the calibration cycle (or Ctrl+C to abort)...")
        print()
        
        # Cycle through all directions starting from specified direction
        for seq_idx, direction in enumerate(directions_to_test):
            direction_idx = DIRECTIONS.index(direction)
            print(f"\n{'=' * 70}")
            print(f"Starting direction {direction}° ({seq_idx + 1}/{len(directions_to_test)})")
            print(f"{'=' * 70}")
            
            end_time = time.time() + DURATION_PER_DIRECTION
            updates_sent = 0
            
            while time.time() < end_time:
                if not send_gps_data(sock, direction_idx):
                    # Send failed, connection likely lost
                    print(f"\nAborting: Failed to send data. Sent {updates_sent} updates for direction {direction}°.")
                    raise ConnectionError("Lost connection to server")
                
                updates_sent += 1
                
                # Sleep until next update
                time.sleep(INTERVAL)
            
            print(f"Direction {direction}° complete. Sent {updates_sent} updates.")
        
        print("\n" + "=" * 70)
        print("All directions complete!")
        print("=" * 70)
        
    except KeyboardInterrupt:
        print("\n\nInterrupted by user.")
    except ConnectionError as e:
        print(f"\nConnection Error: {e}")
        print("Please check that the server is running and reachable.")
    except socket.timeout as e:
        print(f"\nTimeout Error: {e}")
        print("Connection timed out. Check network connectivity.")
    except socket.error as e:
        print(f"\nSocket Error: {e}")
        print("Network communication failed.")
    except Exception as e:
        print(f"\nUnexpected Error: {e}")
        import traceback
        traceback.print_exc()
    finally:
        try:
            sock.close()
            print("Connection closed.")
        except:
            pass


if __name__ == "__main__":
    main()
