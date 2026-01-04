/**
 * @file BlinkModule.cpp
 * @brief Implements a Blink LED for location, heartbeat, RangeTest, and Connection status
 *
 */

#ifdef FLAMINGO

#include "BlinkModule.h"
#include "Default.h"
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
#include "modules/RangeTestModule.h"
#include <Arduino.h>
#include <Throttle.h>

BlinkModule *blinkModule;

BlinkModule::BlinkModule() : concurrency::OSThread("Blink") {}

#if defined(FLAMINGO_RT_LED) || defined(FLAMINGO_CONNECTION_LED)
// Helper method to set RGB LED color (using Red, Green, and optionally Blue pins)
// If pinB is 0, blue is treated as unavailable
void BlinkModule::setRGBLEDColor(uint8_t pinR, uint8_t pinG, uint8_t pinB, LEDColor color)
{
    bool hasBlue = (pinB != 0);
    LOG_DEBUG("setRGBLEDColor: R=%d G=%d B=%d color=%d hasBlue=%d", pinR, pinG, pinB, (int)color, hasBlue);

    switch (color) {
    case LEDColor::Off:
        digitalWrite(pinR, LOW);
        digitalWrite(pinG, LOW);
        if (hasBlue)
            digitalWrite(pinB, LOW);
        break;
    case LEDColor::Red:
        digitalWrite(pinR, HIGH);
        digitalWrite(pinG, LOW);
        if (hasBlue)
            digitalWrite(pinB, LOW);
        break;
    case LEDColor::Green:
        digitalWrite(pinR, LOW);
        digitalWrite(pinG, HIGH);
        if (hasBlue)
            digitalWrite(pinB, LOW);
        break;
    case LEDColor::Amber:
        // Amber = Red + Green (both on)
        digitalWrite(pinR, HIGH);
        digitalWrite(pinG, HIGH);
        if (hasBlue)
            digitalWrite(pinB, LOW);
        break;
    case LEDColor::Purple:
        // Purple = Red + Blue
        digitalWrite(pinR, HIGH);
        digitalWrite(pinG, LOW);
        if (hasBlue)
            digitalWrite(pinB, HIGH);
        break;
    case LEDColor::Blue:
        // Blue = Blue only
        digitalWrite(pinR, LOW);
        digitalWrite(pinG, LOW);
        if (hasBlue)
            digitalWrite(pinB, HIGH);
        break;
    case LEDColor::Teal:
        // Teal = Green + Blue
        digitalWrite(pinR, LOW);
        digitalWrite(pinG, HIGH);
        if (hasBlue)
            digitalWrite(pinB, HIGH);
        break;
    case LEDColor::White:
        // White = Red + Green + Blue (all on)
        digitalWrite(pinR, HIGH);
        digitalWrite(pinG, HIGH);
        if (hasBlue)
            digitalWrite(pinB, HIGH);
        break;
    }
}
#endif

#ifdef FLAMINGO_RT_LED
void BlinkModule::setRangeTestLED(LEDColor color)
{
    // Pins are initialized at startup, just set the color
    setRGBLEDColor(PIN_LED_RT_R, PIN_LED_RT_G, PIN_LED_RT_B, color);
    rtLedControlStartTime = millis();
    rtLedsActive = (color != LEDColor::Off);
}

void BlinkModule::setRangeTestLEDTimeout(unsigned long timeoutMs)
{
    rtLedControlStartTime = millis();
    rtLedsActive = true;
    // Timeout is handled in runOnce()
}
#endif

#ifdef FLAMINGO_CONNECTION_LED
void BlinkModule::setConnectionLED(LEDColor color)
{
    // Pins are initialized at startup, just set the color
    LOG_DEBUG("setConnectionLED called with color=%d, pins R=%d G=%d B=%d", (int)color, PIN_LED_CONN_R, PIN_LED_CONN_G,
              PIN_LED_CONN_B);
    setRGBLEDColor(PIN_LED_CONN_R, PIN_LED_CONN_G, PIN_LED_CONN_B, color);
    connLedControlStartTime = millis();
    connLedsActive = (color != LEDColor::Off);
    connLedColor = color;
    LOG_DEBUG("Connection LED set: active=%d, color=%d", connLedsActive, (int)color);
}

