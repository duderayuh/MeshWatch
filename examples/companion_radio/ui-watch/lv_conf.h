// LVGL configuration for the MeshWatch UI. Anything not set here falls back
// to LVGL's defaults (lv_conf_internal.h).
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Widget/heap memory comes from PSRAM (moving it to internal RAM made no
// measurable rendering difference and costs BLE headroom).
#define LV_USE_STDLIB_MALLOC    LV_STDLIB_BUILTIN
#define LV_MEM_SIZE             (256 * 1024U)
#define LV_MEM_POOL_INCLUDE     <esp32-hal-psram.h>
#define LV_MEM_POOL_ALLOC       ps_malloc

#define LV_DEF_REFR_PERIOD      20
#define LV_DPI_DEF              200

#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL          1
#define LV_USE_ASSERT_MALLOC        1

#define LV_FONT_MONTSERRAT_12   1
#define LV_FONT_MONTSERRAT_14   1
#define LV_FONT_MONTSERRAT_16   1
#define LV_FONT_MONTSERRAT_20   1
#define LV_FONT_MONTSERRAT_28   1
#define LV_FONT_MONTSERRAT_48   1
#define LV_FONT_DEFAULT         &lv_font_montserrat_16

#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1

// Not needed on the watch
#define LV_USE_CALENDAR         0
#define LV_USE_CHART            0
#define LV_USE_IMAGEBUTTON      0
#define LV_USE_MENU             0
#define LV_USE_SPAN             0
#define LV_USE_TABLE            0
#define LV_USE_TABVIEW          0
#define LV_USE_WIN              0
#define LV_USE_SPINBOX          0
#define LV_USE_LED              0
#define LV_USE_ANIMIMG          0
#define LV_BUILD_EXAMPLES       0
#define LV_BUILD_DEMOS          0

#endif
