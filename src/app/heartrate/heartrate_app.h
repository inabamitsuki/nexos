/*
 *  Heart Rate Monitor App for TTGO T-Watch 2019
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 */
#ifndef _HEARTRATE_APP_H
    #define _HEARTRATE_APP_H

    #include <TTGO.h>

    void heartrate_app_setup(void);
    uint32_t heartrate_app_get_app_main_tile_num(void);

#endif // _HEARTRATE_APP_H
