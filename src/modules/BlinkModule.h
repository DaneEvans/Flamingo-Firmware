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

#ifdef FLAMINGO_RT_LED
  // Range Test LED pins
  #ifndef PIN_LED_RT_R
  #define PIN_LED_RT_R (3)
  #endif
  #ifndef PIN_LED_RT_G
  #define PIN_LED_RT_G (2)
  #endif
  #ifndef PIN_LED_RT_B
  #define PIN_LED_RT_B (28)
  #endif
#endif 

#ifdef FLAMINGO_CONNECTION_LED
  // Connection LED pins
  #ifndef PIN_LED_CONN_R
  #define PIN_LED_CONN_R (30)
  #endif
  #ifndef PIN_LED_CONN_G
  #define PIN_LED_CONN_G (26)
  #endif
  #ifndef PIN_LED_CONN_B
  #define PIN_LED_CONN_B (29)
  #endif
#endif

#define BLINK_ON HIGH
#define BLINK_OFF LOW

// LED Color enum
enum class LEDColor {
    Off = 0,
    Red,
    Amber,
    Green,
    Purple,
    Blue,
    Teal,
    White
};

class BlinkModule : private concurrency::OSThread
{
    bool firstTime = 1;
    unsigned long blinkDurationMSecs = 1000;    // how long one blink should last
    unsigned long blinkPauseMSecs = 2000;       // pause between blinks
    unsigned long blinkStarted = 0;   //blink started
    unsigned long blinkFinish = 0;   //finish time for blink
    unsigned long pauseFinish = 0;
    uint16_t currentBlink = 0;     // either 0 or non-zero
    uint16_t blinkNumber = 0;       // number of blinks

    #if defined(FLAMINGO_RT_LED) || defined(FLAMINGO_CONNECTION_LED)
      // Helper method to set RGB LED color (using Red, Green, and optionally Blue pins)
      // If pinB is 0, blue is treated as unavailable
      void setRGBLEDColor(uint8_t pinR, uint8_t pinG, uint8_t pinB, LEDColor color);
    #endif

    #ifdef FLAMINGO_RT_LED
      // Range Test LED state
      bool rtLedsInitialized = false;
      bool rtLedsActive = false;
      unsigned long rtLedControlStartTime = 0;
      static constexpr unsigned long RT_LED_TIMEOUT_MS = 7500; // 7.5 seconds
    #endif

    #ifdef FLAMINGO_CONNECTION_LED
      // Connection LED state
      bool connLedsInitialized = false;
      bool connLedsActive = false;
      unsigned long connLedControlStartTime = 0;
      static constexpr unsigned long CONN_LED_TIMEOUT_MS = 60000; // 60 seconds (1 minute)
      LEDColor connLedColor = LEDColor::Off;
    #endif

  public:
    BlinkModule();

    #ifdef FLAMINGO_RT_LED
    // Range Test LED control
    void setRangeTestLED(LEDColor color);
    void setRangeTestLEDTimeout(unsigned long timeoutMs = RT_LED_TIMEOUT_MS);
    #endif

    #ifdef FLAMINGO_CONNECTION_LED
    // Connection LED control
    void setConnectionLED(LEDColor color);
    void setConnectionLEDTimeout(unsigned long timeoutMs = CONN_LED_TIMEOUT_MS);
    #endif 


  protected:
    virtual int32_t runOnce() override;
    
    
};

extern BlinkModule *blinkModule;
#endif
