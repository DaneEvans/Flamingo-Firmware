/**
 * @file BuzzerModule.cpp
 * @brief Implements a PWM Buzzer
 *
 */

#include "Default.h"
#include "BuzzerModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "SPILock.h"
#include "airtime.h"
#include "configuration.h"
#include "gps/GeoCoord.h"
#include <Arduino.h>
#include <Throttle.h>

BuzzerModule *buzzerModule;

BuzzerModule::BuzzerModule() : concurrency::OSThread("Buzzer") {}

// runOnce is really misnamed - this is periodically called

bool initDone = 0;


#define POLL_INTERVAL_MS 50   

#define STATE_DEFAULT 0
#define STATE_BUZZER_ON 1
#define STATE_PAUSE 2



uint8_t fsmState = STATE_DEFAULT;

int32_t BuzzerModule::runOnce()
{
    if (!initDone) {
        pinMode(BUZZER_PIN, OUTPUT);
        digitalWrite(BUZZER_PIN, BUZZER_OFF);
        initDone = 1;
    }

    unsigned long now = millis();


#if 0
    if (currentTone != 0) {
        pinMode(BUZZER_PIN, OUTPUT);
        LOG_INFO("Buzzer, starting tone");
        unsigned long stop = now + 3000;
        while (millis() < stop) {
            delayMicroseconds(1000);
            digitalWrite(BUZZER_PIN, BUZZER_ON);
            delayMicroseconds(1000);
            digitalWrite(BUZZER_PIN, BUZZER_OFF);
        }
        currentTone = 0;
        LOG_INFO("Buzzer, stopped tone");
    }
#endif

#if 1
 /*
    This is for an active buzzer
    currentTone is either 0 or non-zero
    Tone number is the number of tones that will be done
    Tone pause is the pause in seconds between tones
    */

    switch(fsmState) {
        case STATE_DEFAULT:
            if (currentTone != 0) {
                // start a new tone
                LOG_INFO("Buzzer, starting tone");
                toneStarted = now;
                toneFinish = now + toneDurationMSecs;
                digitalWrite(BUZZER_PIN, BUZZER_ON);
                fsmState = STATE_BUZZER_ON;
            } else {
                digitalWrite(BUZZER_PIN, BUZZER_OFF);
            }
            break;
        case STATE_BUZZER_ON:
            if (now > toneFinish){
                fsmState = STATE_DEFAULT; 
                digitalWrite(BUZZER_PIN, BUZZER_OFF);
                if (toneNumber != 0) toneNumber--;
                if (toneNumber == 0) {
                    currentTone = 0;  //clear current Tone
                    LOG_INFO("Buzzer, finished tone");
                } else if (tonePauseMSecs != 0) {
                    pauseFinish = now + tonePauseMSecs;
                    fsmState = STATE_PAUSE;  
                }
            }
            break;
        case STATE_PAUSE:
            if  (now > pauseFinish ) {
                fsmState = STATE_DEFAULT; 
            }
            break;
    }

#endif
    return(POLL_INTERVAL_MS);
}

void BuzzerModule:: startTone(uint16_t tone, unsigned long durationMs, unsigned long pauseMs, uint16_t number) {
    if (fsmState == STATE_DEFAULT && toneNumber == 0) {
        currentTone = tone;
        toneNumber = number;
        toneDurationMSecs = durationMs;
        tonePauseMSecs = pauseMs;
        if (toneNumber > 0 && tonePauseMSecs == 0) {
            tonePauseMSecs = 500;  // need at least some pause between tones
        }
        LOG_INFO("Buzzer, starting tone type: %d , duration(secs): %d, pause(secs): %d,  number: %d", tone, durationMs, pauseMs, number);
    }
}
