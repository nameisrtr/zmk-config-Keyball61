/*
 * Right-half (central) sleep, reconnect, and fault guards.
 *
 * - Deep sleep: system-off only wakes from the keys if every matrix row still
 *   senses a press and every column is driven. ZMK powers off even when they
 *   are not: while a key is held the matrix polls with sensing disabled, and a
 *   press that lands while sleep is being entered disables it too. Check the
 *   pins with interrupts locked at the last moment and reboot instead of
 *   powering off when the matrix could not wake the half.
 * - Sleep screen: this nice!view keeps its last image through system-off, so a
 *   sleeping right half looked frozen. Write a blank frame before sleeping.
 * - Split reconnect: while the left half is away (it deep-sleeps after ten
 *   minutes without left key presses), ZMK scans for it 30 ms out of every
 *   60 ms. Keep the 30 ms window but open it every 200 ms instead.
 * - Faults: reboot instead of Zephyr's default halt with interrupts locked,
 *   which left the half frozen until its reset button was pressed.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fatal.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <hal/nrf_gpio.h>
#include <soc_nrf_common.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gap.h>
#endif

/* The blank frame is written on the display thread while the activity work
 * waits for it, which would deadlock if both shared the system work queue.
 */
#define SLEEP_SCREEN                                                                               \
    (IS_ENABLED(CONFIG_ZMK_DISPLAY) && IS_ENABLED(CONFIG_ZMK_DISPLAY_WORK_QUEUE_DEDICATED) &&      \
     DT_HAS_CHOSEN(zephyr_display))

#if SLEEP_SCREEN
#include <lvgl.h>
#include <zephyr/drivers/display.h>
#include <zmk/display.h>
#endif

void k_sys_fatal_error_handler(unsigned int reason, const z_arch_esf_t *esf) {
    ARG_UNUSED(reason);
    ARG_UNUSED(esf);

    sys_reboot(SYS_REBOOT_WARM);
}

#if IS_ENABLED(CONFIG_POWEROFF)

/* Diode direction col2row: the rows are the sensed inputs and the columns
 * are driven while the matrix waits for a key.
 */
#define MATRIX_NODE DT_NODELABEL(kscan0)

BUILD_ASSERT(DT_NODE_HAS_COMPAT(MATRIX_NODE, zmk_kscan_gpio_matrix),
             "power guard expects the GPIO matrix kscan");
BUILD_ASSERT(DT_ENUM_HAS_VALUE(MATRIX_NODE, diode_direction, col2row),
             "power guard expects col2row diodes");
BUILD_ASSERT(!IS_ENABLED(CONFIG_ZMK_PM_SOFT_OFF),
             "soft-off disarms the matrix on purpose; the wake interlock would reboot instead");

#define MATRIX_PIN(node_id, prop, idx) NRF_DT_GPIOS_TO_PSEL_BY_IDX(node_id, prop, idx),
#define MATRIX_ACTIVE_LEVEL(node_id, prop, idx)                                                   \
    ((DT_GPIO_FLAGS_BY_IDX(node_id, prop, idx) & GPIO_ACTIVE_LOW) ? 0U : 1U),

static const uint32_t matrix_rows[] = {DT_FOREACH_PROP_ELEM(MATRIX_NODE, row_gpios, MATRIX_PIN)};
static const uint32_t matrix_cols[] = {DT_FOREACH_PROP_ELEM(MATRIX_NODE, col_gpios, MATRIX_PIN)};
static const uint32_t matrix_col_active[] = {
    DT_FOREACH_PROP_ELEM(MATRIX_NODE, col_gpios, MATRIX_ACTIVE_LEVEL)};

