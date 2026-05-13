#include <Arduino.h>
#include <Wire.h>
#include <ArduinoLog.h>
#include "Sensor_9_DOF.h"
#include "BMP390.h"
#include "WifiManager.h"
#include "WebServerManager.h"
#include "CalibrationManager.h"
#include "NmeaIOManager.h"
#include "NMEA0183.h"
#include "I2C_Helper.h"


// Create sensor instance
Sensor_9_DOF sensor;
BMP390 pressureSensor;
WiFiManager wifiManager;
CalibrationManager calibrationManager(sensor);
NMEA0183 nmea;
NmeaIOManager nmeaOutput(&nmea);
WebServerManager webServer(&sensor, &wifiManager, &calibrationManager, &nmeaOutput, &sensor.getDeviation(), &sensor.getWMM());

void setup() {
    Serial.begin(115200);
    delay(2000);
    unsigned long start = millis();
    while(!Serial && (millis() - start < 2000)) {  // 5 second timeout
        delay(10);
    }
    
    // Initialize logging
    Log.begin(LOG_LEVEL_INFO, &Serial);
    
    Log.noticeln("Starting sensor system");
    
    // I2C bus recovery for stuck sensors (when they stay powered through reset)
    recoverI2CBus(SDA, SCL);
  

    // Initialize I2C bus with 400kHz clock speed
    Wire.begin();
    Wire.setClock(400000);
    delay(10); // let bus stabilize, not sure if required

    // Initialize sensors
    if (!sensor.begin()) {
        Log.fatalln("Failed to initialize 9-DOF sensors!");
        while(1) {
            delay(1000);
        }
    }

    if (!pressureSensor.begin()) {
        Log.fatalln("Failed to initialize BMP390 pressure sensor!");
        while(1) {
            delay(1000);
        }
    }
    
    // Load calibrations from NVS (if available)
    calibrationManager.loadAllCalibrations();
    sensor.getDeviation().loadFromNVS();
    sensor.getWMM().loadFromNVS();

    sensor.initFusion();

    Log.verboseln("Sensors initialized successfully");
    
    // Initialize WiFi first (required for TCP/UDP operations)
    wifiManager.begin();
    
    // Initialize NMEA output (after WiFi to avoid TCP/IP stack crashes)
    nmeaOutput.loadConfig();
    nmeaOutput.begin();
    
    webServer.begin();
}



unsigned long fusion_counter = 0;
unsigned long last_nmea_output = 0;
unsigned long last_deviation_wmm_update = 0;

unsigned long time_fusion_last = 0;
uint16_t fusion_print_interval = 1000; // Print every 1000 fusion updates

void loop_fusion() {
    
    if (sensor.fusionUpdate()) {
        fusion_counter++;
        if (fusion_counter >= 1000) {
            // sensor.printFusionFlags();
            // sensor.printFusionInternalStates();
            unsigned long delta = (micros() - time_fusion_last) / fusion_print_interval;
            time_fusion_last = micros();
            unsigned long sensor_micros = sensor.getDeltaUs();
            long sleep_time = sensor.getWaitTimeToNextRead_ms();
            Log.verboseln("fusion count: %l, delta(us): %l, delta_us (us): %l, time to next read (ms): %l, ticks: %d", fusion_counter, delta, sensor_micros, sleep_time, pdMS_TO_TICKS(sleep_time));
            fusion_counter = 0;
        }
    }
    
    pressureSensor.readPressureAndTemperature();
    
    // Send NMEA output at configured rate
    unsigned long now = millis();
    if (now - last_nmea_output >= nmeaOutput.getUpdateIntervalMs()) {
        last_nmea_output = now;
        
       Sensor_9_DOF::SensorResults results;
       sensor.getSensorResults(results); 

        // Batch all NMEA sentences together in one buffer
        char batchBuffer[nmea.maxNMEASentenceLength * 6];
        nmea.batchCompassPressureUpdate(batchBuffer, sizeof(batchBuffer), 
                               results.headingMagDeviationCorrected, results.headingTrueDeviationCorrected, 
                               results.roll, results.pitch, pressureSensor.getPressureBar(),
                               results.rateOfTurn,
                               nmeaOutput.isHdtEnabled(), true);
        nmeaOutput.broadcast(batchBuffer);

        // Update deviation calibration with GPS data (if calibration active) - once per second, lower rate than NMEA output
        if (now - last_deviation_wmm_update >= 1000) {
            last_deviation_wmm_update = now;
            if (nmeaOutput.getGPSLocationValid() && nmeaOutput.getGPSDateValid() && nmeaOutput.getGPSTimeValid()) {
                sensor.getDeviation().update(
                    results.headingMag,                          // Uncorrected magnetic heading
                    nmeaOutput.getGPSCOG(),                      // GPS COG (true heading)
                    nmeaOutput.getGPSSOG(),                      // GPS speed
                    sensor.getWMM().getMagneticDeclination()     // Current WMM declination
                );
                sensor.getWMM().updateLocation(
                    nmeaOutput.getGPSLatitude(),
                    nmeaOutput.getGPSLongitude(),
                    nmeaOutput.getGPSYear(),
                    nmeaOutput.getGPSMonth(),
                    nmeaOutput.getGPSDay(),
                    nmeaOutput.getGPSHour(),
                    nmeaOutput.getGPSMinute(),
                    nmeaOutput.getGPSSecond(),
                    0
                );

            }
        }
    }
    
    calibrationManager.update();
}

int counterSleep = 0;
unsigned long millis_slept = 0;
unsigned long millis_start = 0;

void loop() {
    loop_fusion();
    wifiManager.update();  // Handle reconnections
    nmeaOutput.update();   // Handle TCP client reconnection and UDP broadcast address updates

    // all done, sleep until next read is due
    unsigned long sleep_time = sensor.getWaitTimeToNextRead_ms();
    // millis_slept += sleep_time;
    // if (++counterSleep >= 10000) {
    //     counterSleep = 0;
    //     unsigned long millis_elapsed = millis() - millis_start; 
    //     printf("Slept for %lu ms of total: %lu ms, fraction: %f\n", millis_slept, millis_elapsed, (float)millis_slept / millis_elapsed);
    //     millis_slept = 0;
    //     millis_start = millis();
    // }
    delay(sleep_time);
}

