#pragma once

#ifdef FLAMINGO

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

#ifndef FLAMINGO_BLINKY_IO
#define BLINK_PIN 17
#else
#define BLINK_PIN FLAMINGO_BLINKY_IO
#endif

#define BLINK_ON HIGH
#define BLINK_OFF LOW

class BlinkModule : private concurrency::OSThread
{
    bool firstTime = 1;
    unsigned long blinkDurationMSecs = 1000; // how long one blink should last
    unsigned long blinkPauseMSecs = 2000;    // pause between blinks
    unsigned long blinkStarted = 0;          // blink started
    unsigned long blinkFinish = 0;           // finish time for blink
    unsigned long pauseFinish = 0;
    uint16_t currentBlink = 0; // either 0 or non-zero
    uint16_t blinkNumber = 0;  // number of blinks

  public:
    BlinkModule();

  protected:
    virtual int32_t runOnce() override;
};

extern BlinkModule *blinkModule;
#endif
