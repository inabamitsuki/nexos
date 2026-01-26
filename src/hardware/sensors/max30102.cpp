// max30102.cpp
#include "max30102.h"
#include <Arduino.h>

MAX30102::MAX30102() : 
    lastIRValue(0), 
    bufferIndex(0), 
    valleyTime(0), 
    lastPeakTime(0), 
    peakIntervalIndex(0), 
    isPeak(false),
    minValue(100000), 
    maxValue(0), 
    threshold(0),
    peakCount(0),
    heartRateIndex(0),
    filteredHeartRate(0),
    confidence(0.0),
    signalRangeIndex(0)
{
    // Initialize arrays
    for (int i = 0; i < PEAK_BUFFER_SIZE; i++) {
        irBuffer[i] = 0;
    }
    
    for (int i = 0; i < HR_FILTER_SIZE; i++) {
        peakInterval[i] = 0;
        heartRateBuffer[i] = 0;
    }
    
    for (int i = 0; i < 4; i++) {
        signalRangeHistory[i] = 0;
    }
}

bool MAX30102::begin() {
    // Check if sensor is responding
    if (!checkConnection()) {
        Serial.println("MAX30102 sensor not found or not responding");
        return false;
    }
    
    // Initialize the sensor
    Serial.println("Resetting MAX30102");
    writeRegister(REG_MODE_CONFIG, 0x40);  // Reset sensor
    delay(500);  // Increased delay after reset

    // Check if reset completed
    uint8_t modeConfig = readRegister(REG_MODE_CONFIG);
    Serial.print("Mode config after reset: 0x");
    Serial.println(modeConfig, HEX);
    
    if (modeConfig & 0x40) {
        Serial.println("MAX30102 reset failed - reset bit still set");
        return false;
    }
    
    // Configure sensor
    Serial.println("Configuring MAX30102 registers");
    
    // Configure interrupts
    writeRegister(REG_INTR_ENABLE_1, 0xC0);  // FIFO almost full, data ready
    writeRegister(REG_INTR_ENABLE_2, 0x00);
    
    // Configure FIFO - Sample avg = 8, FIFO rollover = false, FIFO almost full = 17 samples
    writeRegister(REG_FIFO_CONFIG, 0x5F);
    
    // Configure for heart rate mode
    writeRegister(REG_MODE_CONFIG, 0x03);   // Heart rate mode
    
    // Configure SPO2 settings: ADC range = 8192, SR = 100Hz, Pulse width = 411us
    writeRegister(REG_SPO2_CONFIG, 0x67);
    
    // Set LED current - optimized for most skin types
    writeRegister(REG_LED1_PA, 0x32);  // Red LED current (~10mA)
    writeRegister(REG_LED2_PA, 0x32);  // IR LED current (~10mA)

    // Verify settings were applied
    uint8_t modeVerify = readRegister(REG_MODE_CONFIG);
    Serial.print("Mode config after setup: 0x");
    Serial.println(modeVerify, HEX);
    
    if ((modeVerify & 0x07) != 0x03) {
        Serial.println("Failed to set heart rate mode");
        return false;
    }
    
    // Reset all heart rate detection variables
    bufferIndex = 0;
    valleyTime = 0;
    lastPeakTime = 0;
    peakIntervalIndex = 0;
    isPeak = false;
    minValue = 100000;
    maxValue = 0;
    threshold = 0;
    peakCount = 0;
    heartRateIndex = 0;
    filteredHeartRate = 0;
    confidence = 0.0;
    
    for (int i = 0; i < PEAK_BUFFER_SIZE; i++) {
        irBuffer[i] = 0;
    }
    
    for (int i = 0; i < HR_FILTER_SIZE; i++) {
        peakInterval[i] = 0;
        heartRateBuffer[i] = 0;
    }
    
    Serial.println("MAX30102 configuration complete");
    return true;
}