void BlinkModule::setConnectionLEDTimeout(unsigned long timeoutMs)
{
    connLedControlStartTime = millis();
    connLedsActive = true;
    // Timeout is handled in runOnce()
}
#endif

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

#ifdef FLAMINGO_RT_LED
        // Initialize Range Test LED pins at startup
        pinMode(PIN_LED_RT_R, OUTPUT);
        pinMode(PIN_LED_RT_G, OUTPUT);
        digitalWrite(PIN_LED_RT_R, LOW);
        digitalWrite(PIN_LED_RT_G, LOW);
        if (PIN_LED_RT_B != 0) {
            pinMode(PIN_LED_RT_B, OUTPUT);
            digitalWrite(PIN_LED_RT_B, LOW);
        }
        rtLedsInitialized = true;
        LOG_DEBUG("Range test LED pins initialized at startup (R=%d, G=%d, B=%d)", PIN_LED_RT_R, PIN_LED_RT_G, PIN_LED_RT_B);
#endif

#ifdef FLAMINGO_CONNECTION_LED
        // Initialize Connection LED pins at startup
        pinMode(PIN_LED_CONN_R, OUTPUT);
        pinMode(PIN_LED_CONN_G, OUTPUT);
        digitalWrite(PIN_LED_CONN_R, LOW);
        digitalWrite(PIN_LED_CONN_G, LOW);
        if (PIN_LED_CONN_B != 0) {
            pinMode(PIN_LED_CONN_B, OUTPUT);
            digitalWrite(PIN_LED_CONN_B, LOW);
        }
        connLedsInitialized = true;
        LOG_DEBUG("Connection LED pins initialized at startup (R=%d, G=%d, B=%d)", PIN_LED_CONN_R, PIN_LED_CONN_G,
                  PIN_LED_CONN_B);
#endif
    }

    unsigned long now = millis();

// Check if Range Test LEDs need to be reset after timeout
#ifdef FLAMINGO_RT_LED
    if (rtLedsActive && rtLedsInitialized) {
        unsigned long elapsed = now - rtLedControlStartTime;
        if (elapsed >= RT_LED_TIMEOUT_MS) {
            setRGBLEDColor(PIN_LED_RT_R, PIN_LED_RT_G, PIN_LED_RT_B, LEDColor::Off);
            rtLedsActive = false;
            LOG_DEBUG("Range test LEDs reset after timeout (elapsed: %lu ms)", elapsed);
        }
    }
#endif

// Check if Connection LEDs need to be reset after timeout
#ifdef FLAMINGO_CONNECTION_LED
    if (connLedsActive && connLedsInitialized) {
        unsigned long elapsed = now - connLedControlStartTime;
        if (elapsed >= CONN_LED_TIMEOUT_MS) {
            setRGBLEDColor(PIN_LED_CONN_R, PIN_LED_CONN_G, PIN_LED_CONN_B, LEDColor::Off);
            connLedsActive = false;
            LOG_DEBUG("Connection LEDs reset after timeout (elapsed: %lu ms)", elapsed);
        }
    }
#endif

    /*
       currentBlink is either 0 or non-zero
       Blink number is the number of Blinks that will be done
       Blink pause is the pause in seconds between Blinks
       */

    switch (fsmState) {
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
        if (now > blinkFinish) {
            fsmState = STATE_DEFAULT;
            digitalWrite(BLINK_PIN, BLINK_OFF);
            if (blinkPauseMSecs != 0) {
                pauseFinish = now + blinkPauseMSecs;
                fsmState = STATE_PAUSE;
            }
        }
        break;
    case STATE_PAUSE:
        if (now > pauseFinish) {
            fsmState = STATE_DEFAULT;
            currentBlink = 1; // continuous blinks
        }
        break;
    }

// Return shorter interval if any LEDs are active to catch timeout
#ifdef FLAMINGO_RT_LED
    if (rtLedsActive) {
        return 1000; // Check every 500ms when LEDs are active
    }
#endif
#ifdef FLAMINGO_CONNECTION_LED
    if (connLedsActive) {
        return 5000; // Check every 500ms when LEDs are active
    }
#endif

    return (POLL_INTERVAL_MS);
}

#endif
