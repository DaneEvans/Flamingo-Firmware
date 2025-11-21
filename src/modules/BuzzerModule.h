#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

#ifndef BUZZER_IO
#define BUZZER_PIN 21  
#else
#define BUZZER_PIN BUZZER_IO
#endif 

#if BUZZER_LOWTRUE
#define BUZZER_ON LOW
#define BUZZER_OFF HIGH
#else
#define BUZZER_ON HIGH
#define BUZZER_OFF LOW
#endif

//#define BUZZER_PIN PIN_IO1     // Slot A
//#define BUZZER_PIN PIN_IO2     // Slot B
//#define BUZZER_PIN PIN_IO4
#define BUZZER_FULL_TONE 500    //500 ms PWM
#define BUZZER_HALF_TONE 250    //250 ms PWM
#define BUZZER_QUARTER_TONE 125    //125 ms PWM



class BuzzerModule : private concurrency::OSThread
{
    bool firstTime = 1;
    unsigned long toneStarted = 0;   //tone started
    unsigned long toneFinish = 0;   //finish time for tone
    unsigned long pauseFinish = 0;
    uint16_t currentTone = 0;     // either 0 or non-zero
    unsigned long toneDurationMSecs = 0;    // how long one tone should last
    unsigned long tonePauseMSecs = 0;       // pause between tones
    uint16_t toneNumber = 0;       // number of tones

  public:
    BuzzerModule();
    void startTone(uint16_t tone, unsigned long durationMs, unsigned long pauseMs, uint16_t number);

  protected:
    virtual int32_t runOnce() override;


};

extern BuzzerModule *buzzerModule;