bool MAX30102::checkConnection() {
    // Try to read the part ID register
    uint8_t partID = readRegister(REG_PART_ID);
    
    Serial.print("MAX30102 Part ID read: 0x");
    Serial.println(partID, HEX);
    
    if (partID == 0x15) {
        return true;
    }
    
    // Try a second time in case of a glitch
    delay(10);
    partID = readRegister(REG_PART_ID);
    
    return (partID == 0x15);
}

void MAX30102::shutdown() {
    uint8_t currentMode = readRegister(REG_MODE_CONFIG);
    writeRegister(REG_MODE_CONFIG, currentMode | 0x80);
    
    // Reset variables
    confidence = 0.0;
    filteredHeartRate = 0;
}

bool MAX30102::startHeartRateSensor() {
    if (!checkConnection()) {
        Serial.println("Cannot start heart rate - sensor not connected");
        return false;
    }
    
    writeRegister(REG_MODE_CONFIG, 0x03);   // Heart rate mode
    
    uint8_t modeConfig = readRegister(REG_MODE_CONFIG);
    return ((modeConfig & 0x07) == 0x03);
}

void MAX30102::updatePeakDetection(uint32_t irValue) {
    // Store in circular buffer
    irBuffer[bufferIndex] = irValue;
    bufferIndex = (bufferIndex + 1) % PEAK_BUFFER_SIZE;
    
    // Update min/max
    if (irValue > maxValue) {
        maxValue = irValue;
    }
    
    if (irValue < minValue) {
        minValue = irValue;
    }
    
    // Dynamically adjust threshold based on signal range
    uint32_t range = maxValue - minValue;
    
    // Update the signal range history
    signalRangeHistory[signalRangeIndex] = range;
    signalRangeIndex = (signalRangeIndex + 1) % 4;
    
    // Calculate the average range over the last few readings
    uint32_t avgRange = 0;
    for (int i = 0; i < 4; i++) {
        avgRange += signalRangeHistory[i];
    }
    avgRange /= 4;
    
    // Update confidence based on signal range and stability
    if (avgRange > 20000) {
        confidence = 1.0; // Strong signal
    } else if (avgRange > 10000) {
        confidence = 0.7; // Moderate signal
    } else if (avgRange > 5000) {
        confidence = 0.4; // Weak signal
    } else {
        confidence = 0.0; // No reliable signal
    }
    
    // Adjust threshold to 50% of the range
    threshold = minValue + (range * 0.50);
    
    // Every 1 second, gradually reduce the max and increase the min
    // This helps adapt to changing baselines
    static unsigned long lastAdjustTime = 0;
    unsigned long currentTime = millis();
    
    if (currentTime - lastAdjustTime > 1000) {
        maxValue = maxValue * 0.95;
        minValue = minValue * 1.05;
        lastAdjustTime = currentTime;
    }
}

bool MAX30102::detectPeak(uint32_t irValue) {
    static bool wasRising = false;
    static uint32_t lastValue = 0;
    bool isPeak = false;
    
    if (irValue > lastValue) {
        wasRising = true;
    } else if (wasRising && irValue < lastValue) {
        // We just hit a peak if we were rising and now we're falling
        if (lastValue > threshold) {
            unsigned long currentTime = millis();
            
            // Only count this as a peak if enough time has passed since the last peak
            if (currentTime - lastPeakTime > 300) { // Minimum 300ms between peaks (200 BPM max)
                peakCount++;
                lastPeakTime = currentTime;
                isPeak = true;
                
                // Calculate interval since last peak
                if (valleyTime > 0) {
                    uint32_t interval = currentTime - valleyTime;
                    
                    // Store interval for averaging
                    if (interval > 300 && interval < 2000) { // 30-200 BPM range
                        peakInterval[peakIntervalIndex] = interval;
                        peakIntervalIndex = (peakIntervalIndex + 1) % HR_FILTER_SIZE;
                    }
                }
            }
        }
        wasRising = false;
    }
    
    // If we're at a valley (local minimum), update valleyTime
    static bool wasFalling = false;
    if (irValue < lastValue) {
        wasFalling = true;
    } else if (wasFalling && irValue > lastValue) {
        valleyTime = millis();
        wasFalling = false;
    }
    
    lastValue = irValue;
    return isPeak;
}

