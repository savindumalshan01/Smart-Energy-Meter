# ⚡ Smart Energy Meter with Tariff Calculation (ATmega328P)

# 📌 Project Overview
  This project is a standalone smart energy meter built using the ATmega328P microcontroller (removed from an Arduino Uno). It measures electrical parameters in real time and calculates electricity bills based       on Sri Lanka CEB tariff structures.
  The Arduino Uno board is used only for uploading the firmware. After programming, the ATmega328P is deployed on a custom PCB, making this project closer to a real-world embedded system rather than a typical       Arduino-based prototype.

# ✨ Features
Real-time measurement of:

Voltage

Current

Active Power

Apparent Power

Reactive Power

Power Factor

Energy consumption calculation (kWh)


Domestic & Industrial tariff-based billing

Menu-driven user interface

4×4 Matrix Keypad for user input

16×2 I²C LCD for live data display

EEPROM data storage to retain values during power failure

User-editable tariff parameters

Low-cost and compact standalone design

# 🧰 Hardware Components

ATmega328P (standalone microcontroller)
Arduino Uno (used only for code uploading)
PZEM-004T v3.0 Energy Meter Module
16×2 I²C LCD
4×4 Matrix Keypad
Custom PCB

# 💻 Software & Tools

Embedded C (Arduino IDE)
EEPROM memory handling

State-machine–based menu system

Serial communication with PZEM-004T module

# ⚙️ System Architecture

PZEM-004T measures voltage, current, and power parameters

ATmega328P processes data and calculates energy (kWh)

Tariff logic computes the electricity bill

LCD displays real-time data and billing information

EEPROM stores energy and tariff data during power loss

Keypad allows user navigation and configuration

# 🎯 Project Objectives

Learn low-level microcontroller deployment

Apply power engineering concepts in embedded systems

Implement real-world electricity billing algorithms

Design a standalone embedded product beyond breadboard Arduino projects

# 🚀 Future Improvements

GSM / Wi-Fi based remote meter reading

Web or mobile application dashboard

Tamper detection and alerts

Higher-accuracy calibration

Data logging with SD card
