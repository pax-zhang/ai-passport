#pragma once

#include "lvgl.h"

enum {
    APP_FARM_ICO_HOME = 0,
    APP_FARM_ICO_BAG,
    APP_FARM_ICO_STEAL,
    APP_FARM_ICO_SET,
    APP_FARM_ICO_SEED,
    APP_FARM_ICO_PLANT,
    APP_FARM_ICO_WATER,
    APP_FARM_ICO_WEED,
    APP_FARM_ICO_PEST,
    APP_FARM_ICO_CUT,
    APP_FARM_ICO_HAND,
    APP_FARM_ICO_N
};

enum {
    APP_FARM_IMG_SEED = 0,
    APP_FARM_IMG_SPROUT,
    APP_FARM_IMG_DEAD,
    APP_FARM_IMG_CARROT,
    APP_FARM_IMG_CABBAGE,
    APP_FARM_IMG_TOMATO,
    APP_FARM_IMG_CORN,
    APP_FARM_IMG_BERRY,
    APP_FARM_IMG_MELON,
    APP_FARM_IMG_CROP_N
};

enum {
    APP_FARM_HAZ_WEED = 0,
    APP_FARM_HAZ_PEST,
    APP_FARM_HAZ_DRY,
    APP_FARM_HAZ_N
};

extern const lv_image_dsc_t app_farm_ico_img[APP_FARM_ICO_N];
extern const lv_image_dsc_t app_farm_crop_img[APP_FARM_IMG_CROP_N];
extern const lv_image_dsc_t app_farm_haz_img[APP_FARM_HAZ_N];
extern const lv_image_dsc_t app_farm_plot_dirt;
