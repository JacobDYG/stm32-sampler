# STM32 Sampling Application
## Overview
Stores samples from input `A0` into a circular buffer, at a sample rate of 10 Hz.
Through a serial interface, samples can be read, deleted, and samplng turned on and off.

Threading is used to allow sampling, input and output simultaneously. Thread safety is enforced through the use of a mutex for the buffer.
## Usage
Some analog input should be connected to `A0`, such as a potentiometer, else the software may run but will not appear to report any samples. Commands can be entered via a serial interface.

Commands:
* print (n) - Prints the last n samples, or the number of samples in the buffer, whichever is lower.
* delete (n) - Deletes the last n samples, or the number of samples in the buffer, whichever is lower.
* sampling (on/off) - Enables or disables sampling

## Dependencies
* MBED RTOS
