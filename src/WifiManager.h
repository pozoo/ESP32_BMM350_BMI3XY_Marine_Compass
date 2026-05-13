#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "platform_mutex.h"

class WiFiManager {
public:
    WiFiManager();
    
    // Initialize WiFi with default or stored settings
    void begin();
    
    // Update loop - handles reconnection
    void update();
    
    // Station mode - connect to WiFi network
    bool connectToWiFi(const char* ssid, const char* password);
    void disconnect();
    
    // Credential storage
    void saveCredentials(const char* ssid, const char* password);
    void clearCredentials();
    
    // Hostname and AP password persistence
    void saveHostname(const char* hostname);
    void saveAPPassword(const char* password);
    
    // Status queries
    bool isAPActive() const { return _apActive; }
    bool isStationConnected() const { return WiFi.status() == WL_CONNECTED; }
    IPAddress getAPIP() const { return WiFi.softAPIP(); }
    IPAddress getStationIP() const { return WiFi.localIP(); }
    String getHostname() const { return _hostname; }
    String getAPSSID() const { return _apSSID; }
    String getStationSSID() const { return _staSSID; }
    
    // Scan for available networks
    int scanNetworks();
    String getScannedSSID(int index);
    int getScannedRSSI(int index);
    bool getScannedEncryption(int index);
    
private:
    Preferences _preferences;
    PLATFORM_MUTEX_DECLARE(_mutex);
    
    String _hostname;
    String _apSSID;
    String _apPassword;
    bool _apActive;
    
    String _staSSID;
    String _staPassword;
    bool _stationEnabled;

    bool _isConnecting;

    bool _mdnsStarted;
    String _mdnsName;
    
    unsigned long _lastReconnectAttempt;
    unsigned long _reconnectInterval;
    bool _reconnectEnabled;
    
    // Internal helper methods
    void setAPConfig(const char* ssid, const char* password = nullptr);
    void setHostname(const char* hostname);
    bool loadCredentials(String &ssid, String &password);
    void saveConnectionState(bool connected);
    bool loadHostname();
    void startAP();
    void tryReconnect();
    void startMDNS();
    
    // Internal unlocked versions (for use by other methods that already hold the lock)
    void setHostname_internal(const char* hostname);
    void saveConnectionState_internal(bool connected);
    void startAP_internal();
    void startMDNS_internal();
};

#endif // WIFI_MANAGER_H
