#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <stdint.h>
#include "esp_err.h"

/** 初始化 GPIO4 上的 SG90 PWM 输出，启动时给出 90° 中位参考信号。 */
esp_err_t servo_init(void);

/**
 * 设置 0–180° 目标参考值；返回成功仅表示 PWM 参数已提交，不代表测得机械位置。
 * 初始化后仅由 NimBLE 主机任务调用；本驱动不等待舵机转动，不使用渐变功能。
 */
esp_err_t servo_set_angle(uint16_t angle);

#endif