static bool matrix_can_wake(void) {
    for (size_t i = 0; i < ARRAY_SIZE(matrix_rows); i++) {
        if (nrf_gpio_pin_sense_get(matrix_rows[i]) == NRF_GPIO_PIN_NOSENSE) {
            return false;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(matrix_cols); i++) {
        if (nrf_gpio_pin_out_read(matrix_cols[i]) != matrix_col_active[i]) {
            return false;
        }
    }

    return true;
}

FUNC_NORETURN void __real_sys_poweroff(void);

FUNC_NORETURN void __wrap_sys_poweroff(void) {
    /* With interrupts locked a key press can no longer disarm the matrix
     * between this check and system-off.
     */
    (void)irq_lock();

    if (!matrix_can_wake()) {
        sys_reboot(SYS_REBOOT_WARM);
    }

    __real_sys_poweroff();
}

#endif /* IS_ENABLED(CONFIG_POWEROFF) */

#if SLEEP_SCREEN

#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define DISPLAY_WIDTH DT_PROP(DISPLAY_NODE, width)
#define DISPLAY_HEIGHT DT_PROP(DISPLAY_NODE, height)

BUILD_ASSERT(DISPLAY_WIDTH % 8 == 0, "blank frame assumes whole bytes per line");

static const struct device *const display = DEVICE_DT_GET(DISPLAY_NODE);

/* All zeros is LVGL's white background on this 1-bit MONO01 panel. Kept in
 * RAM because the SPI driver transmits straight from the buffer.
 */
static uint8_t blank_frame[DISPLAY_WIDTH / 8 * DISPLAY_HEIGHT];
static bool blank_frame_shown;
static K_SEM_DEFINE(blank_frame_written, 0, 1);

static void blank_frame_write_cb(struct k_work *work) {
    ARG_UNUSED(work);

    const struct display_buffer_descriptor desc = {
        .buf_size = sizeof(blank_frame),
        .width = DISPLAY_WIDTH,
        .height = DISPLAY_HEIGHT,
        .pitch = DISPLAY_WIDTH,
    };

    if (display_write(display, 0, 0, &desc, blank_frame) == 0) {
        blank_frame_shown = true;
    }

    k_sem_give(&blank_frame_written);
}

static K_WORK_DEFINE(blank_frame_write_work, blank_frame_write_cb);

static void status_screen_redraw_cb(struct k_work *work) {
    ARG_UNUSED(work);

    if (blank_frame_shown) {
        /* LVGL does not know the panel was overwritten. */
        lv_obj_invalidate(lv_scr_act());
        blank_frame_shown = false;
    }
}

static K_WORK_DEFINE(status_screen_redraw_work, status_screen_redraw_cb);

#endif /* SLEEP_SCREEN */

static void show_blank_frame(void) {
#if SLEEP_SCREEN
    if (!device_is_ready(display) || !zmk_display_is_initialized()) {
        return;
    }

    k_sem_reset(&blank_frame_written);
    if (k_work_submit_to_queue(zmk_display_work_q(), &blank_frame_write_work) < 0) {
        return;
    }

    /* Bounded so a stuck display never keeps the half awake. A write still in
     * flight would race the SPI suspend that follows, so restart instead.
     */
    if (k_sem_take(&blank_frame_written, K_MSEC(500)) != 0 &&
        k_work_cancel(&blank_frame_write_work) != 0) {
        sys_reboot(SYS_REBOOT_WARM);
    }
#endif
}

static void restore_status_screen(void) {
#if SLEEP_SCREEN
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &status_screen_redraw_work);
    }
#endif
}

static int power_guard_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *activity = as_zmk_activity_state_changed(eh);
    if (activity == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (activity->state) {
    case ZMK_ACTIVITY_SLEEP:
        show_blank_frame();
        break;
    case ZMK_ACTIVITY_ACTIVE:
        /* Only matters if device suspend failed and ZMK stayed up. */
        restore_status_screen();
        break;
    default:
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(keyball61_power_guard, power_guard_listener);
ZMK_SUBSCRIPTION(keyball61_power_guard, zmk_activity_state_changed);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)

/* 0x0140 * 0.625 ms = 200 ms: a 15% scan duty cycle instead of 50%. The left
 * half's 1.28 s of high-duty directed advertising after it wakes still spans
 * six windows; its later 100-150 ms advertising takes longer to catch.
 */
#define SPLIT_RECONNECT_SCAN_INTERVAL 0x0140

int __real_bt_le_scan_start(const struct bt_le_scan_param *param, bt_le_scan_cb_t cb);

int __wrap_bt_le_scan_start(const struct bt_le_scan_param *param, bt_le_scan_cb_t cb) {
    /* ZMK's split central is this firmware's only scanner and always passes
     * BT_LE_SCAN_PASSIVE; any other scan request is forwarded unchanged.
     */
    if (param != NULL && param->type == BT_LE_SCAN_TYPE_PASSIVE &&
        param->interval == BT_GAP_SCAN_FAST_INTERVAL && param->window == BT_GAP_SCAN_FAST_WINDOW) {
        struct bt_le_scan_param reconnect = *param;

        reconnect.interval = SPLIT_RECONNECT_SCAN_INTERVAL;
        return __real_bt_le_scan_start(&reconnect, cb);
    }

    return __real_bt_le_scan_start(param, cb);
}

#endif /* IS_ENABLED(CONFIG_ZMK_SPLIT_BLE) */
