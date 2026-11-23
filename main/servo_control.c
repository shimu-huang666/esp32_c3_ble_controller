#include <stdbool.h>

#include "servo_control.h"
#include "driver/ledc.h"

#define SERVO_GPIO 4
#define SERVO_FREQUENCY_HZ 50U
#define SERVO_PERIOD_US (1000000U / SERVO_FREQUENCY_HZ)
#define SERVO_MIN_PULSE_US 500U
#define SERVO_MAX_PULSE_US 2500U
#define SERVO_MAX_ANGLE 180U
#define SERVO_CENTER_ANGLE 90U
#define SERVO_DUTY_RESOLUTION LEDC_TIMER_14_BIT

static bool s_initialized;

/** 通用 180° 舵机参考映射；实际 SG90 行程需要实物校准，演示只使用中间角度。 */
static uint32_t angle_to_duty(uint16_t angle)
{
    uint32_t pulse_us =
        SERVO_MIN_PULSE_US +
        ((SERVO_MAX_PULSE_US - SERVO_MIN_PULSE_US) * angle + SERVO_MAX_ANGLE / 2) / SERVO_MAX_ANGLE;
    return (pulse_us * (1U << SERVO_DUTY_RESOLUTION) + SERVO_PERIOD_US / 2) / SERVO_PERIOD_US;
}

esp_err_t servo_init(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = SERVO_DUTY_RESOLUTION,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = SERVO_FREQUENCY_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t result = ledc_timer_config(&timer);
    if (result != ESP_OK)
    {
        return result;
    }

    const ledc_channel_config_t channel = {
        .gpio_num = SERVO_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = angle_to_duty(SERVO_CENTER_ANGLE),
        .hpoint = 0,
    };
    result = ledc_channel_config(&channel);
    if (result == ESP_OK)
    {
        s_initialized = true;
    }
    return result;
}

esp_err_t servo_set_angle(uint16_t angle)
{
    if (angle > SERVO_MAX_ANGLE)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    /* 仅一个主机任务更新通道，不与其他任务或渐变操作交错；设置后交给硬件持续输出。 */
    esp_err_t result = ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, angle_to_duty(angle));
    if (result != ESP_OK)
    {
        return result;
    }
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}
