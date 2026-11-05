# -*- coding:utf-8 -*-
'''!
  @file  GP8600output_data.py
  @brief Output the corresponding analog voltage signal or current signal through pulse width modulation signal or I2C communication.
  @copyright  Copyright (c) 2025 DFRobot Co.Ltd (http://www.dfrobot.com)
  @license  The MIT License (MIT)
  @author  [lr](rong.li@dfrobot.com)
  @version  V1.1.0
  @date  2025-07-04
  @url  https://github.com/DFRobot/DFRobot_GP8XXX
'''

from __future__ import print_function
import sys
import os
import time

sys.path.append(os.path.dirname(os.path.dirname(os.path.realpath(__file__))))
from DFRobot_GP8XXX import *

"""
/**************************
----------------------------
| A2 |  A1 | A0 | i2c_addr |
----------------------------
| 0  |  0  | 0  |   0x58   |
----------------------------
| 0  |  0  | 1  |   0x59   |
----------------------------
| 0  |  1  | 0  |   0x5A   |
----------------------------
| 0  |  1  | 1  |   0x5B   |
----------------------------
| 1  |  0  | 0  |   0x5C   |
----------------------------
| 1  |  0  | 1  |   0x5D   |
----------------------------
| 1  |  1  | 0  |   0x5E   |
----------------------------
| 1  |  1  | 1  |   0x5F   |
----------------------------
***************************/
"""

COMM_METHOD = "PWM"  # "I2C" or "PWM"

if COMM_METHOD == "I2C":
  # This example uses software I2C driver. It is not recommended to pass the hardware I2C pin interface. The pin number passed should use the BCM coding.
  DAC = DFRobot_GP8600_I2C(i2c_sda=17,i2c_scl=27,i2c_addr=0x58)
else:
  # Pass the pin number in BCM encoding
  DAC = DFRobot_GP8600_PWM(pin0=17)



def setup():


  if COMM_METHOD == "I2C":
    while DAC.begin()!=0:
      print("Communication with the device failed. Please check if the connection is correct and ensure that the device address is set correctly.")
      time.sleep(1)
    print("Successful connection!")
    '''
      @brief Set DAC output range
      @param mode DAC output range
      @n     OUTPUT_RANGE_5V(0-5V)
      @n     OUTPUT_RANGE_6V(0-6V)
      @n     OUTPUT_RANGE_10V(0-10V)
      @n     OUTPUT_RANGE_12V(0-12V)
      @n     OUTPUT_RANGE_20MA(0-20mA)
    '''
    DAC.set_dac_outrange(DFRobot_GP8600_I2C.OUTPUT_RANGE_5V)
    '''!
      @brief Set different channel output DAC values
      @param data value corresponding to the data voltage value
      @n (0 - 65535) In this module, with a 16-bit precision DAC, the data values ranging from 0 to 65535 correspond to ranges of 0-5V or 0-6V or 0-10V or 0-12V or 0-20MA, respectively.
      @param channel output channel
    '''
    DAC.set_dac_out_data(65535)
  else:
    DAC.begin()
    data = 100.0
    '''
      @brief Set the output DAC value
      @param data PWM duty cycle (0.0-100.0)
      @n In PWM mode, all settings depend on the dip switch. For details, please refer to the back of the module.
      @n The optional parameter (0.0-100.0) corresponds to an output of 0 - 20mA current signal.
      @n If you want to output a 4-20mA current signal, please use the following: 
      @n For example: DAC.set_dac_out_data(max(data, 16.6))
      @n If you want to output a 0-5V or 0-10V voltage signal, please multiply it by the proportion coefficient of 0.83.
      @n For example: DAC.set_dac_out_data(data*0.83);
   '''
    DAC.set_dac_out_data(data)
def loop():
  pass

if __name__ == "__main__":
  setup()
  while True:
    loop()