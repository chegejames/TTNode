// Flash storage support

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "debug.h"
#include "comm.h"
#include "timer.h"
#include "nrf_delay.h"
#include "app_error.h"
#include "config.h"
#include "storage.h"
#include "softdevice_handler.h"

#define DISABLE_STORAGE false

#if !defined(NOBONDING) || !defined(OLDSTORAGE)
#include "fstorage.h"
#endif

#if !DISABLE_STORAGE

#if defined(NSDKV10) || defined(NSDKV11)
#include "pstorage.h"
#define OLDSTORAGE
#endif

#if !defined(OLDSTORAGE)
static void fs_event_handler(fs_evt_t const * const evt, fs_ret_t result);
#define PHY_WORD_SIZE         4
#if   defined(NRF51)
#define PHY_PAGE_SIZE_WORDS   (256)
#elif defined(NRF52)
#define PHY_PAGE_SIZE_WORDS   (1024)
#endif
#define TT_PHY_PAGE     0
#define TT_PHY_PAGES    1
#define TT_WORDS (TTSTORAGE_MAX/PHY_WORD_SIZE)
#if (PHY_PAGE_SIZE_WORDS < TT_WORDS)
@error Code is written assuming max of 1 physical page
#endif
FS_REGISTER_CFG(fs_config_t fs_config) =
{
    .callback  = fs_event_handler,
    .num_pages = TT_PHY_PAGES,
    .priority = 1
};
// Retrieve the address of a page
const uint32_t * address_of_page(uint16_t page_num) {
    return fs_config.p_start_addr + (page_num * PHY_PAGE_SIZE_WORDS);
}
#endif

#endif // !DISABLE_STORAGE

// Storage context
static bool storage_initialized = false;
static union ttstorage_ tt;

// Persistent Storage context
#ifdef OLDSTORAGE
static bool pstorage_waiting = false;
static pstorage_block_t pstorage_wait_handle = 0;
static uint16_t pstorage_wait_result;
static pstorage_handle_t pstorage_handle;
static pstorage_handle_t block_0_handle;
#endif

// Function for system event handling
void storage_sys_event_handler(uint32_t sys_evt) {
#if !DISABLE_STORAGE
#ifdef OLDSTORAGE
    pstorage_sys_event_handler(sys_evt);
#endif
#endif // !DISABLE_STORAGE
// This is required for peer manager, so dispatch here even if storage is disabled
#if !defined(NOBONDING) || !defined(OLDSTORAGE)
    fs_sys_event_handler(sys_evt);
#endif
}

// Event handler for fstorage
#if !DISABLE_STORAGE
#ifndef OLDSTORAGE
static void fs_event_handler(fs_evt_t const * const evt, fs_ret_t result)
{
    if (result != FS_SUCCESS)
    {
        // An error occurred.
    }
}
#endif // OLDSTORAGE
#endif

// Persistent storage callback
#ifdef OLDSTORAGE
void pstorage_callback(pstorage_handle_t  * handle,
                       uint8_t              op_code,
                       uint32_t             result,
                       uint8_t            * p_data,
                       uint32_t             data_len) {

    /// If we were waiting, clear the wait
    if (pstorage_waiting && handle->block_id == pstorage_wait_handle) {
        pstorage_wait_result = result;
        pstorage_waiting = false;
    }

    switch (op_code)
    {

    case PSTORAGE_STORE_OP_CODE:
        if (result == NRF_SUCCESS)
            DEBUG_PRINTF("Flash updated.\n");
        else
            DEBUG_PRINTF("Flash storage save error: %d\n", result);
        break;
    case PSTORAGE_LOAD_OP_CODE:
        if (result == NRF_SUCCESS)
            DEBUG_PRINTF("Flash loaded.\n");
        else
            DEBUG_PRINTF("Flash storage load error: %d\n", result);
        break;
    case PSTORAGE_UPDATE_OP_CODE:
    case PSTORAGE_CLEAR_OP_CODE:
        break;
    }

}
#endif // OLDSTORAGE

