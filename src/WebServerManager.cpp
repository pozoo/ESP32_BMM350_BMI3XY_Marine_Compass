#include "WebServerManager.h"
#include "../include/Html_compass.h"
#include "version.h"
#include <ArduinoLog.h>
#include <Update.h>

WebServerManager::WebServerManager(Sensor_9_DOF* sensor, WiFiManager* wifiManager, CalibrationManager* calibrationManager, NmeaIOManager* nmeaOutput, Deviation* deviation, WMM* wmm) 
    : _sensor(sensor), _wifiManager(wifiManager), _calibrationManager(calibrationManager), _nmeaOutput(nmeaOutput), _deviation(deviation), _wmm(wmm), _port(80),
      _apiController(sensor, calibrationManager, deviation, wmm) {
}

void WebServerManager::begin() {
    setupRoutes();
    setupOTA();
    
    _server.begin();
    Log.noticeln("Web server started on port %d", _port);
    Log.noticeln("OTA update available at http://[device-ip]/update");
}

void WebServerManager::setupRoutes() {
    // Serve main HTML page from PROGMEM (gzipped)
    _server.on("/", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleRoot(request, response);
    });
    
    _server.on("/compass.html", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleRoot(request, response);
    });
    
    // API endpoint for sensor data
    _server.on("/data", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleData(request, response);
    });
    
    // API endpoint for version info
    _server.on("/api/version", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        String json = _apiController.getVersion();
        return response->send(200, "application/json", json.c_str());
    });
    
    // Calibration endpoints
    _server.on("/calibrate/start", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleCalibrateStart(request, response);
    });
    
    _server.on("/calibrate/stop", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleCalibrateStop(request, response);
    });
    
    _server.on("/calibrate/status", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleCalibrateStatus(request, response);
    });
    
    _server.on("/calibrate/compute", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleCalibrateCompute(request, response);
    });
    
    _server.on("/calibrate/info", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleCalibrateInfo(request, response);
    });
    
    // WiFi configuration endpoint
    _server.on("/wifi", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleWiFiConfig(request, response);
    });
    
    // WiFi scan endpoint
    _server.on("/wifi/scan", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleWiFiScan(request, response);
    });
    
    // WiFi status endpoint
    _server.on("/wifi/status", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleWiFiStatus(request, response);
    });
    
    // Magnetic declination endpoints
    _server.on("/settings/declination", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleGetDeclination(request, response);
    });
    
    _server.on("/settings/declination", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleSetDeclination(request, response);
    });
    
    // WMM endpoints
    _server.on("/api/wmm/status", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleWMMGetStatus(request, response);
    });
    
    _server.on("/api/wmm/auto", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleWMMSetAuto(request, response);
    });
    
    _server.on("/api/wmm/manual", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleWMMSetManual(request, response);
    });
    
    // NMEA configuration endpoints
    _server.on("/nmea/config", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleNMEAGetConfig(request, response);
    });
    
    _server.on("/nmea/config", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleNMEASetConfig(request, response);
    });
    
    _server.on("/nmea/status", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleNMEAGetStatus(request, response);
    });
    
    // Mounting calibration endpoints
    _server.on("/mounting/get", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleMountingGet(request, response);
    });
    
    _server.on("/mounting/set", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleMountingSet(request, response);
    });
    
    _server.on("/mounting/level", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleMountingLevel(request, response);
    });
    
    // Deviation table endpoints
    _server.on("/deviation/table", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleDeviationGetTable(request, response);
    });
    
    _server.on("/deviation/table", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleDeviationSetTable(request, response);
    });
    
    _server.on("/deviation/start", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleDeviationStart(request, response);
    });
    
    _server.on("/deviation/stop", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleDeviationStop(request, response);
    });
    
    _server.on("/deviation/status", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleDeviationStatus(request, response);
    });
    
    _server.on("/deviation/reset", HTTP_POST, [this](PsychicRequest* request, PsychicResponse* response) {
        return handleDeviationReset(request, response);
    });
    
    // Platform detection endpoint
    _server.on("/api/platform", HTTP_GET, [this](PsychicRequest* request, PsychicResponse* response) {
        return handlePlatformInfo(request, response);
    });
    
    // 404 handler
    _server.onNotFound([this](PsychicRequest* request, PsychicResponse* response) {
        return handleNotFound(request, response);
    });
}

