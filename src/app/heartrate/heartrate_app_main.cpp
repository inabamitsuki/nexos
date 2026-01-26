#include "config.h"
#include <Arduino.h>
#include <Wire.h>
#include "hardware/sensors/max30102.h"
#include "heartrate_app.h"
#include "heartrate_app_main.h"
#include "gui/mainbar/app_tile/app_tile.h"
#include "gui/mainbar/main_tile/main_tile.h"
#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "hardware/blectl.h"
#include "hardware/ble/gadgetbridge.h"
#include "utils/bluejsonrequest.h"

// Debugging
#ifdef NATIVE_64BIT
    #include "utils/logging.h"
#else
    #include <Arduino.h>
#endif

// Constants for real-time monitoring
#define REAL_TIME_SAMPLING_RATE   50  // Read sensor every 50ms (20Hz)
#define BPM_UPDATE_INTERVAL      500  // Update display every 500ms
#define BLE_MIN_UPDATE_INTERVAL  500  // Minimum time between BLE updates (ms)

class HeartRateBleUpdater {
private:
    unsigned long last_update = 0;
    unsigned long update_interval = 1000; // Default to 1 second updates
    int32_t last_value = 0;

public:
    HeartRateBleUpdater() = default;

    void setUpdateInterval(unsigned long interval_ms) {
        update_interval = max((unsigned long)BLE_MIN_UPDATE_INTERVAL, interval_ms);
    }

    void update(int32_t heartRate, bool force = false) {
        unsigned long now = millis();
        
        if (force || (now - last_update) >= update_interval) {
            if (notify(heartRate)) {
                last_update = now;
                last_value = heartRate;
            }
        }
    }

protected:
    bool notify(int32_t heartRate) {
        if (heartRate <= 0) {
            log_d("Invalid heart rate value: %d", heartRate);
            return false;
        }
        if (!blectl_get_event(BLECTL_CONNECT)) {
            log_d("BLE not connected, cannot send heart rate");
            return false;
        }
        
        // Send to GadgetBridge
        char msg[64];
        snprintf(msg, sizeof(msg), "\r\n{\"t\":\"hr\", \"bpm\":%d}\r\n", heartRate);
        log_d("Sending BPM to GadgetBridge: %s", msg);
        bool gb_ret = gadgetbridge_send_msg(msg);
        
        // Send via standard BLE Heart Rate Service
        bool hrs_ret = false;
        uint8_t hrsData[2];
        hrsData[0] = 0x06;  // Flags: 8-bit value, sensor contact detected
        hrsData[1] = (uint8_t)heartRate;
        
        if (gadgetbridge_update_hrs(hrsData, sizeof(hrsData))) {
            log_d("Sent to BLE HRS: %d bpm", heartRate);
            hrs_ret = true;
        } else {
            log_d("Failed to send to BLE HRS");
        }
        
        log_d("Heart rate notification - GB: %s, HRS: %s, value: %d bpm", 
              gb_ret ? "OK" : "FAIL", hrs_ret ? "OK" : "FAIL", heartRate);
        return gb_ret || hrs_ret;
    }
};

static HeartRateBleUpdater heartrate_ble_updater;

lv_obj_t *heartrate_app_main_tile = NULL;
lv_style_t heartrate_app_main_style;
lv_obj_t *bpm_label = NULL;
lv_obj_t *status_label = NULL;
lv_obj_t *start_btn = NULL;
lv_obj_t *start_btn_label = NULL;
lv_obj_t *debug_label = NULL;
lv_obj_t *realtime_chart = NULL;
lv_chart_series_t *chart_ir_series = NULL;

MAX30102 max30102;
int32_t heartRate = 0;
bool isMeasuring = false;
uint8_t sensorRetryCount = 0;
const uint8_t MAX_RETRY_COUNT = 5;

// Real-time measurement buffers
uint32_t ir_values[100] = {0};
uint8_t ir_index = 0;
unsigned long last_bpm_update = 0;
unsigned long last_read_time = 0;

lv_task_t *_heartrate_measure_task = NULL;

LV_IMG_DECLARE(exit_32px);
LV_FONT_DECLARE(Ubuntu_72px);
LV_FONT_DECLARE(Ubuntu_48px);

static void exit_heartrate_app_main_event_cb(lv_obj_t *obj, lv_event_t event);
static void start_stop_heart_rate_event_cb(lv_obj_t *obj, lv_event_t event);
void heartrate_measure_task(lv_task_t *task);
bool start_heart_rate_measurement();
void stop_heart_rate_measurement();
void update_debug_info(const char *info);
static bool heartrate_bluetooth_event_cb(EventBits_t event, void *arg);
void update_realtime_chart(uint32_t ir_value);
uint32_t read_ir_value();

