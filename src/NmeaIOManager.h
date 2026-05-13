#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>
#include "platform_mutex.h"

class NMEA0183; // Forward declaration

class NmeaIOManager {
public:
    // Configuration enums (moved from NMEAConfig)
    enum SerialPort {
        SERIAL_0 = 0,
        SERIAL_1 = 1
    };

    enum UpdateRate {
        RATE_1HZ = 1000,
        RATE_2HZ = 500,
        RATE_3HZ = 333,
        RATE_5HZ = 200,
        RATE_10HZ = 100,
        RATE_20HZ = 50
    };

    enum InputChannel {
        INPUT_NONE = 0,
        INPUT_SERIAL_0 = 1,
        INPUT_SERIAL_1 = 2,
        INPUT_TCP_SERVER = 3,
        INPUT_TCP_CLIENT = 4,
        INPUT_UDP = 5
    };

    NmeaIOManager(NMEA0183* nmea);
    ~NmeaIOManager();
    
    // Configuration management (moved from NMEAConfig)
    void loadConfig();
    void saveConfig();
    void resetConfig();
    
    // Initialize all channels based on config
    void begin();
    
    // Update connections (call in loop for reconnection logic)
    void update();
    
    // Broadcast a complete NMEA sentence to all enabled channels
    void broadcast(const char* sentence);
    
    // Statistics
    uint16_t getTcpServerClientCount() const;
    bool isTcpClientConnected() const;
    bool isUdpActive() const;
    uint32_t getDroppedSentenceCount() const;
    void resetDroppedSentenceCount();
    
    // GPS data access (thread-safe)
    bool getGPSLocationValid() const;  // GPS Fix status
    bool getGPSDateValid() const;  // GPS date validity
    bool getGPSTimeValid() const;  // GPS time validity
    double getGPSCOG();  // Course Over Ground in degrees
    double getGPSSOG();  // Speed Over Ground in knots
    double getGPSLatitude();  // Latitude in decimal degrees
    double getGPSLongitude();  // Longitude in decimal degrees
    uint16_t getGPSYear();  // Year (4-digit, e.g., 2026)
    uint8_t getGPSMonth();  // Month (1-12)
    uint8_t getGPSDay();  // Day (1-31)
    uint8_t getGPSHour();  // Hour (0-23)
    uint8_t getGPSMinute();  // Minute (0-59)
    uint8_t getGPSSecond();  // Second (0-59)
    
    // Configuration change notification
    void onConfigChanged();
    
    // Configuration accessors (thread-safe, moved from NMEAConfig)
    uint16_t getUpdateIntervalMs() const;
    void setUpdateIntervalMs(uint16_t intervalMs);
    
    SerialPort getSerialPort() const;
    void setSerialPort(SerialPort port);
    bool isSerialEnabled() const;
    void setSerialEnabled(bool enabled);
    
    bool isTcpServerEnabled() const;
    void setTcpServerEnabled(bool enabled);
    uint16_t getTcpServerPort() const;  // Fixed port 2000
    
    bool isTcpClientEnabled() const;
    void setTcpClientEnabled(bool enabled);
    String getTcpClientHost() const;
    void setTcpClientHost(const String& host);
    uint16_t getTcpClientPort() const;
    void setTcpClientPort(uint16_t port);
    
    bool isUdpEnabled() const;
    void setUdpEnabled(bool enabled);
    uint16_t getUdpPort() const;
    void setUdpPort(uint16_t port);
    
    InputChannel getInputChannel() const;
    void setInputChannel(InputChannel channel);
    bool isInputForwardEnabled() const;
    void setInputForwardEnabled(bool enabled);
    
    bool isHdtEnabled() const;
    void setHdtEnabled(bool enabled);

private:
    
    // Thread safety
    PLATFORM_MUTEX_DECLARE(_mutex);
    
    // Configuration storage (moved from NMEAConfig)
    Preferences _preferences;
    uint16_t _updateIntervalMs;
    SerialPort _serialPort;
    bool _serialEnabled;
    bool _tcpServerEnabled;
    bool _tcpClientEnabled;
    String _tcpClientHost;
    uint16_t _tcpClientPort;
    bool _udpEnabled;
    uint16_t _udpPort;
    InputChannel _inputChannel;
    bool _inputForwardEnabled;
    bool _hdtEnabled;
    
    // Private unlocked initialization (called while mutex is already held)
    void beginUnsafe();
    void broadcastUnsafe(const char* sentence);
    
    // Serial output (use Print* for compatibility with Serial/Serial1/HWCDC)
    Print* _serial;
    
    // TCP Server
    int _tcpServerSocket; // Server socket file descriptor
    int _tcpClientSockets[5]; // Socket file descriptors for up to 5 simultaneous clients
    uint8_t _tcpClientCount;
    void startTcpServer();
    void stopTcpServer();
    void acceptNewTcpClient();
    bool isSocketConnected(int sockfd);
    
    // TCP Client
    int _tcpClientSocket;
    bool _tcpClientConnected;
    unsigned long _lastTcpClientConnectAttempt;
    uint16_t _tcpClientReconnectInterval;
    void startTcpClient();
    void stopTcpClient();
    void attemptTcpClientConnection();
    
    // UDP Broadcast (separate for AP and Station networks)
    WiFiUDP _udpSta;  // UDP for Station network
    WiFiUDP _udpAp;   // UDP for AP network
    bool _udpActive;
    IPAddress _staBroadcastAddress;  // Station network broadcast
    IPAddress _apBroadcastAddress;   // AP network broadcast
    bool _staNetworkAvailable;
    bool _apNetworkAvailable;
    void startUdp();
    void stopUdp();
    void updateBroadcastAddresses();
    
    // GPS Input
    TinyGPSPlus _gps;
    NMEA0183* _nmea;  // Reference to NMEA encoder for forwarding
    void readInputChannel();
    void feedCharacter(char c);
    
    // Forward buffer for GPS input
    char _forwardBuf[256];
    int _forwardBufPos;
    
    // Statistics
    uint32_t _droppedSentences;
    uint32_t _bytesReceived;
    unsigned long _lastStatsLogTime;
    void logReceiveStats();
};
