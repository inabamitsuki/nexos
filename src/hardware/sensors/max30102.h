// max30102.h
#ifndef _MAX30102_H
#define _MAX30102_H

#include <Wire.h>

#define MAX30102_I2C_ADDRESS       0x57

// Register addresses
#define REG_INTR_STATUS_1           0x00
#define REG_INTR_STATUS_2           0x01
#define REG_INTR_ENABLE_1           0x02
#define REG_INTR_ENABLE_2           0x03
#define REG_FIFO_WR_PTR             0x04
#define REG_OVF_COUNTER             0x05
#define REG_FIFO_RD_PTR             0x06
#define REG_FIFO_DATA               0x07
#define REG_FIFO_CONFIG             0x08
#define REG_MODE_CONFIG             0x09
#define REG_SPO2_CONFIG             0x0A
#define REG_LED1_PA                 0x0C
#define REG_LED2_PA                 0x0D
#define REG_MULTI_LED_CTRL1         0x11
#define REG_MULTI_LED_CTRL2         0x12
#define REG_TEMP_INTR               0x1F
#define REG_TEMP_FRAC               0x20
#define REG_TEMP_CONFIG             0x21
#define REG_PROX_INT_THRESH         0x30
#define REG_REV_ID                  0xFE
#define REG_PART_ID                 0xFF

// Size of moving average filter for heart rate
#define HR_FILTER_SIZE 5
// Size of buffer for peak detection
#define PEAK_BUFFER_SIZE 16

class MAX30102 {
public:
    MAX30102();
    bool begin();
    bool checkConnection();
    void shutdown();
    bool startHeartRateSensor();
    int32_t getHeartRate();
    uint32_t getLastIRValue();
    float getConfidence();  // New method to get reading confidence

private:
    void writeRegister(uint8_t reg, uint8_t value);
    uint8_t readRegister(uint8_t reg);
    void readFIFO(uint8_t *data, uint8_t length);
    void updatePeakDetection(uint32_t irValue);
    bool detectPeak(uint32_t irValue);
    uint32_t calculateHeartRate();
    
    uint32_t lastIRValue;
    
    // Enhanced heart rate detection
    uint32_t irBuffer[PEAK_BUFFER_SIZE];  // Larger buffer for better detection
    uint8_t bufferIndex;
    uint32_t valleyTime;
    uint32_t lastPeakTime;
    uint32_t peakInterval[HR_FILTER_SIZE];
    uint8_t peakIntervalIndex;
    bool isPeak;
    uint32_t minValue;
    uint32_t maxValue;
    uint32_t threshold;
    uint8_t peakCount;
    
    // Filtered heart rate values
    int32_t heartRateBuffer[HR_FILTER_SIZE];
    uint8_t heartRateIndex;
    int32_t filteredHeartRate;
    
    // Confidence metric (0.0-1.0)
    float confidence;
    
    // Signal quality detection
    uint32_t signalRangeHistory[4];
    uint8_t signalRangeIndex;
};

#endif // _MAX30102_H