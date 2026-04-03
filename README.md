# SoundViz-Display

## Overview

This project uses an ESP32 to capture audio via an I2S Microphone, performs a  real-time FFT and some post processing, and visualize the frequency spectrum on a HUB75 RGB LED matrix.

The system is designed with a clean separation via different tasks:

- Signal processing (FFT)
- Rendering the display
- I2S Mic Reading

## Status

Currently debugging a display ghosting issue, will implement further visualization features once fixed.
