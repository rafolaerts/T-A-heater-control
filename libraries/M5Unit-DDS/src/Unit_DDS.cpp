#include "Unit_DDS.h"
#include <cstdio>

/*! @brief Write data to the DDS.*/
void Unit_DDS::writeDDSReg(uint8_t addr, uint8_t data)
{
    // Serial.printf("ADDR:%02X,DATA:%02X\r\n",addr,data);
    _pwire->beginTransmission(DDS_UNIT_I2CADDR);
    _pwire->write(addr);
    _pwire->write(data);
    _pwire->endTransmission();
}

/*! @brief Write a certain length of data to the DDS.*/
void Unit_DDS::writeDDSReg(uint8_t addr, uint8_t* data, size_t size)
{
    _pwire->beginTransmission(DDS_UNIT_I2CADDR);
    _pwire->write(addr);
    for (size_t i = 0; i < size; i++) {
        _pwire->write(data[i]);
    }
    _pwire->endTransmission();
}

/*! @brief Read data from the DDS.*/
uint8_t Unit_DDS::readDDSReg(uint8_t addr)
{
    uint8_t data;
    _pwire->beginTransmission(DDS_UNIT_I2CADDR);
    _pwire->write(addr);
    _pwire->endTransmission();
    _pwire->requestFrom(DDS_UNIT_I2CADDR, 1);
    data = _pwire->read();
    _pwire->endTransmission();

    return data;
}

/*! @brief Read a certain length of data from the DDS.*/
void Unit_DDS::readDDSRegs(uint8_t addr, uint8_t* dataptr, uint8_t size)
{
    _pwire->beginTransmission(DDS_UNIT_I2CADDR);
    _pwire->write(addr);
    _pwire->endTransmission();
    _pwire->requestFrom(DDS_UNIT_I2CADDR, (int)size);
    for (int i = 0; i < 6; i++) {
        dataptr[i] = _pwire->read();
    }
    _pwire->endTransmission();
}

/*! @brief Initialize the DDS.
    @return Return 0 if the init was successful, otherwise -1.. */
int Unit_DDS::begin(TwoWire* p)
{
    _pwire = p;

    char snStr[7];
    memset(snStr, 0, sizeof(char) * 7);
    readDDSRegs(DDS_DESC_ADDR, (uint8_t*)snStr, 6);

    String desc(snStr);

    Serial.printf("sn:%s\r\n", snStr);

    return (desc == String("ad9833")) ? 0 : -1;
}

/*! @brief Set the signal frequency.*/
void Unit_DDS::setFreq(uint8_t reg, uint64_t freq)
{
    freq = freq * 268435456 / DDS_FMCLK;

    uint8_t sendbuff[4] = {0, 0, 0, 0};
    sendbuff[0] |= (((freq >> 24) & 0xff) | (reg == 1) ? 0xC0 : 0x80);
    sendbuff[1] |= ((freq >> 16) & 0xff);
    sendbuff[2] |= ((freq >> 8) & 0xff);
    sendbuff[3] |= (freq & 0xff);
    writeDDSReg(DDS_FREQ_ADDR, sendbuff, 4);
}

/*! @brief Set the signal phase.*/
void Unit_DDS::setPhase(uint8_t reg, uint32_t phase)
{
    uint8_t sendbuff[2] = {0, 0};
    phase               = phase * 2048 / 360;
    sendbuff[0] |= (((phase >> 8) & 0xff) | ((reg == 1) ? 0xC0 : 0x80));
    sendbuff[1] |= (phase & 0xff);

    // Serial.printf("%02X %02X",sendbuff[0],sendbuff[1]);
    writeDDSReg(DDS_PHASE_ADDR, sendbuff, 2);
}

/*! @brief Set the signal frequency and phase.*/
void Unit_DDS::setFreqAndPhase(uint8_t freg, uint64_t freq, uint8_t preg, uint32_t phase)
{
    uint8_t sendbuff[6] = {0, 0, 0, 0, 0, 0};

    freq = freq * 268435456 / DDS_FMCLK;
    sendbuff[0] |= (((freq >> 24) & 0xff) | (freg == 1) ? 0xC0 : 0x80);
    sendbuff[1] |= ((freq >> 16) & 0xff);
    sendbuff[2] |= ((freq >> 8) & 0xff);
    sendbuff[3] |= (freq & 0xff);

    phase = phase * 2048 / 360;
    sendbuff[4] |= (((phase >> 8) & 0xff) | ((preg == 1) ? 0xC0 : 0x80));
    sendbuff[5] |= (phase & 0xff);

    std::printf("%02X:%02X:%02X:%02X:%02X:%02X\n", sendbuff[0], sendbuff[1], sendbuff[2], sendbuff[3], sendbuff[4],
                sendbuff[5]);

    writeDDSReg(DDS_FREQ_ADDR, sendbuff, 6);
}

/*! @brief Set the type of output signal.*/
void Unit_DDS::setMode(DDSmode mode)
{
    writeDDSReg(DDS_MODE_ADDE, 0x80 | mode);
}

/*! @brief Set the signal frequency and phase.*/
void Unit_DDS::setCTRL(uint8_t ctrlbyte)
{
    writeDDSReg(DDS_CTRL_ADDR, 0x80 | ctrlbyte);
}

/*! @brief Select frequency register.*/
void Unit_DDS::selectFreqReg(uint8_t num)
{
    uint8_t reg = readDDSReg(DDS_CTRL_ADDR);
    reg &= (~0x40);
    writeDDSReg(DDS_CTRL_ADDR, reg | 0x80 | (num == 1) ? 0x40 : 0);
}

/*! @brief Select phase register.*/
void Unit_DDS::selectPhaseReg(uint8_t num)
{
    uint8_t reg = readDDSReg(DDS_CTRL_ADDR);
    reg &= (~0x20);
    writeDDSReg(DDS_CTRL_ADDR, reg | 0x80 | (num == 1) ? 0x20 : 0);
}

/*! @brief Output waveform with specified frequency and phase.*/
void Unit_DDS::quickOUT(DDSmode mode, uint64_t freq, uint32_t phase)
{
    if (mode <= kSQUAREMode) {
        std::printf("====> setFreq\n");
        setFreqAndPhase(0, freq, 0, phase);
    }
    writeDDSReg(0x20, 0x80 | mode);
    writeDDSReg(0x21, 0x80);

    std::printf("M:%02X C:%02X\n", 0x80 | mode, 0x80);
}

/*! @brief Output specified frequency and phase.*/
void Unit_DDS::OUT(uint8_t freqnum, uint8_t phasenum)
{
    uint8_t reg = readDDSReg(DDS_CTRL_ADDR);
    reg &= (~0x60);
    writeDDSReg(DDS_CTRL_ADDR, reg | 0x80 | ((freqnum == 1) ? 0x40 : 0) | ((phasenum == 1) ? 0x20 : 0));
}

/*! @brief Set the signal frequency and phase.*/
void Unit_DDS::setSleep(uint8_t level)
{
    uint8_t reg = readDDSReg(DDS_CTRL_ADDR);
    reg &= (~0x18);
    reg |= (level == 1) ? 0x10 : 0;
    reg |= (level == 2) ? 0x08 : 0;
    writeDDSReg(DDS_CTRL_ADDR, 0x80 | reg);
}

/*! @brief Reset DDS Unit.*/
void Unit_DDS::reset()
{
    writeDDSReg(DDS_CTRL_ADDR, 0x80 | 0x04);
}