// Initialize storage subsystem
void storage_init() {
    bool reinitStorage;
    bool initSuccess;

    // clear the in-memory data structure to default in case of failure
    memset(&tt, 0, sizeof(tt));

    // If storage is disabled, just fill in the memory structure
#if DISABLE_STORAGE

    storage_set_to_default();
    storage_initialized = true;
    return;

#else

#ifdef OLDSTORAGE
    // Overall architecture:
    // - each page is 1024 bytes
    // - You can register for N fixed-size blocks within those pages
    uint32_t err_code;
    err_code = pstorage_init();
    if (err_code != NRF_SUCCESS) {
        DEBUG_CHECK(err_code);
        storage_set_to_default();
        return;
    }

    // In our app we'll use a single fixed-size page of TTSTORAGE_MAX bytes
    pstorage_module_param_t param;
    param.block_size  = TTSTORAGE_MAX;
    param.block_count = 1;
    param.cb          = pstorage_callback;
    err_code = pstorage_register(&param, &pstorage_handle);
    if (err_code != NRF_SUCCESS) {
        DEBUG_CHECK(err_code);
        storage_set_to_default();
        return;
    }
    // Get block identifier, by block #.
    // Since we only have a single block, it's #0
    pstorage_block_identifier_get(&pstorage_handle, 0, &block_0_handle);
#else
    fs_ret_t err_code = fs_init();
    if (err_code != FS_SUCCESS) {
        DEBUG_CHECK(err_code);
        storage_set_to_default();
        return;
    }
#endif

#endif // !DISABLE_STORAGE

    // We've successfully initialized
    storage_initialized = true;

    // Load it
    initSuccess = storage_load();

    // Determine whether or not what we've read is valid
    reinitStorage = true;
    if (initSuccess && tt.storage.signature_top == VALID_SIGNATURE && tt.storage.signature_bottom == VALID_SIGNATURE)
        if (tt.storage.version >= MIN_SUPPORTED_VERSION && tt.storage.version <= MAX_SUPPORTED_VERSION) {
            DEBUG_PRINTF("Loaded valid params from storage\n");
            reinitStorage = false;
        }

    // Reinitialize storage if we must
    if (reinitStorage) {
        // Initialize the in-memory structure
        storage_set_to_default();
        // Write it, because we always keep the latest copy on-disk
        storage_save();
    }

}

// Let others get access to the storage
STORAGE *storage() {
    return(&tt.storage.versions.v1);
}

// Set the in-memory structures to default values
void storage_set_to_default() {

    DEBUG_PRINTF("Setting storage to default values\n");

    // Set the storage signature
    tt.storage.signature_top = tt.storage.signature_bottom = VALID_SIGNATURE;

    // We're operating on v1 as the most current version
    tt.storage.version = 1;

    // Initialize ALL fields of our in-memory structure to the current version
#ifdef STORAGE_PRODUCT
    tt.storage.versions.v1.product = STORAGE_PRODUCT;
#else
    tt.storage.versions.v1.product = PRODUCT_SIMPLECAST;
#endif
#ifdef STORAGE_SENSORS
    tt.storage.versions.v1.sensors = STORAGE_SENSORS;
#else
    tt.storage.versions.v1.sensors = SENSOR_ALL;
#endif
#ifdef AIR_COUNTS
    tt.storage.versions.v1.sensors |= SENSOR_AIR_COUNTS;
#endif
#ifdef STORAGE_FLAGS
    tt.storage.versions.v1.flags = STORAGE_FLAGS;
#else
    tt.storage.versions.v1.flags = 0x00000000L;
#endif

    tt.storage.versions.v1.restart_days = DEFAULT_RESTART_DAYS;

#ifdef STORAGE_WAN
    tt.storage.versions.v1.wan = STORAGE_WAN;
#else
#ifdef CELLX
    tt.storage.versions.v1.wan = WAN_AUTO;
#else
#ifdef LORA
    tt.storage.versions.v1.wan = WAN_LORA_THEN_LORAWAN;
#else
    tt.storage.versions.v1.wan = WAN_NONE;
#endif
#endif
#endif

    // Oneshot stuff
#ifdef STORAGE_ONESHOT
    tt.storage.versions.v1.oneshot_minutes = STORAGE_ONESHOT;
#else
    // No oneshot
    tt.storage.versions.v1.oneshot_minutes = 0;
    // If PMS is using the UART, default to oneshot
#if defined(PMSX) && (PMSX==IOUART)
    tt.storage.versions.v1.oneshot_minutes = ONESHOT_MINUTES;
#endif
    // If a cell is configured, default to oneshot
#if defined(CELLX)
    if (tt.storage.versions.v1.wan == WAN_AUTO || tt.storage.versions.v1.wan == WAN_FONA)
        tt.storage.versions.v1.oneshot_minutes = ONESHOT_MINUTES;
#endif
#endif

    // Default cell upload
    tt.storage.versions.v1.oneshot_cell_minutes = ONESHOT_CELL_UPLOAD_MINUTES;

    // Device ID
    tt.storage.versions.v1.device_id = 0L;

    // Initialize things that allow us to contact the service
#ifdef STORAGE_REGION
    strcpy(tt.storage.versions.v1.lpwan_region, STRINGIZE_VALUE_OF(STORAGE_REGION));
#else
    strcpy(tt.storage.versions.v1.lpwan_region, "");
#endif
#ifdef STORAGE_APN
    strcpy(tt.storage.versions.v1.carrier_apn, STRINGIZE_VALUE_OF(STORAGE_APN));
#else
    strcpy(tt.storage.versions.v1.carrier_apn, WIRELESS_CARRIER_APN);
#endif

    // Initialize gps
#if defined(FAKEGPS)
    tt.storage.versions.v1.gps_latitude = 1.23;
    tt.storage.versions.v1.gps_longitude = 2.34;
    tt.storage.versions.v1.gps_altitude = 3.45;
#elif defined(ROCKSGPS)
    tt.storage.versions.v1.gps_latitude = 42.565;
    tt.storage.versions.v1.gps_longitude = -70.784;
    tt.storage.versions.v1.gps_altitude = 0;
#else
    tt.storage.versions.v1.gps_latitude = 0.0;
    tt.storage.versions.v1.gps_longitude = 0.0;
    tt.storage.versions.v1.gps_altitude = 0.0;
#endif
    tt.storage.versions.v1.lkg_gps_latitude = 1.23;
    tt.storage.versions.v1.lkg_gps_longitude = 1.23;
    tt.storage.versions.v1.lkg_gps_altitude = 1.23;

    // Initialize sensor params
    tt.storage.versions.v1.sensor_params[0] = '\0';

    // Initialize expected firmware build filename
#ifdef FIRMWARE
    strcpy(tt.storage.versions.v1.dfu_filename, STRINGIZE_VALUE_OF(FIRMWARE));
#else
    strcpy(tt.storage.versions.v1.dfu_filename, "");
#endif
    tt.storage.versions.v1.dfu_status = DFU_IDLE;
    tt.storage.versions.v1.dfu_error = DFU_ERR_NONE;
    tt.storage.versions.v1.dfu_count = 0;

}

