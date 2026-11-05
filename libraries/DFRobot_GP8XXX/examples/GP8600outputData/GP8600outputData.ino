/*!
 * @file GP8600outputData.cpp
 * @brief Output the corresponding analog voltage signal or current signal through pulse width modulation signal or I2C communication.
 * @copyright Copyright (c) 2025 DFRobot Co.Ltd (http://www.dfrobot.com)
 * @license The MIT License (MIT)
 * @author [lr](rong.li@dfrobot.com)
 * @version V1.1.0
 * @date 2025-07-04
 * @url https://github.com/DFRobot/DFRobot_GP8XXX
 */
#include <DFRobot_GP8XXX.h>

/*
  For communication using I2C, please enable the following macro definitions. Using the PWM mode, please disable the following macro definition
*/
// #define I2C_COMMUNICATION 

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

#ifdef  I2C_COMMUNICATION
  //Please make the modifications according to the address on the back of the module.
  #define MODULE_I2C_ADDRESS 0x58
  DFRobot_GP8600_I2C GP8600(&Wire, MODULE_I2C_ADDRESS);
#else
  //Output the IO port.
  #if defined(ESP32) || defined(ESP8266)
    int pwmPin = 13;//IO13
  #else
    int pwmPin = 9;
  #endif
  DFRobot_GP8600_PWM GP8600(pwmPin);
#endif


void setup() {

#ifdef  I2C_COMMUNICATION
  Serial.begin(9600);
  while(GP8600.begin()!=0){
    Serial.println("Communication with the device failed. Please check if the connection is normal or if the device address is set correctly.");
    delay(1000);
  }
  Serial.println("Successful connection!");
   /**
   * @brief Set the DAC output range.
   * @param range DAC output range.
   * @n    eOutputRange5V(0-5V)
   * @n    eOutputRange6V(0-6V)
   * @n    eOutputRange10V(0-10V)
   * @n    eOutputRange12V(0-12V)
   * @n    eOutputRange20MA(0-20MA)
   */  
  GP8600.setDACOutRange(GP8600.eOutputRange12V);
   /**
   * @brief Set the DAC output data.
   * @param data DAC output data.
   * @n In this module, with a 16-bit precision DAC, the data values ranging from 0 to 65535 correspond to ranges of 0-5V or 0-6V or 0-10V or 0-12V or 0-20MA, respectively.
   */  
  uint16_t data=65535;
  GP8600.setDACOutData(data);

#else

  GP8600.begin();
   /**
   * @brief Set the DAC output data.
   * @param data DAC output data.
   * @n In this module, with a 16-bit precision DAC, the data values ranging from 0 to 255 correspond to ranges of or 0-6V or 0-12V or 0-20MA, respectively.
   * @n However, when you use esp32 or esp8266, the range of this data will change to 0 - 1023.
   * @n In PWM mode, all settings depend on the dip switch. For details, please refer to the back of the module.
   * @n If you want to output a 0-5V or 0-10V voltage signal, please multiply it by the proportion coefficient of 0.83.
   * @n For example: GP8600.setDACOutData(data*0.83);
   * @n If you want to output a 4-20mA current signal, please use the following: 
   * @n For example: GP8600.setDACOutData(max(data, 42.5));
   * @n If you wish to output a 4 - 20 milliampere current signal and are using esp32 or esp8266, please use the following method:
   * @n For example: GP8600.setDACOutData(max(data*0.83, 170.5));
   */  
  uint16_t data=255;
  GP8600.setDACOutData(data);
#endif 

}

void loop() {

}
