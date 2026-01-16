## Compile time defines

These are included as a build flag eg. `-D FLAMINGO`

```
# Booleans (-D FLAMINGO)
FLAMINGO - Change the packet headers / checksum and enable all of the rest of these changes
FLAMINGO_SLINK - Add RS485 support
FLAMINGO_SLINK_DEBUG

FLAMINGO_BLINKY - Adds another hearbeat LED
FLAMINGO_BUZZER - Adds an active buzzer, used to make rangetest driven placeent easier.
FLAMINGO_RT_LED - RGB Led that responds to rangetest packets
FLAMINGO_CONNECTION_LED - RGB Led that can be set via messages.

# switches (-DFLAMINGO_BUZZER_LOWTRUE=1)

FLAMINGO_BUZZER_LOWTRUE=1 ## Not sure why this isn't boolean

# pin definitions - use BCM numbering, 1.01 = 33
# (-DFLAMINGO_BLINKY_IO=33)
FLAMINGO_BLINKY_IO=n

FLAMINGO_BUZZER_IO=n

PIN_LED_RT_R=n
PIN_LED_RT_G=n
PIN_LED_RT_B=n

PIN_LED_CONN_R=n
PIN_LED_CONN_G=n
PIN_LED_CONN_B=n

## The rs485 ones need to go in here too - or we need to use the serail pins rather than overrriding them.



```

## Command List

### Range test

_Needs rangetest module turned on (on both transmitter and reciever)_
ADRT On - The node will start transmitting rangetest packets
ADRT delay <15 | 30 | 60> - Set rangetest frequency to n seconds
ADRT Off - Set the soft switch so the node does not transmit
ADRT on hop - turn on dynamic rangetest with hopping.
Alert bel - Toggle rangetest
_When present the Rangetest LED, or buzzer will respond to these packets._

### Connectivity LED

Sent by DM or on a channel (recommend the infra channel from the surface)
`ADLED <Red | Green | off> ` Times out after 10 minutes?
_Note this behaviour is still being tweaked_
