#include "WiFiManager.h"
#include <ArduinoLog.h>
#include <ESPmDNS.h>

// Default configuration
#define DEFAULT_HOSTNAME "ESP32-Compass"
#define DEFAULT_AP_PASSWORD "compass123"
#define RECONNECT_INTERVAL_MS 30000  // 30 seconds

WiFiManager::WiFiManager() 
    : _hostname(DEFAULT_HOSTNAME),
      _apSSID(DEFAULT_HOSTNAME),
      _apPassword(DEFAULT_AP_PASSWORD),
      _apActive(false),
      _stationEnabled(false),
    _isConnecting(false),
    _mdnsStarted(false),
    _mdnsName(""),
      _lastReconnectAttempt(0),
      _reconnectInterval(RECONNECT_INTERVAL_MS),
      _reconnectEnabled(true) {
}

void WiFiManager::begin() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    // Set WiFi mode to AP+STA
    WiFi.mode(WIFI_AP_STA);
    
    // Initialize preferences
    _preferences.begin("wifi", false);
    
    // Load saved hostname (if any)
    loadHostname();
    
    // Load AP password if saved
    String savedPassword = _preferences.getString("ap_password", "");
    if (savedPassword.length() > 0) {
        _apPassword = savedPassword;
        Log.verboseln("Loaded AP password from NVS");
    }
    
    // Set the hostname
    WiFi.setHostname(_hostname.c_str());
    
    // Try to load saved credentials
    String ssid, password;
    if (loadCredentials(ssid, password)) {
        Log.verboseln("Loaded WiFi credentials from NVS");
        
        // Check if we should auto-reconnect (was connected before reboot)
        bool wasConnected = _preferences.getBool("was_connected", false);
        
        if (wasConnected) {
            _staSSID = ssid;
            _staPassword = password;
            _stationEnabled = true;
            
            // Try to connect
            Log.verboseln("Auto-reconnecting to %s...", ssid.c_str());
            WiFi.begin(ssid.c_str(), password.c_str());
            
            // Wait for connection (with timeout)
            unsigned long startAttempt = millis();
            while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
                delay(100);
            }
            
            if (WiFi.status() == WL_CONNECTED) {
                Log.noticeln("WiFi connected! IP: %p", WiFi.localIP());
                startMDNS_internal();
            } else {
                Log.warningln("WiFi connection failed, will retry later");
            }
        } else {
            Log.verboseln("Not auto-connecting (was disconnected before reboot)");
            _staSSID = ssid;
            _staPassword = password;
            _stationEnabled = false;
        }
    } else {
        Log.verboseln("No saved WiFi credentials");
    }
    
    // Always start AP mode
    startAP_internal();
}

void WiFiManager::startAP() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    startAP_internal();
}

void WiFiManager::startAP_internal() {
    Log.verboseln("Starting Access Point: %s", _apSSID.c_str());
    
    bool success;
    if (_apPassword.length() > 0) {
        success = WiFi.softAP(_apSSID.c_str(), _apPassword.c_str());
    } else {
        success = WiFi.softAP(_apSSID.c_str());
    }
    
    if (success) {
        _apActive = true;
        Log.noticeln("AP started. IP: %p", WiFi.softAPIP());
    } else {
        Log.errorln("Failed to start AP!");
        _apActive = false;
    }
}

void WiFiManager::update() {
    // Try to acquire lock without blocking - return immediately if lock is held
    // This prevents blocking the sensor fusion thread
#if defined(ESP32) || defined(ESP_PLATFORM)
    std::unique_lock<std::mutex> lock(_mutex, std::defer_lock);
    if (!lock.try_lock()) {
        return;  // Lock is held by another thread, skip this update cycle
    }
#endif
    
    // Check if we should try to reconnect
    if (_isConnecting) {
        return;
    }

    if (_stationEnabled && _reconnectEnabled && WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - _lastReconnectAttempt >= _reconnectInterval) {
            tryReconnect();
            _lastReconnectAttempt = now;
        }
    }
}

void WiFiManager::tryReconnect() {
    if (_staSSID.length() == 0) return;
    
    Log.verboseln("Attempting to reconnect to %s...", _staSSID.c_str());
    WiFi.begin(_staSSID.c_str(), _staPassword.c_str());
    
    // Quick check (don't block)
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 5000) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Log.noticeln("WiFi reconnected! IP: %p", WiFi.localIP());
        startMDNS_internal();
    } else {
        Log.warningln("WiFi reconnection failed");
    }
}

void WiFiManager::setAPConfig(const char* ssid, const char* password) {
    _apSSID = ssid;
    if (password != nullptr) {
        _apPassword = password;
    }
}

void WiFiManager::setHostname(const char* hostname) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    setHostname_internal(hostname);
}

