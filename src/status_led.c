#include "status_led.h"

#include "tal_api.h"
#include "tal_sw_timer.h"
#include "tkl_gpio.h"

#ifndef STATUS_LED_GPIO
#define STATUS_LED_GPIO TUYA_GPIO_NUM_23
#endif

#ifndef STATUS_LED_ACTIVE_LEVEL
#define STATUS_LED_ACTIVE_LEVEL TUYA_GPIO_LEVEL_HIGH
#endif

#define STATUS_LED_BLINK_INTERVAL_MS 500

typedef struct {
    bool initialized;
    bool pairing;
    bool paired;
    bool switch_on;
    bool blink_on;
    MUTEX_HANDLE mutex;
    TIMER_ID timer;
} status_led_context_t;

static status_led_context_t s_status_led = {0};

static TUYA_GPIO_LEVEL_E status_led_off_level(void)
{
    return (STATUS_LED_ACTIVE_LEVEL == TUYA_GPIO_LEVEL_HIGH) ? TUYA_GPIO_LEVEL_LOW : TUYA_GPIO_LEVEL_HIGH;
}

static TUYA_GPIO_LEVEL_E status_led_on_level(void)
{
    return STATUS_LED_ACTIVE_LEVEL;
}

static void status_led_apply_locked(bool advance_blink)
{
    TUYA_GPIO_LEVEL_E level = status_led_off_level();

    if (!s_status_led.initialized) {
        return;
    }

    if (s_status_led.pairing) {
        if (advance_blink) {
            s_status_led.blink_on = !s_status_led.blink_on;
        }
        level = s_status_led.blink_on ? status_led_on_level() : status_led_off_level();
    } else if (s_status_led.paired) {
        level = s_status_led.switch_on ? status_led_on_level() : status_led_off_level();
    }

    if (tkl_gpio_write(STATUS_LED_GPIO, level) != OPRT_OK) {
        PR_ERR("Failed to update status LED GPIO");
    }
}

static void status_led_apply(bool advance_blink)
{
    if (!s_status_led.initialized) {
        return;
    }

    if (OPRT_OK != tal_mutex_lock(s_status_led.mutex)) {
        return;
    }

    status_led_apply_locked(advance_blink);

    tal_mutex_unlock(s_status_led.mutex);
}

static void status_led_timer_cb(TIMER_ID timer_id, void *arg)
{
    (void)timer_id;
    (void)arg;

    status_led_apply(true);
}

void status_led_init(void)
{
    if (s_status_led.initialized) {
        return;
    }

    TUYA_GPIO_BASE_CFG_T gpio_cfg = {
        .mode = TUYA_GPIO_PUSH_PULL,
        .direct = TUYA_GPIO_OUTPUT,
        .level = status_led_off_level(),
    };

    if (OPRT_OK != tal_mutex_create_init(&s_status_led.mutex)) {
        PR_ERR("Failed to create status LED mutex");
        return;
    }

    if (OPRT_OK != tkl_gpio_init(STATUS_LED_GPIO, &gpio_cfg)) {
        PR_ERR("Failed to initialize status LED GPIO");
        tal_mutex_release(s_status_led.mutex);
        s_status_led.mutex = NULL;
        return;
    }

    if (OPRT_OK != tal_sw_timer_create(status_led_timer_cb, NULL, &s_status_led.timer)) {
        PR_ERR("Failed to create status LED timer");
        tal_mutex_release(s_status_led.mutex);
        s_status_led.mutex = NULL;
        return;
    }

    s_status_led.initialized = true;
    s_status_led.pairing = false;
    s_status_led.paired = false;
    s_status_led.switch_on = false;
    s_status_led.blink_on = false;

    status_led_apply(false);
    if (OPRT_OK != tal_sw_timer_start(s_status_led.timer, STATUS_LED_BLINK_INTERVAL_MS, TAL_TIMER_CYCLE)) {
        PR_ERR("Failed to start status LED timer");
    }
}

void status_led_set_pairing(bool pairing)
{
    if (!s_status_led.initialized) {
        return;
    }

    if (OPRT_OK != tal_mutex_lock(s_status_led.mutex)) {
        return;
    }

    s_status_led.pairing = pairing;
    if (pairing) {
        s_status_led.blink_on = true;
    }
    status_led_apply_locked(false);

    tal_mutex_unlock(s_status_led.mutex);
}

void status_led_set_paired(bool paired)
{
    if (!s_status_led.initialized) {
        return;
    }

    if (OPRT_OK != tal_mutex_lock(s_status_led.mutex)) {
        return;
    }

    s_status_led.paired = paired;
    if (!paired) {
        s_status_led.pairing = true;
        s_status_led.blink_on = true;
    }
    status_led_apply_locked(false);

    tal_mutex_unlock(s_status_led.mutex);
}

void status_led_set_switch_state(bool switch_on)
{
    if (!s_status_led.initialized) {
        return;
    }

    if (OPRT_OK != tal_mutex_lock(s_status_led.mutex)) {
        return;
    }

    s_status_led.switch_on = switch_on;
    status_led_apply_locked(false);

    tal_mutex_unlock(s_status_led.mutex);
}

void status_led_update(void)
{
    status_led_apply(false);
}