/**
 * @file BlinkModule.cpp
 * @brief Implements a Blink LED for location, heartbeat
 *
 */

 #ifdef FLAMINGO

#include "Default.h"
#include "BlinkModule.h"
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

BlinkModule *blinkModule;

BlinkModule::BlinkModule() : concurrency::OSThread("Blink") {}

// runOnce is really misnamed - this is periodically called

static bool initDone = 0;


#define POLL_INTERVAL_MS 200   

#define STATE_DEFAULT 0
#define STATE_ON 1
#define STATE_PAUSE 2



static uint8_t fsmState = STATE_DEFAULT;

/* This module blinks an LED periodically */

int32_t BlinkModule::runOnce()
{
    if (!initDone) {
        pinMode(BLINK_PIN, OUTPUT);
        digitalWrite(BLINK_PIN, BLINK_OFF);
        fsmState = STATE_DEFAULT;
        initDone = 1;
        currentBlink = 1;
        blinkNumber = 0;
        blinkDurationMSecs = 500;
        blinkPauseMSecs = 2000;
    }
    
    unsigned long now = millis();


 /*
    currentBlink is either 0 or non-zero
    Blink number is the number of Blinks that will be done
    Blink pause is the pause in seconds between Blinks
    */

    switch(fsmState) {
        case STATE_DEFAULT:
            if (currentBlink != 0) {
                // start a new Blink
                LOG_INFO("Blink module, starting Blink");
                blinkStarted = now;
                blinkFinish = now + blinkDurationMSecs;
                digitalWrite(BLINK_PIN, BLINK_ON);
                fsmState = STATE_ON;
            } else {
                digitalWrite(BLINK_PIN, BLINK_OFF);
            }
            break;
        case STATE_ON:
            if (now > blinkFinish){
                fsmState = STATE_DEFAULT; 
                digitalWrite(BLINK_PIN, BLINK_OFF);
                if (blinkPauseMSecs != 0) {
                    pauseFinish = now + blinkPauseMSecs;
                    fsmState = STATE_PAUSE;  
                }
            }
            break;
        case STATE_PAUSE:
            if  (now > pauseFinish ) {
                fsmState = STATE_DEFAULT;
                currentBlink = 1;  //continuous blinks
            }
            break;
    }


    return(POLL_INTERVAL_MS);
}

#endif