void WiFiManager::setHostname_internal(const char* hostname) {
    _hostname = hostname;
    WiFi.setHostname(hostname);
    
    // Update AP SSID to match hostname if not explicitly set
    if (_apSSID == DEFAULT_HOSTNAME) {
        _apSSID = hostname;
    }
    
    // Restart mDNS with new hostname if WiFi is connected
    if (WiFi.status() == WL_CONNECTED) {
        MDNS.end();
        startMDNS_internal();
    }
}

bool WiFiManager::connectToWiFi(const char* ssid, const char* password) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    Log.verboseln("Connecting to WiFi: %s", ssid);
    
    _staSSID = ssid;
    _staPassword = password;
    _stationEnabled = true;

    // Prevent update()/tryReconnect() from calling WiFi.begin() in parallel.
    _isConnecting = true;
    _lastReconnectAttempt = millis();
    
    WiFi.begin(ssid, password);
    
    // Wait for connection with timeout
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 15000) {
        delay(100);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Log.noticeln("WiFi connected! IP: %p", WiFi.localIP());
        saveConnectionState_internal(true);
        startMDNS_internal();
        _isConnecting = false;
        return true;
    } else {
        Log.warningln("WiFi connection failed!");
        _isConnecting = false;
        return false;
    }
}

void WiFiManager::disconnect() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    Log.infoln("Disconnecting from WiFi");
    WiFi.disconnect();
    _stationEnabled = false;
    saveConnectionState_internal(false);
}

void WiFiManager::saveCredentials(const char* ssid, const char* password) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    Log.verboseln("Saving WiFi credentials to NVS");
    _preferences.putString("ssid", ssid);
    _preferences.putString("password", password);
}

bool WiFiManager::loadCredentials(String &ssid, String &password) {
    ssid = _preferences.getString("ssid", "");
    password = _preferences.getString("password", "");
    
    return ssid.length() > 0;
}

void WiFiManager::clearCredentials() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    Log.infoln("Clearing WiFi credentials from NVS");
    _preferences.remove("ssid");
    _preferences.remove("password");
    _preferences.remove("was_connected");
    _staSSID = "";
    _staPassword = "";
    _stationEnabled = false;
}

int WiFiManager::scanNetworks() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    Log.verboseln("Scanning for WiFi networks...");
    return WiFi.scanNetworks();
}

String WiFiManager::getScannedSSID(int index) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return WiFi.SSID(index);
}

int WiFiManager::getScannedRSSI(int index) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return WiFi.RSSI(index);
}

bool WiFiManager::getScannedEncryption(int index) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
}

void WiFiManager::saveConnectionState(bool connected) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    saveConnectionState_internal(connected);
}

void WiFiManager::saveConnectionState_internal(bool connected) {
    _preferences.putBool("was_connected", connected);
    Log.verboseln("Saved connection state: %s", connected ? "connected" : "disconnected");
}

void WiFiManager::saveHostname(const char* hostname) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    Log.verboseln("Saving hostname to NVS: %s", hostname);
    _preferences.putString("hostname", hostname);
    setHostname_internal(hostname);
    
    // Restart AP with new SSID
    if (_apActive) {
        WiFi.softAPdisconnect(true);
        delay(100);
        startAP_internal();
    }
}

void WiFiManager::saveAPPassword(const char* password) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    Log.verboseln("Saving AP password to NVS");
    _preferences.putString("ap_password", password);
    _apPassword = password;
    
    // Restart AP with new password
    if (_apActive) {
        WiFi.softAPdisconnect(true);
        delay(100);
        startAP_internal();
    }
}

bool WiFiManager::loadHostname() {
    String hostname = _preferences.getString("hostname", "");
    if (hostname.length() > 0) {
        _hostname = hostname;
        _apSSID = hostname;  // Use hostname as AP SSID
        Log.verboseln("Loaded hostname from NVS: %s", hostname.c_str());
        return true;
    }
    return false;
}

void WiFiManager::startMDNS() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    startMDNS_internal();
}

void WiFiManager::startMDNS_internal() {
    // Extract hostname without domain suffix for mDNS
    String mdnsName = _hostname;
    int dotPos = mdnsName.indexOf('.');
    if (dotPos > 0) {
        mdnsName = mdnsName.substring(0, dotPos);
    }

    // Avoid repeated addService() calls which can fail if the service is already registered.
    if (_mdnsStarted && _mdnsName == mdnsName) {
        return;
    }

    if (_mdnsStarted) {
        MDNS.end();
        _mdnsStarted = false;
    }
    
    if (!MDNS.begin(mdnsName.c_str())) {
        Log.warningln("Error starting mDNS");
        return;
    }
    
    // Add service advertisement
    if (!MDNS.addService("http", "tcp", 80)) {
        Log.warningln("mDNS addService failed");
    }

    _mdnsStarted = true;
    _mdnsName = mdnsName;
    
    Log.noticeln("mDNS started: http://%s.local", mdnsName.c_str());
}