void heartrate_app_main_setup(uint32_t tile_num) {
    heartrate_app_main_tile = mainbar_get_tile_obj(tile_num);
    
    lv_style_init(&heartrate_app_main_style);
    lv_style_set_bg_color(&heartrate_app_main_style, LV_STATE_DEFAULT, LV_COLOR_WHITE);

    // Create exit button
    lv_obj_t *exit_btn = lv_imgbtn_create(heartrate_app_main_tile, NULL);
    lv_imgbtn_set_src(exit_btn, LV_BTN_STATE_RELEASED, &exit_32px);
    lv_imgbtn_set_src(exit_btn, LV_BTN_STATE_PRESSED, &exit_32px);
    lv_obj_add_style(exit_btn, LV_IMGBTN_PART_MAIN, &heartrate_app_main_style);
    lv_obj_align(exit_btn, heartrate_app_main_tile, LV_ALIGN_IN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_event_cb(exit_btn, exit_heartrate_app_main_event_cb);

    // Create real-time chart
    realtime_chart = lv_chart_create(heartrate_app_main_tile, NULL);
    lv_obj_set_size(realtime_chart, 220, 100);
    lv_obj_align(realtime_chart, NULL, LV_ALIGN_IN_TOP_MID, 0, 10);
    lv_chart_set_type(realtime_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_range(realtime_chart, 0, 100000);
    lv_chart_set_point_count(realtime_chart, 100);
    lv_chart_set_update_mode(realtime_chart, LV_CHART_UPDATE_MODE_SHIFT);
    chart_ir_series = lv_chart_add_series(realtime_chart, LV_COLOR_RED);
    
    // Create BPM label
    bpm_label = lv_label_create(heartrate_app_main_tile, NULL);
    lv_label_set_text(bpm_label, "BPM: --");
    lv_obj_align(bpm_label, realtime_chart, LV_ALIGN_OUT_BOTTOM_MID, -100, 10);
    lv_obj_set_style_local_text_font(bpm_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, &Ubuntu_48px);

    // Create status label
    status_label = lv_label_create(heartrate_app_main_tile, NULL);
    lv_label_set_text(status_label, "Press to start measurement");
    lv_obj_align(status_label, bpm_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 15);

    // Create debug label
    debug_label = lv_label_create(heartrate_app_main_tile, NULL);
    lv_label_set_text(debug_label, "Debug: Ready");
    lv_obj_align(debug_label, status_label, LV_ALIGN_OUT_BOTTOM_MID, 0, 20);
    lv_obj_set_style_local_text_color(debug_label, LV_LABEL_PART_MAIN, LV_STATE_DEFAULT, LV_COLOR_RED);

    // Create start/stop button
    start_btn = lv_btn_create(heartrate_app_main_tile, NULL);
    lv_obj_set_width(start_btn, 120);
    lv_obj_align(start_btn, NULL, LV_ALIGN_IN_BOTTOM_RIGHT, -20, -10);
    lv_obj_set_event_cb(start_btn, start_stop_heart_rate_event_cb);
    start_btn_label = lv_label_create(start_btn, NULL);
    lv_label_set_text(start_btn_label, "Start");

    // Register Bluetooth callbacks
    gadgetbridge_register_cb(GADGETBRIDGE_CONNECT | GADGETBRIDGE_JSON_MSG, heartrate_bluetooth_event_cb, "heart rate monitor");

    // Initialize I2C with optimized settings for better performance
    Wire.begin(21, 22);
    Wire.setClock(400000);  // Increased I2C clock for faster communication
    
    delay(100);
    if (!max30102.checkConnection()) {
        lv_label_set_text(status_label, "Sensor not detected");
        update_debug_info("No sensor at 0x57");
    } else {
        update_debug_info("Sensor found!");
    }
}

static bool heartrate_bluetooth_event_cb(EventBits_t event, void *arg) {
    bool retval = false;
    
    switch(event) {
        case GADGETBRIDGE_CONNECT: 
            // Try to refresh heart rate value on (re)connect
            if (isMeasuring) {
                heartrate_ble_updater.update(heartRate, true);
            }
            retval = true;
            break;
            
        case GADGETBRIDGE_JSON_MSG: {
            BluetoothJsonRequest &request = *(BluetoothJsonRequest*)arg;

            if (request.isEqualKeyValue("t","hr")) {
                if (request.containsKey("interval")) {
                    // Adjust update interval if requested - convert to milliseconds
                    unsigned long interval = request["interval"].as<unsigned long>() * 1000;
                    heartrate_ble_updater.setUpdateInterval(interval);
                    log_d("Heart rate update interval set to %lu ms", interval);
                }
                
                // Check for real-time mode request
                if (request.containsKey("realtime") && request["realtime"].as<bool>()) {
                    // Set minimum update interval for real-time mode
                    heartrate_ble_updater.setUpdateInterval(BLE_MIN_UPDATE_INTERVAL);
                    log_d("Real-time mode enabled");
                }
            }
            retval = true;
            break;
        }
    }

    return retval;
}

static void exit_heartrate_app_main_event_cb(lv_obj_t *obj, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) {
        stop_heart_rate_measurement();
        mainbar_jump_to_maintile(LV_ANIM_OFF);
    }
}

static void start_stop_heart_rate_event_cb(lv_obj_t *obj, lv_event_t event) {
    if (event == LV_EVENT_CLICKED) {
        if (!isMeasuring) {
            if (start_heart_rate_measurement()) {
                lv_label_set_text(start_btn_label, "Stop");
            }
        } else {
            stop_heart_rate_measurement();
            lv_label_set_text(start_btn_label, "Start");
        }
    }
}

// Helper function to manually read an IR value from the sensor
uint32_t read_ir_value() {
    uint8_t fifoData[6];
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(REG_FIFO_DATA);
    Wire.endTransmission(false);
    Wire.requestFrom(MAX30102_I2C_ADDRESS, 6);
    
    for(uint8_t i = 0; i < 6; i++) {
        fifoData[i] = Wire.available() ? Wire.read() : 0;
    }
    
    // Extract and return the IR value (last 3 bytes of the 6-byte packet)
    uint32_t irValue = ((uint32_t)fifoData[3] << 16) | ((uint32_t)fifoData[4] << 8) | fifoData[5];
    return irValue;
}

bool start_heart_rate_measurement() {
    lv_label_set_text(status_label, "Initializing sensor...");
    update_debug_info("Starting...");
    
    if (!max30102.checkConnection()) {
        update_debug_info("No sensor found");
        lv_label_set_text(status_label, "Sensor not found!");
        return false;
    }
    
    if (!max30102.begin()) {
        sensorRetryCount++;
        char debug_msg[40];
        snprintf(debug_msg, sizeof(debug_msg), "Init fail #%d", sensorRetryCount);
        update_debug_info(debug_msg);
        
        if (sensorRetryCount < MAX_RETRY_COUNT) {
            lv_label_set_text(status_label, "Sensor error. Retrying...");
            delay(250);
            return start_heart_rate_measurement();
        } else {
            lv_label_set_text(status_label, "Sensor init failed!");
            update_debug_info("Max retries reached");
            sensorRetryCount = 0;
            return false;
        }
    }
    
    sensorRetryCount = 0;
    update_debug_info("Sensor initialized OK");
    
    // Instead of using setFIFOAlmostFull, we'll directly configure the registers
    // for optimal real-time performance, setting the interrupt on 1 sample
    
    // REG_FIFO_CONFIG: Sample average = 1, FIFO rollover = false, almost full = 1 sample (0xF)
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(REG_FIFO_CONFIG);
    Wire.write(0x0F);  // No averaging, FIFO almost full at 1 sample
    Wire.endTransmission();
    
    // REG_SPO2_CONFIG: Set to fastest sampling (0x27)
    // SPO2 settings: ADC range = 4096, SR = 100Hz, Pulse width = 411us
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(REG_SPO2_CONFIG);
    Wire.write(0x27);
    Wire.endTransmission();
    
    // Configure LED brightness for optimal signal
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(REG_LED1_PA);  // Red LED current
    Wire.write(0x3F);         // Increase to maximum brightness
    Wire.endTransmission();
    
    Wire.beginTransmission(MAX30102_I2C_ADDRESS);
    Wire.write(REG_LED2_PA);  // IR LED current
    Wire.write(0x3F);         // Increase to maximum brightness
    Wire.endTransmission();
    
    // Start heart rate detection
    if (!max30102.startHeartRateSensor()) {
        lv_label_set_text(status_label, "Failed to start sensor");
        update_debug_info("HR mode start failed");
        return false;
    }
    
    // Reset all variables
    isMeasuring = true;
    ir_index = 0;
    memset(ir_values, 0, sizeof(ir_values));
    lv_chart_clear_series(realtime_chart, chart_ir_series);
    
    lv_label_set_text(status_label, "Measuring heart rate...");
    update_debug_info("Ready for reading");
    
    // Create a higher frequency task for real-time monitoring
    _heartrate_measure_task = lv_task_create(heartrate_measure_task, REAL_TIME_SAMPLING_RATE, LV_TASK_PRIO_HIGH, NULL);
    
    return true;
}

void stop_heart_rate_measurement() {
    if (isMeasuring) {
        isMeasuring = false;
        lv_label_set_text(status_label, "Measurement stopped");
        lv_label_set_text(bpm_label, "BPM: --");
        update_debug_info("Stopped");
        
        max30102.shutdown();
        
        if (_heartrate_measure_task) {
            lv_task_del(_heartrate_measure_task);
            _heartrate_measure_task = NULL;
        }
    }
}

void update_realtime_chart(uint32_t ir_value) {
    // Scale IR value to fit chart range
    uint32_t scaled_value = min(ir_value, (uint32_t)100000);
    
    // Store value in buffer for display
    ir_values[ir_index] = ir_value;
    ir_index = (ir_index + 1) % 100;
    
    // Update chart with scaled value
    lv_chart_set_next(realtime_chart, chart_ir_series, scaled_value);
}

void heartrate_measure_task(lv_task_t *task) {
    static int failCount = 0;
    static bool finger_detected = false;
    static unsigned long lastHeartRateCalc = 0;
    
    // Get a new reading from sensor (using our own direct read function)
    uint32_t irValue = max30102.getLastIRValue();
    if (irValue == 0) {
        // If we get 0, try to read directly
        irValue = read_ir_value();
    }
    
    // Update real-time chart
    update_realtime_chart(irValue);
    
    // Update BPM calculation at a fixed interval
    unsigned long currentTime = millis();
    if (currentTime - last_bpm_update >= BPM_UPDATE_INTERVAL) {
        last_bpm_update = currentTime;
        
        // Get heart rate
        int32_t tempHeartRate = max30102.getHeartRate();
        
        char debug_msg[60];
        snprintf(debug_msg, sizeof(debug_msg), "IR: %lu", irValue);
        update_debug_info(debug_msg);
        
        // Detect finger presence based on IR value
        if (irValue > 30000) {
            if (!finger_detected) {
                finger_detected = true;
                lv_label_set_text(status_label, "Finger detected, measuring...");
            }
        } else {
            if (finger_detected) {
                finger_detected = false;
                lv_label_set_text(status_label, "Place finger on sensor");
            }
        }
        
        if (tempHeartRate > 0) {
            failCount = 0;
            heartRate = tempHeartRate;
            
            char bpm_text[20];
            snprintf(bpm_text, sizeof(bpm_text), "BPM: %ld", heartRate);
            lv_label_set_text(bpm_label, bpm_text);
            
            if (finger_detected) {
                lv_label_set_text(status_label, "Reading successful");
            }
            
            // Update BLE with new heart rate
            heartrate_ble_updater.update(heartRate);
        } else if (tempHeartRate == -1) {
            failCount += 2;
            lv_label_set_text(status_label, "Sensor connection lost");
            update_debug_info("Connection error");
        } else {
            failCount++;
            
            if (irValue < 10000) {
                lv_label_set_text(status_label, "Place finger firmly on sensor");
            } else if (irValue > 300000) {
                lv_label_set_text(status_label, "Too much pressure - press lighter");
            } else if (finger_detected) {
                lv_label_set_text(status_label, "Hold still... detecting");
            }
            
            char debug_msg[40];
            snprintf(debug_msg, sizeof(debug_msg), "No reading (%d)", failCount);
            update_debug_info(debug_msg);
        }
        
        if (failCount >= 10) {
            lv_label_set_text(status_label, "Restarting sensor...");
            update_debug_info("Auto-restart");
            
            max30102.shutdown();
            delay(200);
            
            if (max30102.begin() && max30102.startHeartRateSensor()) {
                lv_label_set_text(status_label, "Sensor restarted");
                update_debug_info("Restart OK");
            } else {
                lv_label_set_text(status_label, "Sensor restart failed");
                update_debug_info("Restart failed");
                stop_heart_rate_measurement();
                lv_label_set_text(start_btn_label, "Start");
            }
            
            failCount = 0;
        }
    }
}

void update_debug_info(const char *info) {
    if (debug_label != NULL) {
        char debug_text[50];
        snprintf(debug_text, sizeof(debug_text), "Debug: %s", info);
        lv_label_set_text(debug_label, debug_text);
    }
}