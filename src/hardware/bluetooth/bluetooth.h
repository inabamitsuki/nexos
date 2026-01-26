/*
 *  Bluetooth Communication for Heart Rate Monitor
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#ifndef _BLUETOOTH_H
    #define _BLUETOOTH_H

    #include <BLEDevice.h>
    #include <BLEServer.h>
    #include <BLEUtils.h>
    #include <BLE2902.h>

    // Heart Rate Service UUIDs
    #define HEARTRATE_SERVICE_UUID        "0000180D-0000-1000-8000-00805f9b34fb"
    #define HEARTRATE_CHARACTERISTIC_UUID "00002A37-0000-1000-8000-00805f9b34fb"

    class HeartRateMonitorBLE {
    public:
        // Initialize Bluetooth Low Energy
        static bool begin(const std::string& deviceName = "TTGO Heart Monitor");
        
        // Send heart rate data
        static bool sendHeartRate(uint8_t heartRate);
        
        // Check if client is connected
        static bool isConnected();
        
        // Stop BLE advertising
        static void stop();

    private:
        // BLE Server and Characteristic
        static BLEServer* pServer;
        static BLECharacteristic* pHeartRateCharacteristic;
        static BLEAdvertising* pAdvertising;
        
        // Connection callback class
        class ServerCallbacks : public BLEServerCallbacks {
            void onConnect(BLEServer* pServer);
            void onDisconnect(BLEServer* pServer);
        };
    };

#endif // _BLUETOOTH_H