// Get a static help string indicating how the as_string stuff works
char *storage_get_device_params_as_string_help() {
    return("wan.prod.flags.1shotMin.1shotCellMin.bootDays.sensors.deviceID");
}

// Get the in-memory structures as a deterministic sequential text string
bool storage_get_device_params_as_string(char *buffer, uint16_t length) {
    char buf[100];
    sprintf(buf, "%u.%u.%lu.%u.%u.%u.%lu.%lu",
            tt.storage.versions.v1.wan,
            tt.storage.versions.v1.product,
            tt.storage.versions.v1.flags,
            tt.storage.versions.v1.oneshot_minutes,
            tt.storage.versions.v1.oneshot_cell_minutes,
            tt.storage.versions.v1.restart_days,
            tt.storage.versions.v1.sensors,
            tt.storage.versions.v1.device_id);
    if (buffer != NULL)
        strncpy(buffer, buf, length);
    return true;
}

// Set the storage params from a text string
void storage_set_device_params_as_string(char *str) {
    long int l;

    if (*str == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.wan = (uint8_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.product = (uint16_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.flags = (uint32_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.oneshot_minutes = (uint16_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.oneshot_cell_minutes = (uint16_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.restart_days = (uint16_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.sensors = (uint32_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.device_id = (uint32_t) l;
    if (*str++ == '\0')
        return;

}

// Get a static help string indicating how the as_string stuff works
char *storage_get_service_params_as_string_help() {
    return("region/apn");
}

// Get the in-memory structures as a deterministic sequential text string
bool storage_get_service_params_as_string(char *buffer, uint16_t length) {
    char buf[100];
    sprintf(buf, "%s/%s",
            tt.storage.versions.v1.lpwan_region,
            tt.storage.versions.v1.carrier_apn);
    if (buffer != NULL)
        strncpy(buffer, buf, length);
    return true;
}

// Set the storage params from a text string
void storage_set_service_params_as_string(char *str) {
    char ch;
    int i;

    if (*str == '\0')
        return;

    for (i=0;;) {
        ch = *str++;
        tt.storage.versions.v1.lpwan_region[i++] = ch;
        tt.storage.versions.v1.lpwan_region[i] = '\0';
        if (ch == '\0')
            return;
        if (ch == '/')
            break;
    }
    if (*str == '\0')
        return;

    for (i=0;;) {
        ch = *str++;
        tt.storage.versions.v1.carrier_apn[i++] = ch;
        tt.storage.versions.v1.carrier_apn[i] = '\0';
        if (ch == '\0')
            return;
        if (ch == '/')
            break;
    }
    if (*str == '\0')
        return;

}

// Get a static help string indicating how the as_string stuff works
char *storage_get_dfu_state_as_string_help() {
    return("filename/count/status/error");
}

// Get the in-memory structures as a deterministic sequential text string
bool storage_get_dfu_state_as_string(char *buffer, uint16_t length) {
    char buf[100];
    sprintf(buf, "%s/%d/%d/%d",
            tt.storage.versions.v1.dfu_filename,
            tt.storage.versions.v1.dfu_count,
            tt.storage.versions.v1.dfu_status,
            tt.storage.versions.v1.dfu_error);
    if (buffer != NULL)
        strncpy(buffer, buf, length);
    return true;
}

// Set the storage params from a text string
void storage_set_dfu_state_as_string(char *str) {
    char ch;
    int i;
    long int l;

    if (*str == '\0')
        return;

    for (i=0;;) {
        ch = *str++;
        tt.storage.versions.v1.dfu_filename[i++] = ch;
        tt.storage.versions.v1.dfu_filename[i] = '\0';
        if (ch == '\0')
            return;
        if (ch == '/')
            break;
    }
    if (*str == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.dfu_count = (uint16_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.dfu_status = (uint16_t) l;
    if (*str++ == '\0')
        return;

    l = strtol(str, &str, 0);
    tt.storage.versions.v1.dfu_error = (uint16_t) l;
    if (*str++ == '\0')
        return;

}

// Get a static help string indicating how the as_string stuff works
char *storage_get_gps_params_as_string_help() {
    return("lat/lon/alt");
}

// Get the in-memory structures as a deterministic sequential text string
bool storage_get_gps_params_as_string(char *buffer, uint16_t length) {
    char buf[100];
    sprintf(buf, "%f/%f/%f",
            tt.storage.versions.v1.gps_latitude,
            tt.storage.versions.v1.gps_longitude,
            tt.storage.versions.v1.gps_altitude);
    if (buffer != NULL)
        strncpy(buffer, buf, length);
    if (tt.storage.versions.v1.gps_latitude == 0.0
        && tt.storage.versions.v1.gps_longitude == 0.0
        && tt.storage.versions.v1.gps_altitude == 0.0)
        return false;
    return true;
}

// Set the storage params from a text string
void storage_set_gps_params_as_string(char *str) {
    float f;

    if (*str == '\0')
        return;

    f = strtof(str, &str);
    tt.storage.versions.v1.gps_latitude = f;
    if (*str++ == '\0')
        return;

    f = strtof(str, &str);
    tt.storage.versions.v1.gps_longitude = f;
    if (*str++ == '\0')
        return;

    f = strtof(str, &str);
    tt.storage.versions.v1.gps_altitude = f;
    if (*str++ == '\0')
        return;

}

// Get a static help string indicating how the as_string stuff works
char *storage_get_sensor_params_as_string_help() {
    return("g-air.r=15/g-geigers.r=5");
}

// Get the in-memory structures as a deterministic sequential text string
bool storage_get_sensor_params_as_string(char *buffer, uint16_t length) {
    if (buffer != NULL)
        strncpy(buffer, tt.storage.versions.v1.sensor_params, length);
    if (buffer[0] == '\0')
        return false;
    return true;
}

// Set the storage params from a text string
void storage_set_sensor_params_as_string(char *str) {
    strncpy(tt.storage.versions.v1.sensor_params, str, sizeof(tt.storage.versions.v1.sensor_params));
}

// Load from pstorage
bool storage_load() {
#if DISABLE_STORAGE
    storage_set_to_default();
    return true;
#else
    if (storage_initialized) {
#ifdef OLDSTORAGE
        int i;
        pstorage_wait_handle = block_0_handle.block_id;
        pstorage_waiting = true;
        pstorage_load(tt.data, &block_0_handle, TTSTORAGE_MAX, 0);
        for (i=0; i<10 && pstorage_waiting; i++) {
            nrf_delay_ms(500);
        }
        if (!pstorage_waiting && pstorage_wait_result == NRF_SUCCESS)
            return true;
#else
        memcpy(tt.data, (uint8_t *) address_of_page(TT_PHY_PAGE), sizeof(tt.data));
        return true;
#endif
    }
    storage_set_to_default();
    return (false);
#endif // !DISABLE_STORAGE
}

// Save the in-memory storage block
void storage_save() {
#if !DISABLE_STORAGE
    if (storage_initialized) {
        DEBUG_PRINTF("Saving storage, %d/%d used\n", sizeof(tt.storage), sizeof(tt.data));
#ifdef OLDSTORAGE
        pstorage_clear(&block_0_handle, TTSTORAGE_MAX);
        pstorage_store(&block_0_handle, tt.data, TTSTORAGE_MAX, 0);
#else
        uint32_t err_code;
        err_code = fs_erase(&fs_config, address_of_page(TT_PHY_PAGE), TT_PHY_PAGES, NULL);
        if (err_code != NRF_SUCCESS)
            DEBUG_PRINTF("Flash storage erase error: 0x%04x\n", err_code);
        err_code = fs_store(&fs_config, address_of_page(TT_PHY_PAGE), (uint32_t *) tt.data, TT_WORDS, NULL);
        if (err_code != NRF_SUCCESS)
            DEBUG_PRINTF("Flash storage save error: 0x%04x\n", err_code);

#endif
    }
#endif
}
