/*
 *  Bluetooth Communication for Heart Rate Monitor
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#include "bluetooth.h"

// Static member initializations
BLEServer* HeartRateMonitorBLE::pServer = nullptr;
BLECharacteristic* HeartRateMonitorBLE::pHeartRateCharacteristic = nullptr;
BLEAdvertising* HeartRateMonitorBLE::pAdvertising = nullptr;

bool HeartRateMonitorBLE::begin(const std::string& deviceName) {
    // Initialize BLE
    BLEDevice::init(deviceName);

    // Create BLE Server
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Create Heart Rate Service
    BLEService* pHeartRateService = pServer->createService(HEARTRATE_SERVICE_UUID);

    // Create Heart Rate Characteristic
    pHeartRateCharacteristic = pHeartRateService->createCharacteristic(
        HEARTRATE_CHARACTERISTIC_UUID,
        BLECharacteristic::PROPERTY_NOTIFY
    );

    // Add descriptor for notifications
    pHeartRateCharacteristic->addDescriptor(new BLE2902());

    // Start the service
    pHeartRateService->start();

    // Start advertising
    pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(HEARTRATE_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);  // helps with iPhone connections
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();

    return true;
}

bool HeartRateMonitorBLE::sendHeartRate(uint8_t heartRate) {
    // Check if characteristic and server exist
    if (!pHeartRateCharacteristic || !pServer) {
        return false;
    }

    // Check if client is connected
    if (!isConnected()) {
        return false;
    }

    // Send heart rate as uint8_t
    pHeartRateCharacteristic->setValue(&heartRate, 1);
    pHeartRateCharacteristic->notify();

    return true;
}

bool HeartRateMonitorBLE::isConnected() {
    return pServer && pServer->getConnectedCount() > 0;
}

void HeartRateMonitorBLE::stop() {
    BLEDevice::stopAdvertising();
}

// Connection Callbacks Implementation
void HeartRateMonitorBLE::ServerCallbacks::onConnect(BLEServer* pServer) {
    // Optional: Add connection handling logic
    // For example, update a status LED or log connection
}

void HeartRateMonitorBLE::ServerCallbacks::onDisconnect(BLEServer* pServer) {
    // Restart advertising when a client disconnects
    BLEDevice::startAdvertising();
}