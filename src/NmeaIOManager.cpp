#include "NmeaIOManager.h"
#include "NMEA0183.h"
#include <ArduinoLog.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <arpa/inet.h>

NmeaIOManager::NmeaIOManager(NMEA0183* nmea) 
    : _updateIntervalMs(1000),
      _serialPort(SERIAL_0),
      _serialEnabled(false),
      _tcpServerEnabled(false),
      _tcpClientEnabled(false),
      _tcpClientPort(2000),
      _udpEnabled(false),
      _udpPort(10110),
      _inputChannel(INPUT_NONE),
      _inputForwardEnabled(false),
      _hdtEnabled(true),
      _serial(nullptr),
      _tcpServerSocket(-1),
      _tcpClientCount(0),
      _tcpClientSocket(-1),
      _tcpClientConnected(false),
      _lastTcpClientConnectAttempt(0),
      _tcpClientReconnectInterval(10000),
      _udpActive(false),
      _staNetworkAvailable(false),
      _apNetworkAvailable(false),
      _nmea(nmea),
      _forwardBufPos(0),
      _droppedSentences(0),
      _bytesReceived(0),
      _lastStatsLogTime(0) {
    // Initialize socket arrays
    for (int i = 0; i < 5; i++) {
        _tcpClientSockets[i] = -1;
    }
}

NmeaIOManager::~NmeaIOManager() {
    stopTcpServer();
    stopTcpClient();
    stopUdp();
}

// Configuration management methods (moved from NMEAConfig)
void NmeaIOManager::loadConfig() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    _preferences.begin("nmea", true); // read-only
    
    _updateIntervalMs = _preferences.getUShort("update_ms", RATE_10HZ);
    _serialPort = static_cast<SerialPort>(_preferences.getUChar("serial_port", SERIAL_0));
    _serialEnabled = _preferences.getBool("serial_en", false);
    _tcpServerEnabled = _preferences.getBool("tcp_srv_en", false);
    _tcpClientEnabled = _preferences.getBool("tcp_cli_en", false);
    _tcpClientHost = _preferences.getString("tcp_cli_host", "");
    _tcpClientPort = _preferences.getUShort("tcp_cli_port", 2000);
    _udpEnabled = _preferences.getBool("udp_en", false);
    _udpPort = _preferences.getUShort("udp_port", 10110);
    _inputChannel = static_cast<InputChannel>(_preferences.getUChar("input_ch", INPUT_NONE));
    _inputForwardEnabled = _preferences.getBool("input_fwd", false);
    _hdtEnabled = _preferences.getBool("hdt_enabled", true);
    
    _preferences.end();
    
    Log.infoln("NMEA config loaded: interval=%dms, serial=%d (en=%d), tcp_srv=%d, tcp_cli=%d (host=%s:%d), udp=%d (port=%d), input_ch=%d, input_fwd=%d",
               _updateIntervalMs, _serialPort, _serialEnabled, _tcpServerEnabled, 
               _tcpClientEnabled, _tcpClientHost.c_str(), _tcpClientPort, 
               _udpEnabled, _udpPort, _inputChannel, _inputForwardEnabled);
}

void NmeaIOManager::saveConfig() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    _preferences.begin("nmea", false); // read-write
    
    _preferences.putUShort("update_ms", _updateIntervalMs);
    _preferences.putUChar("serial_port", static_cast<uint8_t>(_serialPort));
    _preferences.putBool("serial_en", _serialEnabled);
    _preferences.putBool("tcp_srv_en", _tcpServerEnabled);
    _preferences.putBool("tcp_cli_en", _tcpClientEnabled);
    _preferences.putString("tcp_cli_host", _tcpClientHost);
    _preferences.putUShort("tcp_cli_port", _tcpClientPort);
    _preferences.putBool("udp_en", _udpEnabled);
    _preferences.putUShort("udp_port", _udpPort);
    _preferences.putUChar("input_ch", static_cast<uint8_t>(_inputChannel));
    _preferences.putBool("input_fwd", _inputForwardEnabled);
    _preferences.putBool("hdt_enabled", _hdtEnabled);
    
    _preferences.end();
    
    Log.infoln("NMEA config saved");
}

