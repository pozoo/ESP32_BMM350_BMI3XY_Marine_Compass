#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <PsychicHttp.h>
#include <ArduinoJson.h>
#include "Sensor_9_DOF.h"
#include "WifiManager.h"
#include "CalibrationManager.h"
#include "NmeaIOManager.h"
#include "Deviation.h"
#include "WMM.h"
#include "WebAPIController.h"

class WebServerManager {
public:
    WebServerManager(Sensor_9_DOF* sensor, WiFiManager* wifiManager, CalibrationManager* calibrationManager, NmeaIOManager* nmeaOutput, Deviation* deviation, WMM* wmm);
    
    // Initialize and start web server
    void begin();
    
    // Set port (default: 80)
    void setPort(uint16_t port) { _port = port; }
    
private:
    PsychicHttpServer _server;
    Sensor_9_DOF* _sensor;
    WiFiManager* _wifiManager;
    CalibrationManager* _calibrationManager;
    NmeaIOManager* _nmeaOutput;
    Deviation* _deviation;
    WMM* _wmm;
    uint16_t _port;
    
    // HTTP-agnostic API controller
    WebAPIController _apiController;
    
    // Endpoint handlers
    esp_err_t handleRoot(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleData(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleCalibrateStart(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleCalibrateStop(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleCalibrateStatus(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleCalibrateCompute(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleCalibrateInfo(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleWiFiConfig(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleWiFiScan(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleWiFiStatus(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleGetDeclination(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleSetDeclination(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleNMEAGetConfig(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleNMEASetConfig(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleNMEAGetStatus(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleMountingGet(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleMountingSet(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleMountingLevel(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handlePlatformInfo(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleDeviationGetTable(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleDeviationSetTable(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleDeviationStart(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleDeviationStop(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleDeviationStatus(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleDeviationReset(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleWMMGetStatus(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleWMMSetAuto(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleWMMSetManual(PsychicRequest* request, PsychicResponse* response);
    esp_err_t handleNotFound(PsychicRequest* request, PsychicResponse* response);
    
    // Helper functions
    String getSensorDataJSON();
    void setupRoutes();
    void setupOTA();
};

#endif // WEB_SERVER_H