esp_err_t WebServerManager::handleRoot(PsychicRequest* request, PsychicResponse* response) {
    // Serve compressed HTML from PROGMEM
    response->setCode(200);
    response->setContentType("text/html");
    response->addHeader("Content-Encoding", "gzip");
    response->addHeader("Cache-Control", "no-cache");
    response->setContent(WebContent::compass_gz, WebContent::compass_gz_len);
    return response->send();
}

esp_err_t WebServerManager::handleData(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.getData();
    return response->send(200, "application/json", json.c_str());
}

String WebServerManager::getSensorDataJSON() {
    // Deprecated - use _apiController.getData() instead
    return _apiController.getData();
}

esp_err_t WebServerManager::handleCalibrateStart(PsychicRequest* request, PsychicResponse* response) {
    int httpCode = 200;
    String json = _apiController.postCalibrateStart(request->body(), httpCode);
    return response->send(httpCode, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleCalibrateStop(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.postCalibrateStop();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleCalibrateStatus(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.getCalibrateStatus();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleCalibrateCompute(PsychicRequest* request, PsychicResponse* response) {
    int httpCode = 200;
    String json = _apiController.postCalibrateCompute(httpCode);
    return response->send(httpCode, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleCalibrateInfo(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.getCalibrateInfo();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleWiFiConfig(PsychicRequest* request, PsychicResponse* response) {
    // Get request body
    String body = request->body();
    
    // Parse JSON
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
        return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    }
    
    const char* action = doc["action"];
    
    if (action == nullptr) {
        return response->send(400, "application/json", "{\"error\":\"Missing action\"}");
    }
    
    if (strcmp(action, "connect") == 0) {
        const char* ssid = doc["ssid"];
        const char* password = doc["password"];
        
        if (ssid == nullptr) {
            return response->send(400, "application/json", "{\"error\":\"Missing SSID\"}");
        }
        
        // Attempt connection
        bool success = _wifiManager->connectToWiFi(ssid, password ? password : "");
        
        if (success) {
            // Save credentials
            _wifiManager->saveCredentials(ssid, password ? password : "");
            
            JsonDocument responseDoc;
            responseDoc["status"] = "connected";
            responseDoc["ip"] = _wifiManager->getStationIP().toString();
            
            String output;
            serializeJson(responseDoc, output);
            return response->send(200, "application/json", output.c_str());
        } else {
            return response->send(200, "application/json", "{\"status\":\"failed\"}");
        }
        
    } else if (strcmp(action, "disconnect") == 0) {
        // Disconnect WiFi
        _wifiManager->disconnect();
        return response->send(200, "application/json", "{\"status\":\"disconnecting\"}");
        
    } else if (strcmp(action, "clear") == 0) {
        // Clear credentials and disconnect
        _wifiManager->clearCredentials();
        _wifiManager->disconnect();
        return response->send(200, "application/json", "{\"status\":\"credentials cleared\"}");
        
    } else if (strcmp(action, "set_hostname") == 0) {
        const char* hostname = doc["hostname"];
        
        if (hostname == nullptr || strlen(hostname) == 0) {
            return response->send(400, "application/json", "{\"error\":\"Missing hostname\"}");
        }
        
        // Validate hostname (alphanumeric, hyphens, max 32 chars)
        String hostnameStr(hostname);
        if (hostnameStr.length() > 32) {
            return response->send(400, "application/json", "{\"error\":\"Hostname too long (max 32 chars)\"}");
        }
        
        // Save hostname
        _wifiManager->saveHostname(hostname);
        
        JsonDocument responseDoc;
        responseDoc["status"] = "success";
        responseDoc["hostname"] = _wifiManager->getHostname();
        responseDoc["ap_ssid"] = _wifiManager->getAPSSID();
        
        String output;
        serializeJson(responseDoc, output);
        return response->send(200, "application/json", output.c_str());
        
    } else if (strcmp(action, "set_ap_password") == 0) {
        const char* password = doc["password"];
        
        if (password == nullptr || strlen(password) == 0) {
            return response->send(400, "application/json", "{\"error\":\"Missing password\"}");
        }
        
        // Validate password length (minimum 8 characters for WPA2)
        String passwordStr(password);
        if (passwordStr.length() < 8) {
            return response->send(400, "application/json", "{\"error\":\"Password must be at least 8 characters\"}" );
        }
        
        if (passwordStr.length() > 63) {
            return response->send(400, "application/json", "{\"error\":\"Password must be 63 characters or less\"}");
        }
        
        // Save the new AP password (this will also restart the AP)
        _wifiManager->saveAPPassword(password);
        
        JsonDocument responseDoc;
        responseDoc["status"] = "success";
        responseDoc["message"] = "AP password updated and saved. AP has been restarted.";
        responseDoc["ap_ssid"] = _wifiManager->getAPSSID();
        
        String output;
        serializeJson(responseDoc, output);
        return response->send(200, "application/json", output.c_str());
        
    } else {
        return response->send(400, "application/json", "{\"error\":\"Unknown action\"}");
    }
}

esp_err_t WebServerManager::handleWiFiScan(PsychicRequest* request, PsychicResponse* response) {
    int numNetworks = _wifiManager->scanNetworks();
    
    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();
    
    for (int i = 0; i < numNetworks; i++) {
        JsonObject network = networks.add<JsonObject>();
        network["ssid"] = _wifiManager->getScannedSSID(i);
        network["rssi"] = _wifiManager->getScannedRSSI(i);
        network["encrypted"] = _wifiManager->getScannedEncryption(i);
    }
    
    String output;
    serializeJson(doc, output);
    return response->send(200, "application/json", output.c_str());
}

esp_err_t WebServerManager::handleWiFiStatus(PsychicRequest* request, PsychicResponse* response) {
    JsonDocument doc;
    
    // AP info
    doc["ap_active"] = _wifiManager->isAPActive();
    doc["ap_ssid"] = _wifiManager->getAPSSID();
    doc["ap_ip"] = _wifiManager->getAPIP().toString();
    
    // Station info
    doc["sta_connected"] = _wifiManager->isStationConnected();
    if (_wifiManager->isStationConnected()) {
        doc["sta_ip"] = _wifiManager->getStationIP().toString();
        doc["sta_ssid"] = _wifiManager->getStationSSID();
    }
    
    doc["hostname"] = _wifiManager->getHostname();
    
    String output;
    serializeJson(doc, output);
    return response->send(200, "application/json", output.c_str());
}

esp_err_t WebServerManager::handleGetDeclination(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.getDeclination();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleSetDeclination(PsychicRequest* request, PsychicResponse* response) {
    int httpCode = 200;
    String json = _apiController.postSetDeclination(request->body(), httpCode);
    return response->send(httpCode, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleNotFound(PsychicRequest* request, PsychicResponse* response) {
    return response->send(404, "text/plain", "Not Found");
}

esp_err_t WebServerManager::handleNMEAGetConfig(PsychicRequest* request, PsychicResponse* response) {
    JsonDocument doc;
    
    // Update rate
    doc["update_interval_ms"] = _nmeaOutput->getUpdateIntervalMs();
    
    // Serial port
    doc["serial_enabled"] = _nmeaOutput->isSerialEnabled();
    doc["serial_port"] = _nmeaOutput->getSerialPort();
    
    // TCP Server
    doc["tcp_server_enabled"] = _nmeaOutput->isTcpServerEnabled();
    doc["tcp_server_port"] = _nmeaOutput->getTcpServerPort();
    
    // TCP Client
    doc["tcp_client_enabled"] = _nmeaOutput->isTcpClientEnabled();
    doc["tcp_client_host"] = _nmeaOutput->getTcpClientHost();
    doc["tcp_client_port"] = _nmeaOutput->getTcpClientPort();
    
    // UDP
    doc["udp_enabled"] = _nmeaOutput->isUdpEnabled();
    doc["udp_port"] = _nmeaOutput->getUdpPort();
    
    // GPS Input
    doc["input_channel"] = _nmeaOutput->getInputChannel();
    doc["input_forward_enabled"] = _nmeaOutput->isInputForwardEnabled();
    
    // Sentence generation
    doc["hdt_enabled"] = _nmeaOutput->isHdtEnabled();
    
    String output;
    serializeJson(doc, output);
    return response->send(200, "application/json", output.c_str());
}

esp_err_t WebServerManager::handleNMEASetConfig(PsychicRequest* request, PsychicResponse* response) {
    String body = request->body();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    
    if (error) {
        return response->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    }
    
    // Update configuration
    if (doc["update_interval_ms"].is<uint16_t>()) {
        _nmeaOutput->setUpdateIntervalMs(doc["update_interval_ms"].as<uint16_t>());
    }
    
    if (doc["serial_enabled"].is<bool>()) {
        _nmeaOutput->setSerialEnabled(doc["serial_enabled"].as<bool>());
    }
    
    if (doc["serial_port"].is<uint8_t>()) {
        _nmeaOutput->setSerialPort(static_cast<NmeaIOManager::SerialPort>(doc["serial_port"].as<uint8_t>()));
    }
    
    if (doc["tcp_server_enabled"].is<bool>()) {
        _nmeaOutput->setTcpServerEnabled(doc["tcp_server_enabled"].as<bool>());
    }
    
    if (doc["tcp_client_enabled"].is<bool>()) {
        _nmeaOutput->setTcpClientEnabled(doc["tcp_client_enabled"].as<bool>());
    }
    
    if (doc["tcp_client_host"].is<String>()) {
        _nmeaOutput->setTcpClientHost(doc["tcp_client_host"].as<String>());
    }
    
    if (doc["tcp_client_port"].is<uint16_t>()) {
        _nmeaOutput->setTcpClientPort(doc["tcp_client_port"].as<uint16_t>());
    }
    
    if (doc["udp_enabled"].is<bool>()) {
        _nmeaOutput->setUdpEnabled(doc["udp_enabled"].as<bool>());
    }
    
    if (doc["udp_port"].is<uint16_t>()) {
        _nmeaOutput->setUdpPort(doc["udp_port"].as<uint16_t>());
    }
    
    if (doc["input_channel"].is<uint8_t>()) {
        _nmeaOutput->setInputChannel(static_cast<NmeaIOManager::InputChannel>(doc["input_channel"].as<uint8_t>()));
    }
    
    if (doc["input_forward_enabled"].is<bool>()) {
        _nmeaOutput->setInputForwardEnabled(doc["input_forward_enabled"].as<bool>());
    }
    
    if (doc["hdt_enabled"].is<bool>()) {
        _nmeaOutput->setHdtEnabled(doc["hdt_enabled"].as<bool>());
    }
    
    // Save to NVS
    _nmeaOutput->saveConfig();
    
    // Notify output manager of configuration change
    _nmeaOutput->onConfigChanged();
    
    JsonDocument responseDoc;
    responseDoc["success"] = true;
    
    String output;
    serializeJson(responseDoc, output);
    return response->send(200, "application/json", output.c_str());
}

esp_err_t WebServerManager::handleNMEAGetStatus(PsychicRequest* request, PsychicResponse* response) {
    JsonDocument doc;
    
    // Connection status
    doc["tcp_server_clients"] = _nmeaOutput->getTcpServerClientCount();
    doc["tcp_client_connected"] = _nmeaOutput->isTcpClientConnected();
    doc["udp_active"] = _nmeaOutput->isUdpActive();
    
    // Statistics
    doc["dropped_sentences"] = _nmeaOutput->getDroppedSentenceCount();
    
    // GPS data (thread-safe access)
    doc["gps_valid"] = _nmeaOutput->getGPSLocationValid();
    doc["gps_cog"] = _nmeaOutput->getGPSCOG();
    doc["gps_sog"] = _nmeaOutput->getGPSSOG();
    doc["gps_latitude"] = _nmeaOutput->getGPSLatitude();
    doc["gps_longitude"] = _nmeaOutput->getGPSLongitude();
    doc["gps_date_valid"] = _nmeaOutput->getGPSDateValid();
    if (_nmeaOutput->getGPSDateValid()) {
        doc["gps_year"] = _nmeaOutput->getGPSYear();
        doc["gps_month"] = _nmeaOutput->getGPSMonth();
        doc["gps_day"] = _nmeaOutput->getGPSDay();
    }
    doc["gps_time_valid"] = _nmeaOutput->getGPSTimeValid();
    if (_nmeaOutput->getGPSTimeValid()) {
        doc["gps_hour"] = _nmeaOutput->getGPSHour();
        doc["gps_minute"] = _nmeaOutput->getGPSMinute();
        doc["gps_second"] = _nmeaOutput->getGPSSecond();
    }
    
    String output;
    serializeJson(doc, output);
    return response->send(200, "application/json", output.c_str());
}

esp_err_t WebServerManager::handleMountingGet(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.getMountingOffsets();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleMountingSet(PsychicRequest* request, PsychicResponse* response) {
    int httpCode = 200;
    String json = _apiController.postSetMountingOffsets(request->body(), httpCode);
    return response->send(httpCode, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleMountingLevel(PsychicRequest* request, PsychicResponse* response) {
    int httpCode = 200;
    String json = _apiController.postMountingLevel(httpCode);
    return response->send(httpCode, "application/json", json.c_str());
}

esp_err_t WebServerManager::handlePlatformInfo(PsychicRequest* request, PsychicResponse* response) {
    JsonDocument doc;
    
    doc["platform"] = "esp32";
    doc["hasWiFi"] = true;
    doc["hasNMEA"] = true;
    doc["hasOTA"] = true;
    
    // Polling intervals in milliseconds
    JsonObject pollingIntervals = doc["pollingIntervals"].to<JsonObject>();
    pollingIntervals["sensorData"] = 300;
    pollingIntervals["sensorDataBackground"] = 5000;
    pollingIntervals["calibrationStatus"] = 500;
    pollingIntervals["wmmStatus"] = 5000;
    pollingIntervals["deviationStatus"] = 1000;
    
    String output;
    serializeJson(doc, output);
    return response->send(200, "application/json", output.c_str());
}

void WebServerManager::setupOTA() {
    // PsychicHttp has built-in OTA support via PsychicUploadHandler
    PsychicUploadHandler* uploadHandler = new PsychicUploadHandler();
    uploadHandler->onUpload([](PsychicRequest *request, const String& filename, uint64_t index, uint8_t *data, size_t len, bool final) {
        if (!index) {
            Log.noticeln("OTA Update Start: %s", filename.c_str());
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                Update.printError(Serial);
                return ESP_FAIL;
            }
        }
        
        if (Update.write(data, len) != len) {
            Update.printError(Serial);
            return ESP_FAIL;
        }
        
        if (final) {
            if (Update.end(true)) {
                Log.noticeln("OTA Update Success: %l bytes", (long)(index + len));
                return ESP_OK;
            } else {
                Update.printError(Serial);
                return ESP_FAIL;
            }
        }
        
        return ESP_OK;
    });
    
    uploadHandler->onRequest([](PsychicRequest *request, PsychicResponse *response) {
        bool success = !Update.hasError();
        
        if (success) {
            Log.noticeln("OTA: Update successful, restarting in 2 seconds...");
            // Delay restart to allow response to be sent
            delay(2000);
            ESP.restart();
        }
        
        return response->send(200, "text/plain", success ? "OK" : "FAIL");
    });
    
    _server.on("/update", HTTP_POST, uploadHandler);
}

// Deviation endpoint handlers
esp_err_t WebServerManager::handleDeviationGetTable(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.getDeviationTable();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleDeviationSetTable(PsychicRequest* request, PsychicResponse* response) {
    int httpCode = 200;
    String json = _apiController.postDeviationSetTable(request->body(), httpCode);
    return response->send(httpCode, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleDeviationStart(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.postDeviationStart();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleDeviationStop(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.postDeviationStop();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleDeviationStatus(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.getDeviationStatus();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleDeviationReset(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.postDeviationReset();
    return response->send(200, "application/json", json.c_str());
}
esp_err_t WebServerManager::handleWMMGetStatus(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.getWMMStatus();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleWMMSetAuto(PsychicRequest* request, PsychicResponse* response) {
    String json = _apiController.postWMMSetAuto();
    return response->send(200, "application/json", json.c_str());
}

esp_err_t WebServerManager::handleWMMSetManual(PsychicRequest* request, PsychicResponse* response) {
    int httpCode = 200;
    String json = _apiController.postWMMSetManual(request->body(), httpCode);
    return response->send(httpCode, "application/json", json.c_str());
}