void NmeaIOManager::resetConfig() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    _preferences.begin("nmea", false);
    _preferences.clear();
    _preferences.end();
    
    // Reset to defaults
    _updateIntervalMs = RATE_10HZ;
    _serialPort = SERIAL_0;
    _serialEnabled = false;
    _tcpServerEnabled = false;
    _tcpClientEnabled = false;
    _tcpClientHost = "";
    _tcpClientPort = 2000;
    _udpEnabled = false;
    _udpPort = 10110;
    _inputChannel = INPUT_NONE;
    _inputForwardEnabled = false;
    _hdtEnabled = true;
    
    Log.infoln("NMEA config reset to defaults");
}

void NmeaIOManager::begin() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    beginUnsafe();
}

void NmeaIOManager::beginUnsafe() {
    Log.infoln("NMEAOutputManager: Initializing channels");
    
    // Configure serial port
    if (_serialEnabled) {
        if (_serialPort == SERIAL_0) {
            _serial = &Serial;  // HWCDC on ESP32-C3
        } else {
            Serial1.begin(4800, SERIAL_8N1, 20, 21); // Default ESP32-C3 pins: RX=20, TX=21
            _serial = &Serial1;
            Log.infoln("Serial1 initialized at 4800 baud");
        }
    }
    
    // Start TCP server if enabled
    if (_tcpServerEnabled) {
        startTcpServer();
    }
    
    // Start TCP client if enabled
    if (_tcpClientEnabled) {
        startTcpClient();
    }
    
    // Start UDP if enabled
    if (_udpEnabled) {
        startUdp();
    }
}

void NmeaIOManager::update() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    
    // Read GPS input if configured
    if (_inputChannel != INPUT_NONE) {
        readInputChannel();
    }
    
    // TCP server - accept new clients
    if (_tcpServerSocket >= 0 && _tcpClientCount < 5) {
        acceptNewTcpClient();
    }
    
    // TCP server - check existing clients and read data
    for (int i = 0; i < 5; i++) {
        if (_tcpClientSockets[i] >= 0) {
            if (!isSocketConnected(_tcpClientSockets[i])) {
                Log.infoln("TCP client %d disconnected", i);
                lwip_close(_tcpClientSockets[i]);
                _tcpClientSockets[i] = -1;
                _tcpClientCount--;
            } else {
                // ALWAYS read to drain receive buffer and prevent TCP flow control deadlock
                // Only process data if this is the configured input channel
                bool readMore = true;
                while (readMore) {
                    char buffer[256];
                    ssize_t received = lwip_recv(_tcpClientSockets[i], buffer, sizeof(buffer), MSG_DONTWAIT);
                    if (received > 0) {
                        // Only feed to GPS parser if this is the input channel
                        if (_inputChannel == INPUT_TCP_SERVER) {
                            _bytesReceived += received;
                            for (ssize_t j = 0; j < received; j++) {
                                feedCharacter(buffer[j]);
                            }
                        }
                        // Otherwise discard to keep buffer drained
                        // Continue reading if we got a full buffer
                        readMore = (received == sizeof(buffer));
                    } else if (received < 0) {
                        if (errno == EWOULDBLOCK || errno == EAGAIN) {
                            // No more data available
                            readMore = false;
                        } else {
                            // Read error
                            Log.warningln("TCP client %d read error (errno=%d), disconnecting", i, errno);
                            lwip_close(_tcpClientSockets[i]);
                            _tcpClientSockets[i] = -1;
                            _tcpClientCount--;
                            readMore = false;
                        }
                    } else {
                        // received == 0, connection closed
                        readMore = false;
                    }
                }
            }
        }
    }
    
    // TCP client reconnection logic
    if (_tcpClientEnabled) {
        if (_tcpClientSocket >= 0 && isSocketConnected(_tcpClientSocket)) {
            _tcpClientConnected = true;
            // ALWAYS read to drain buffer - loop until buffer drained
            bool readMore = true;
            while (readMore) {
                char buffer[256];
                ssize_t received = lwip_recv(_tcpClientSocket, buffer, sizeof(buffer), MSG_DONTWAIT);
                if (received > 0) {
                    // Only feed to GPS parser if this is the input channel
                    if (_inputChannel == INPUT_TCP_CLIENT) {
                        _bytesReceived += received;
                        for (ssize_t j = 0; j < received; j++) {
                            feedCharacter(buffer[j]);
                        }
                    }
                    // Otherwise discard to keep buffer drained
                    // Continue if we got a full buffer
                    readMore = (received == sizeof(buffer));
                } else if (received < 0) {
                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        // No more data available
                        readMore = false;
                    } else {
                        // Read error
                        Log.warningln("TCP client read error (errno=%d), disconnecting", errno);
                        lwip_close(_tcpClientSocket);
                        _tcpClientSocket = -1;
                        _tcpClientConnected = false;
                        readMore = false;
                    }
                } else {
                    // received == 0, connection closed
                    readMore = false;
                }
            }
        } else if (_tcpClientConnected) {
            _tcpClientConnected = false;
            if (_tcpClientSocket >= 0) {
                lwip_close(_tcpClientSocket);
                _tcpClientSocket = -1;
            }
            Log.infoln("TCP client disconnected");
        } else if (millis() - _lastTcpClientConnectAttempt > _tcpClientReconnectInterval) {
            attemptTcpClientConnection();
        }
    }
    
    // Update UDP broadcast addresses if WiFi state changed
    if (_udpEnabled) {
        updateBroadcastAddresses();
    }
    
    // Log receive statistics every 5 seconds
    logReceiveStats();
}

