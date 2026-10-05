#pragma once
#include "mico.h"
#include <stdint.h>
typedef int mico_pwm_t;
#define MICO_PWM_4 3
#define MICO_PWM_5 4
OSStatus MicoPwmInitialize(mico_pwm_t pwm,uint32_t frequency,float duty);
OSStatus MicoPwmStart(mico_pwm_t pwm);
OSStatus MicoPwmStop(mico_pwm_t pwm);
