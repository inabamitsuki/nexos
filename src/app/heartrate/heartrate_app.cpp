/*
 *  Heart Rate Monitor App for TTGO T-Watch 2019
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#include "config.h"
#include <Arduino.h>
#include <TTGO.h>

#include "heartrate_app.h"
#include "heartrate_app_main.h"

#include "gui/mainbar/mainbar.h"
#include "gui/statusbar.h"
#include "gui/app.h"
#include "gui/widget.h"

uint32_t heartrate_app_main_tile_num;

// app icon
icon_t *heartrate_app = NULL;

// Declare images and fonts
LV_IMG_DECLARE(heartrate_app_64px);
LV_IMG_DECLARE(info_1_16px);

// Callback for entering the app
static void enter_heartrate_app_event_cb(lv_obj_t *obj, lv_event_t event);

static int __attribute__((unused)) registed = app_autocall_function(&heartrate_app_setup, 1);

/*
 * Setup routine for heart rate app
 */
void heartrate_app_setup(void) {
    // Add app tile
    heartrate_app_main_tile_num = mainbar_add_app_tile(1, 1, "Heart Rate");
    
    // Register app
    heartrate_app = app_register("Heart Rate", &heartrate_app_64px, enter_heartrate_app_event_cb);
    
    // Setup main tile
    heartrate_app_main_setup(heartrate_app_main_tile_num);
}

/*
 * Get the main tile number
 */
uint32_t heartrate_app_get_app_main_tile_num(void) {
    return(heartrate_app_main_tile_num);
}

/*
 * Event callback when app is entered
 */
static void enter_heartrate_app_event_cb(lv_obj_t *obj, lv_event_t event) {
    switch(event) {
        case(LV_EVENT_CLICKED):   
            statusbar_hide(true);
            mainbar_jump_to_tilenumber(heartrate_app_main_tile_num, LV_ANIM_OFF);
            break;
    }    
}