void NmeaIOManager::broadcast(const char* sentence) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    broadcastUnsafe(sentence);
}

void NmeaIOManager::broadcastUnsafe(const char* sentence) {
    if (!sentence || strlen(sentence) == 0) {
        return;
    }
    
    size_t len = strlen(sentence);
    bool sentToAny = false;
    
    // Serial output
    if (_serialEnabled && _serial) {
        size_t written = _serial->print(sentence);
        if (written > 0) {
            sentToAny = true;
        }
    }
    
    // TCP Server clients - non-blocking writes
    for (int i = 0; i < 5; i++) {
        if (_tcpClientSockets[i] >= 0 && isSocketConnected(_tcpClientSockets[i])) {
            ssize_t sent = lwip_send(_tcpClientSockets[i], sentence, len, MSG_DONTWAIT);
            if (sent > 0) {
                sentToAny = true;
            } else if (sent < 0) {
                // Send failed - buffer full or other error, skip this client
                _droppedSentences++;
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    Log.verboseln("TCP client %d buffer full, skipping", i);
                } else {
                    Log.verboseln("TCP client %d send error (errno=%d), skipping", i, errno);
                }
            }
        }
    }
    
    // TCP Client - non-blocking write
    if (_tcpClientSocket >= 0 && _tcpClientConnected && isSocketConnected(_tcpClientSocket)) {
        ssize_t sent = lwip_send(_tcpClientSocket, sentence, len, MSG_DONTWAIT);
        if (sent > 0) {
            sentToAny = true;
        } else if (sent < 0) {
            // Send failed - buffer full or other error, skip
            _droppedSentences++;
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                Log.verboseln("TCP client buffer full, skipping");
            } else {
                Log.verboseln("TCP client send error (errno=%d), skipping", errno);
            }
        }
    }
    
    // UDP Broadcast to Station network
    if (_udpActive && _staNetworkAvailable) {
        if (_udpSta.beginPacket(_staBroadcastAddress, _udpPort)) {
            _udpSta.write((const uint8_t*)sentence, strlen(sentence));
            if (_udpSta.endPacket()) {
                sentToAny = true;
            } else {
                Log.verboseln("UDP Station endPacket failed");
            }
        } else {
            Log.verboseln("UDP Station beginPacket failed");
        }
    }
    
    // UDP Broadcast to AP network (only if clients are connected)
    if (_udpActive && _apNetworkAvailable && WiFi.softAPgetStationNum() > 0) {
        if (_udpAp.beginPacket(_apBroadcastAddress, _udpPort)) {
            _udpAp.write((const uint8_t*)sentence, strlen(sentence));
            if (_udpAp.endPacket()) {
                sentToAny = true;
            } else {
                Log.verboseln("UDP AP endPacket failed");
            }
        } else {
            Log.verboseln("UDP AP beginPacket failed");
        }
    }
    
    // Track dropped sentences
    if (!sentToAny && (_serialEnabled || _tcpServerEnabled || 
                       _tcpClientEnabled || _udpEnabled)) {
        _droppedSentences++;
        if (_droppedSentences % 100 == 1) { // Log every 100th drop
            Log.warningln("NMEA sentences dropped: %d", _droppedSentences);
        }
    }
}