uint32_t MAX30102::calculateHeartRate() {
    // Calculate average peak interval
    uint32_t totalInterval = 0;
    uint8_t validIntervals = 0;
    
    for (int i = 0; i < HR_FILTER_SIZE; i++) {
        if (peakInterval[i] > 300 && peakInterval[i] < 2000) {
            totalInterval += peakInterval[i];
            validIntervals++;
        }
    }
    
    // Need at least 2 valid intervals for a reliable calculation
    if (validIntervals < 2) {
        return 0;
    }
    
    // Calculate BPM based on average interval
    uint32_t avgInterval = totalInterval / validIntervals;
    uint32_t bpm = 60000 / avgInterval;
    
    // Ensure it's in a reasonable range
    if (bpm < 40 || bpm > 200) {
        return 0;
    }
    
    return bpm;
}

int32_t MAX30102::getHeartRate() {
    if (!checkConnection()) {
        Serial.println("Cannot get heart rate - sensor not connected");
        return -1;
    }
    
    // Read from FIFO
    uint8_t readPtr = readRegister(REG_FIFO_RD_PTR);
    uint8_t writePtr = readRegister(REG_FIFO_WR_PTR);
    
    // Calculate available samples
    int availableSamples = (writePtr - readPtr) & 0x1F;
    if (availableSamples == 0) {
        return filteredHeartRate;  // Return last filtered value if no new data
    }
    
    // Get the latest sample
    uint8_t fifoData[6];
    readFIFO(fifoData, 6);
    
    // Extract IR value (last 3 bytes of the 6-byte packet)
    uint32_t irValue = ((uint32_t)fifoData[3] << 16) | ((uint32_t)fifoData[4] << 8) | fifoData[5];
    lastIRValue = irValue;
    
    // Basic finger detection - requires stable signal
    if (irValue < 5000) {
        // Reset detection when finger is removed
        minValue = 100000;
        maxValue = 0;
        filteredHeartRate = 0;
        confidence = 0.0;
        return 0;
    }
    
    // Process the new IR value for peak detection
    updatePeakDetection(irValue);
    bool newPeak = detectPeak(irValue);
    
    // Calculate heart rate if we have enough data
    if (newPeak) {
        uint32_t bpm = calculateHeartRate();
        
        if (bpm > 0) {
            // Add to the moving average filter
            heartRateBuffer[heartRateIndex] = bpm;
            heartRateIndex = (heartRateIndex + 1) % HR_FILTER_SIZE;
            
            // Calculate the new filtered heart rate
            int32_t sum = 0;
            uint8_t count = 0;
            
            for (int i = 0; i < HR_FILTER_SIZE; i++) {
                if (heartRateBuffer[i] > 0) {
                    sum += heartRateBuffer[i];
                    count++;
                }
            }
            
            if (count > 0) {
                filteredHeartRate = sum / count;
            }
        }
    }
    
    // Return the filtered heart rate
    return filteredHeartRate;
}

uint32_t MAX30102::getLastIRValue() {
    return lastIRValue;
}

float MAX30102::getConfidence() {
    return confidence;
}

void MAX30102::writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t MAX30102::readRegister(uint8_t reg) {
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(MAX30102_I2C_ADDRESS, 1);
    return Wire.available() ? Wire.read() : 0xFF;
}

void MAX30102::readFIFO(uint8_t *data, uint8_t length) {
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(REG_FIFO_DATA);
    Wire.endTransmission(false);
    Wire.requestFrom(MAX30102_I2C_ADDRESS, length);
    for(uint8_t i = 0; i < length; i++) {
        data[i] = Wire.available() ? Wire.read() : 0;
    }
}