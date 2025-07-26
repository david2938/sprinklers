# Sprinklers

An ESP8266-based sprinkler system controller

## Table of Contents

- [Features](#features)
- [Limitations](#limitations)
- [Installation](#installation)
- [License](#license)

---

## Features

- Define up to 8 zones
- Ad-hoc scheduling of zones
- Named cycles that automate watering of zones based on days of the week

## Limitations

The controller code has been tested with only a single shift register chip,
meaning that a maximum of 8 zones can be controlled.  In theory, the
SN74HC595N chip allows for chaining of chips and the ShiftRegister74HC595
class supports multiple chips, but this has not been tested.  The prototype
only had an 8-relay board to control 8 zones.

Currently, the cycle controller logic does not support 2- or 3- day watering
programs.  You must select specific days of the week.

## Installation

TBD

## Hardware

- ESP8266 (or maybe ESP32) - tested with a Lolin (Wemos) D1 mini
- SN74HC595N shift register chip
- 8-relay control board
- 24V AC power supply for driving the sprinkler valves
- 24V AC to 5V DC transformer for the 5V electronics (ESP8266, shift register and relay board)
- Project enclosure (if your deployment has to be outside, it should be weather-proof)

## License

Licensed under the Apache License Version 2.0, January 2004.

Importantly, this work is provided on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
implied, including, without limitation, any warranties or conditions
of TITLE, NON-INFRINGEMENT, MERCHANTABILITY, or FITNESS FOR A
PARTICULAR PURPOSE. You are solely responsible for determining the
appropriateness of using or redistributing the Work and assume any
risks associated with Your exercise of permissions under this License.

Many of the electronics used by this project are sensitive to input
voltages.  You assume all liability for ensuring that proper protections
are incorporated to protect your electronics from over-voltage conditions.

**Acknowledgements**

Timo Denk and contributers for the core library that interfaces with the SN74HC595N chip.