// Statistics methods
uint16_t NmeaIOManager::getTcpServerClientCount() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _tcpClientCount;
}

bool NmeaIOManager::isTcpClientConnected() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _tcpClientConnected;
}

bool NmeaIOManager::isUdpActive() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _udpActive;
}

uint32_t NmeaIOManager::getDroppedSentenceCount() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _droppedSentences;
}

void NmeaIOManager::resetDroppedSentenceCount() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _droppedSentences = 0;
}

void NmeaIOManager::logReceiveStats() {
    unsigned long now = millis();
    if (now - _lastStatsLogTime >= 5000) {
        if (_bytesReceived > 0) {
            // Use integer math to avoid float formatting issues with ArduinoLog
            uint32_t bytesPerSec = _bytesReceived / 5;
            Log.infoln("NMEA RX: %u bytes received in last 5s (%u bytes/sec)", 
                      _bytesReceived, bytesPerSec);
        }
        _bytesReceived = 0;
        _lastStatsLogTime = now;
    }
}

// GPS data access methods
bool NmeaIOManager::getGPSLocationValid() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.location.isValid();
}

bool NmeaIOManager::getGPSDateValid() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.date.isValid();
}

double NmeaIOManager::getGPSCOG() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.course.deg();
}

double NmeaIOManager::getGPSSOG() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.speed.knots();
}

double NmeaIOManager::getGPSLatitude() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.location.lat();
}

double NmeaIOManager::getGPSLongitude() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.location.lng();
}

uint16_t NmeaIOManager::getGPSYear() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.date.year();  // Return full 4-digit year
}

uint8_t NmeaIOManager::getGPSMonth() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.date.month();
}

uint8_t NmeaIOManager::getGPSDay() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.date.day();
}

bool NmeaIOManager::getGPSTimeValid() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.time.isValid();
}

uint8_t NmeaIOManager::getGPSHour() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.time.hour();
}

uint8_t NmeaIOManager::getGPSMinute() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.time.minute();
}

uint8_t NmeaIOManager::getGPSSecond() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _gps.time.second();
}

// Configuration accessor methods (moved from NMEAConfig)
uint16_t NmeaIOManager::getUpdateIntervalMs() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _updateIntervalMs;
}

void NmeaIOManager::setUpdateIntervalMs(uint16_t intervalMs) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _updateIntervalMs = intervalMs;
}

NmeaIOManager::SerialPort NmeaIOManager::getSerialPort() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _serialPort;
}

void NmeaIOManager::setSerialPort(SerialPort port) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _serialPort = port;
}

bool NmeaIOManager::isSerialEnabled() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _serialEnabled;
}

void NmeaIOManager::setSerialEnabled(bool enabled) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _serialEnabled = enabled;
}

bool NmeaIOManager::isTcpServerEnabled() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _tcpServerEnabled;
}

void NmeaIOManager::setTcpServerEnabled(bool enabled) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _tcpServerEnabled = enabled;
}

uint16_t NmeaIOManager::getTcpServerPort() const {
    return 2000;  // Fixed port, no lock needed
}

bool NmeaIOManager::isTcpClientEnabled() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _tcpClientEnabled;
}

void NmeaIOManager::setTcpClientEnabled(bool enabled) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _tcpClientEnabled = enabled;
}

String NmeaIOManager::getTcpClientHost() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _tcpClientHost;
}

void NmeaIOManager::setTcpClientHost(const String& host) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _tcpClientHost = host;
}

uint16_t NmeaIOManager::getTcpClientPort() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _tcpClientPort;
}

void NmeaIOManager::setTcpClientPort(uint16_t port) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _tcpClientPort = port;
}

bool NmeaIOManager::isUdpEnabled() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _udpEnabled;
}

void NmeaIOManager::setUdpEnabled(bool enabled) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _udpEnabled = enabled;
}

uint16_t NmeaIOManager::getUdpPort() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _udpPort;
}

void NmeaIOManager::setUdpPort(uint16_t port) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _udpPort = port;
}

NmeaIOManager::InputChannel NmeaIOManager::getInputChannel() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _inputChannel;
}

void NmeaIOManager::setInputChannel(InputChannel channel) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _inputChannel = channel;
}

bool NmeaIOManager::isInputForwardEnabled() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _inputForwardEnabled;
}

void NmeaIOManager::setInputForwardEnabled(bool enabled) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _inputForwardEnabled = enabled;
}

bool NmeaIOManager::isHdtEnabled() const {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    return _hdtEnabled;
}

void NmeaIOManager::setHdtEnabled(bool enabled) {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    _hdtEnabled = enabled;
}

void NmeaIOManager::onConfigChanged() {
    PLATFORM_LOCK_GUARD(PLATFORM_MUTEX_TYPE, lock, _mutex);
    Log.infoln("NMEAOutputManager: Configuration changed, reinitializing");
    
    // Stop everything
    stopTcpServer();
    stopTcpClient();
    stopUdp();
    _serial = nullptr;
    
    // Restart with new config
    beginUnsafe();
}

// TCP Server implementation
void NmeaIOManager::startTcpServer() {
    if (_tcpServerSocket >= 0) {
        stopTcpServer();
    }
    
    // Create server socket
    _tcpServerSocket = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (_tcpServerSocket < 0) {
        Log.errorln("Failed to create TCP server socket");
        return;
    }
    
    // Set socket to non-blocking
    int flags = lwip_fcntl(_tcpServerSocket, F_GETFL, 0);
    lwip_fcntl(_tcpServerSocket, F_SETFL, flags | O_NONBLOCK);
    
    // Set SO_REUSEADDR
    int reuse = 1;
    lwip_setsockopt(_tcpServerSocket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    
    // Bind to port 2000 on all interfaces
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(2000);
    
    if (lwip_bind(_tcpServerSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        Log.errorln("Failed to bind TCP server socket (errno=%d)", errno);
        lwip_close(_tcpServerSocket);
        _tcpServerSocket = -1;
        return;
    }
    
    // Listen with backlog of 5
    if (lwip_listen(_tcpServerSocket, 5) < 0) {
        Log.errorln("Failed to listen on TCP server socket (errno=%d)", errno);
        lwip_close(_tcpServerSocket);
        _tcpServerSocket = -1;
        return;
    }
    
    Log.infoln("TCP server started on port 2000");
}

void NmeaIOManager::stopTcpServer() {
    // Close all client sockets
    for (int i = 0; i < 5; i++) {
        if (_tcpClientSockets[i] >= 0) {
            lwip_close(_tcpClientSockets[i]);
            _tcpClientSockets[i] = -1;
        }
    }
    _tcpClientCount = 0;
    
    // Close server socket
    if (_tcpServerSocket >= 0) {
        lwip_close(_tcpServerSocket);
        _tcpServerSocket = -1;
    }
    
    Log.infoln("TCP server stopped");
}

bool NmeaIOManager::isSocketConnected(int sockfd) {
    if (sockfd < 0) return false;
    
    // Use getpeername to check if socket is still connected
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    return lwip_getpeername(sockfd, (struct sockaddr*)&addr, &addr_len) == 0;
}

void NmeaIOManager::acceptNewTcpClient() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    // Accept new connection (non-blocking)
    int client_fd = lwip_accept(_tcpServerSocket, (struct sockaddr*)&client_addr, &addr_len);
    if (client_fd < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            Log.warningln("Accept failed (errno=%d)", errno);
        }
        return;
    }
    
    // Find empty slot
    for (int i = 0; i < 5; i++) {
        if (_tcpClientSockets[i] < 0) {
            _tcpClientSockets[i] = client_fd;
            
            // Set socket to non-blocking mode
            int flags = lwip_fcntl(client_fd, F_GETFL, 0);
            lwip_fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
            
            // Set TCP_NODELAY
            int nodelay = 1;
            lwip_setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
            
            // Increase receive buffer to 8KB
            int rcvbuf = 8192;
            lwip_setsockopt(client_fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
            
            // Increase send buffer to 8KB  
            int sndbuf = 8192;
            lwip_setsockopt(client_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
            
            // Enable keepalive
            int keepalive = 1;
            lwip_setsockopt(client_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
            
            // Set 100ms send timeout as safety net
            struct timeval timeout;
            timeout.tv_sec = 0;
            timeout.tv_usec = 100000;
            lwip_setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
            
            _tcpClientCount++;
            
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
            Log.infoln("TCP client %d connected from %s (socket %d)", i, ip_str, client_fd);
            
            if (_inputChannel == INPUT_TCP_SERVER) {
                Log.verboseln("TCP server client will be polled for GPS input");
            }
            
            return;
        }
    }
    
    // No slots available
    Log.warningln("TCP server full, rejecting client");
    lwip_close(client_fd);
}

// TCP Client implementation
void NmeaIOManager::startTcpClient() {
    if (_tcpClientHost.length() == 0) {
        Log.warningln("TCP client host not configured");
        return;
    }
    
    attemptTcpClientConnection();
}

void NmeaIOManager::stopTcpClient() {
    if (_tcpClientSocket >= 0) {
        lwip_close(_tcpClientSocket);
        _tcpClientSocket = -1;
        _tcpClientConnected = false;
        Log.infoln("TCP client stopped");
    }
}

void NmeaIOManager::attemptTcpClientConnection() {
    _lastTcpClientConnectAttempt = millis();
    
    if (_tcpClientSocket >= 0) {
        lwip_close(_tcpClientSocket);
        _tcpClientSocket = -1;
    }
    
    Log.infoln("TCP client connecting to %s:%d", _tcpClientHost.c_str(), _tcpClientPort);
    
    // Create socket
    int sockfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        Log.warningln("TCP client socket creation failed");
        return;
    }
    
    // Resolve hostname
    struct hostent* host = gethostbyname(_tcpClientHost.c_str());
    if (!host) {
        Log.warningln("TCP client DNS lookup failed for %s", _tcpClientHost.c_str());
        lwip_close(sockfd);
        return;
    }
    
    // Setup address
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(_tcpClientPort);
    memcpy(&addr.sin_addr, host->h_addr_list[0], sizeof(addr.sin_addr));
    
    // Set socket to non-blocking for connect
    int flags = lwip_fcntl(sockfd, F_GETFL, 0);
    lwip_fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
    
    // Attempt connection
    int result = lwip_connect(sockfd, (struct sockaddr*)&addr, sizeof(addr));
    if (result < 0 && errno != EINPROGRESS) {
        Log.warningln("TCP client connection failed (errno=%d)", errno);
        lwip_close(sockfd);
        return;
    }
    
    // Set TCP_NODELAY
    int nodelay = 1;
    lwip_setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    
    // Increase receive buffer to 8KB
    int rcvbuf = 8192;
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // Increase send buffer to 8KB
    int sndbuf = 8192;
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    // Enable keepalive
    int keepalive = 1;
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
    
    // Set 100ms send timeout
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    lwip_setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    _tcpClientSocket = sockfd;
    _tcpClientConnected = true;
    Log.infoln("TCP client connected to %s:%d (socket %d)", _tcpClientHost.c_str(), _tcpClientPort, sockfd);
    
    if (_inputChannel == INPUT_TCP_CLIENT) {
        Log.verboseln("TCP client will be polled for GPS input");
    }
}

// UDP implementation
void NmeaIOManager::startUdp() {
    updateBroadcastAddresses();
    _udpActive = true;
    
    // Bind UDP port for input reception
    if (_inputChannel == INPUT_UDP) {
        if (_udpSta.begin(_udpPort)) {
            Log.infoln("UDP receiver started on port %d", _udpPort);
        } else {
            Log.warningln("UDP receiver failed to start on port %d", _udpPort);
        }
    }
    
    if (_staNetworkAvailable || _apNetworkAvailable) {
        Log.infoln("UDP broadcast enabled on port %d", _udpPort);
        if (_staNetworkAvailable) {
            Log.infoln("  Station broadcast: %s", _staBroadcastAddress.toString().c_str());
        }
        if (_apNetworkAvailable) {
            Log.infoln("  AP broadcast: %s", _apBroadcastAddress.toString().c_str());
        }
    } else {
        Log.warningln("UDP: No networks available");
    }
}

void NmeaIOManager::stopUdp() {
    if (_udpActive) {
        _udpSta.stop();
        _udpAp.stop();
        _udpActive = false;
        _staNetworkAvailable = false;
        _apNetworkAvailable = false;
        Log.infoln("UDP stopped");
    }
}

void NmeaIOManager::updateBroadcastAddresses() {
    // Check Station network
    _staNetworkAvailable = false;
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        IPAddress subnet = WiFi.subnetMask();
        
        // Calculate broadcast address: IP | ~subnet
        _staBroadcastAddress = IPAddress(
            ip[0] | (~subnet[0]),
            ip[1] | (~subnet[1]),
            ip[2] | (~subnet[2]),
            ip[3] | (~subnet[3])
        );
        _staNetworkAvailable = true;
    }
    
    // Check AP network
    _apNetworkAvailable = false;
    if (WiFi.getMode() == WIFI_AP || WiFi.getMode() == WIFI_AP_STA) {
        IPAddress ip = WiFi.softAPIP();
        IPAddress subnet = WiFi.softAPSubnetMask();
        
        // Calculate broadcast address: IP | ~subnet
        _apBroadcastAddress = IPAddress(
            ip[0] | (~subnet[0]),
            ip[1] | (~subnet[1]),
            ip[2] | (~subnet[2]),
            ip[3] | (~subnet[3])
        );
        _apNetworkAvailable = true;
    }
}

// GPS Input implementation
void NmeaIOManager::readInputChannel() {
    InputChannel channel = _inputChannel;
    
    switch (channel) {
        case INPUT_SERIAL_0:
            while (Serial.available()) {
                feedCharacter(Serial.read());
            }
            break;
            
        case INPUT_SERIAL_1:
            while (Serial1.available()) {
                feedCharacter(Serial1.read());
            }
            break;
            
        case INPUT_TCP_SERVER:
        case INPUT_TCP_CLIENT:
            // TCP input is handled via onData callbacks set up during connection
            // See handleNewTcpClient() and attemptTcpClientConnection()
            break;
            
        case INPUT_UDP:
            // Check for incoming UDP packets
            if (_udpActive) {
                int packetSize = _udpSta.parsePacket();
                if (packetSize > 0) {
                    while (_udpSta.available()) {
                        feedCharacter(_udpSta.read());
                    }
                }
            }
            break;
            
        case INPUT_NONE:
        default:
            // No input configured
            break;
    }
}

void NmeaIOManager::feedCharacter(char c) {
    // Feed character to GPS parser
    _gps.encode(c);
    
    // Forward to output channels if enabled
    if (_inputForwardEnabled) {
        // Buffer incoming NMEA sentences
        if (_forwardBufPos > sizeof(_forwardBuf) - 2) {
            Log.warningln("Forward buffer full, discarding");
            _forwardBufPos = 0;
        }
        
        if (c == '$') {
            // Start of new sentence
            _forwardBuf[0] = c;
            _forwardBufPos = 1;
        } else if (c == '\n' && _forwardBufPos > 0) {
            // End of sentence - add \r\n and null terminator
            if (_forwardBufPos > 0 && _forwardBuf[_forwardBufPos - 1] == '\r') {
                // Already has \r, just add \n and terminate
                _forwardBuf[_forwardBufPos] = c;
                _forwardBuf[_forwardBufPos + 1] = '\0';
            } else {
                // Add \r\n and terminate
                _forwardBuf[_forwardBufPos] = '\r';
                _forwardBuf[_forwardBufPos + 1] = c;
                _forwardBuf[_forwardBufPos + 2] = '\0';
            }
            
            // Validate checksum if we have NMEA encoder
            bool valid = true;
            if (_nmea) {
                valid = _nmea->verifyChecksum(_forwardBuf);
                if (!valid) {
                    Log.verboseln("Invalid checksum, not forwarding: %s", _forwardBuf);
                }
            }
            
            // Broadcast complete validated sentence to all output channels
            if (valid && _forwardBufPos > 10) {  // Minimum valid sentence length
                broadcastUnsafe(_forwardBuf);
                Log.verboseln("Forwarded GPS sentence: %s", _forwardBuf);
            }
            
            _forwardBufPos = 0;
        } else if (_forwardBufPos > 0) {
            // Middle of sentence
            _forwardBuf[_forwardBufPos] = c;
            _forwardBufPos++;
        }
        // Ignore characters outside of sentences
    }
}
