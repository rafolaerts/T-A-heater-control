/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file unit_DDS.cpp
  @brief DDS Unit for M5UnitUnified
*/
#include "unit_DDS.hpp"
#include <M5Utility.hpp>
#include <cmath>

using namespace m5::utility::mmh3;
using namespace m5::unit::types;
using namespace m5::unit::dds;
using namespace m5::unit::dds::command;

namespace {

constexpr char DESC[] = "ad9833";
constexpr double MCLK{10000000.f};
constexpr uint32_t MINIMUM_FREQ{0};
constexpr uint32_t MAXIMUM_FREQ{1000000};

// Calculate 28-bit FTW from out[Hz] to be output
uint32_t calculate_ftw(const uint32_t out_hz)
{
    constexpr double scale = static_cast<double>(1ULL << 28);
    uint32_t ftw           = static_cast<uint32_t>(llround(static_cast<double>(out_hz) * scale / MCLK));
    return ftw & 0x0FFFFFFF;
}

// Calculate 11-bit PHASE from phase[deg]
uint16_t calculate_phase(const uint16_t deg)
{
    uint16_t d  = deg % 360;
    uint16_t pw = static_cast<uint16_t>(lround(d * (2048.0f / 360.0f)));
    return pw & 0x7FF;
}

inline bool is_valid_frequency(const uint32_t freq)
{
    //    return std::isfinite(freq) && freq >= MINIMUM_FREQ && freq <= MAXIMUM_FREQ;
    return std::isfinite(freq) && freq <= MAXIMUM_FREQ;
}

}  // namespace

namespace m5 {
namespace unit {

const char UnitDDS::name[] = "UnitDDS";
const types::uid_t UnitDDS::uid{"UnitDDS"_mmh3};
const types::attr_t UnitDDS::attr{attribute::AccessI2C};

bool UnitDDS::begin()
{
    char desc[7]{};
    if (!readDescription(desc)) {
        M5_LIB_LOGE("Failed to read description");
        return false;
    }
    if (strcmp(desc, DESC)) {
        M5_LIB_LOGE("Illegal desc [%s]", desc);
        return false;
    }
    return _cfg.start_output ? writeOutput(_cfg.mode, _cfg.select, _cfg.freq, _cfg.deg) && wakeup() : true;
}

bool UnitDDS::readDescription(char str[7])
{
    if (!str) {
        return false;
    }
    str[0] = '\0';

    uint8_t rbuf[6]{};
    if (readRegister(READ_DESCRIPTION_REG, rbuf, m5::stl::size(rbuf), 0)) {
        memcpy((uint8_t*)str, rbuf, m5::stl::size(rbuf));
        str[m5::stl::size(rbuf)] = '\0';
        return true;
    }
    return false;
}

bool UnitDDS::readMode(Mode& mode)
{
    mode = Mode::Reserved;
    uint8_t v{};
    if (readRegister8(MODE_REG, v, 0)) {
        mode = static_cast<Mode>(v & 0x07);
        return true;
    }
    return false;
}

bool UnitDDS::writeMode(const Mode mode)
{
    uint8_t v{};
    uint8_t ctrl{};
    if (readRegister8(MODE_REG, v, 0) && read_control(ctrl)) {
        Mode old        = (Mode)(v & 0x07);
        bool write_freq = (old == Mode::Sawtooth || old == Mode::DC) && (mode != Mode::Sawtooth && mode != Mode::DC);
        v               = (v & ~0x07) | m5::stl::to_underlying(mode);
        // *** From Firmware Implementation ***
        // Ctrl must also be re-written to reflect the mode change
        // When SAWTOOH/DC mode is selected, the internal ferq is set to 0, so it is set back.
        return write_register8(MODE_REG, v) && write_register8(CONTROL_REG, ctrl) &&
               (write_freq ? (writeFrequency0(_freq[0]) && writeFrequency1(_freq[1])) : true);
    }
    return false;
}

bool UnitDDS::writeFrequency(const bool select, const uint32_t freq)
{
    if (!is_valid_frequency(freq)) {
        M5_LIB_LOGE("freq must be between %u and %u (%u)", MINIMUM_FREQ, MAXIMUM_FREQ, freq);
        return false;
    }
    uint32_t ftw = calculate_ftw(freq);
    uint8_t buf[4]{};
    buf[0] = ((ftw >> 24) & 0x0F) | (select ? 0xC0 : 0x80);
    buf[1] = (ftw >> 16) & 0xFF;
    buf[2] = (ftw >> 8) & 0xFF;
    buf[3] = ftw & 0xFF;

    _freq[(int)select] = 0;
    if (!writeRegister(FREQUENCY_REG, buf, m5::stl::size(buf))) {
        return false;
    }
    _freq[(int)select] = freq;
    return true;
}

bool UnitDDS::writePhase(const bool select, const uint16_t deg)
{
    uint16_t ph = calculate_phase(deg);
    uint8_t buf[2]{};
    buf[0] = ((ph >> 8) & 0x07) | (select ? 0xC0 : 0x80);
    buf[1] = ph & 0xFF;
    return writeRegister(PHASE_REG, buf, m5::stl::size(buf));
}

bool UnitDDS::writeFrequencyAndPhase(const bool select_freq, const uint32_t freq, const bool select_phase,
                                     const uint16_t deg)
{
    if (!is_valid_frequency(freq)) {
        M5_LIB_LOGE("freq must be between %u and %u (%u)", MINIMUM_FREQ, MAXIMUM_FREQ, freq);
        return false;
    }

    uint32_t ftw = calculate_ftw(freq);
    uint16_t ph  = calculate_phase(deg);
    uint8_t buf[6]{};
    buf[0] = ((ftw >> 24) & 0x0F) | (select_freq ? 0xC0 : 0x80);
    buf[1] = (ftw >> 16) & 0xFF;
    buf[2] = (ftw >> 8) & 0xFF;
    buf[3] = ftw & 0xFF;
    buf[4] = ((ph >> 8) & 0x07) | (select_phase ? 0xC0 : 0x80);
    buf[5] = ph & 0xFF;
    // M5_LIB_LOGE("%02X:%02X:%02X:%02X:%02X:%02X", buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);

    _freq[(int)select_freq] = 0;
    if (writeRegister(FREQUENCY_REG, buf, m5::stl::size(buf))) {
        _freq[(int)select_freq] = freq;
        return true;
    }
    return false;
}

bool UnitDDS::writeCurrent(const bool select_freq, const bool select_phase)
{
    uint8_t ctrl{};
    if (read_control(ctrl)) {
        ctrl &= ~0x60;
        ctrl |= select_freq ? 0x40 : 0x00;
        ctrl |= select_phase ? 0x20 : 0x00;
        return write_register8(CONTROL_REG, ctrl);
    }
    return false;
}

bool UnitDDS::writeCurrentFrequency(const bool select)
{
    uint8_t ctrl{};
    if (read_control(ctrl)) {
        ctrl &= ~0x40;
        ctrl |= select ? 0x40 : 0x00;
        return write_register8(CONTROL_REG, ctrl);
    }
    return false;
}

bool UnitDDS::writeCurrentPhase(const bool select)
{
    uint8_t ctrl{};
    if (read_control(ctrl)) {
        ctrl &= ~0x20;
        ctrl |= select ? 0x20 : 0x00;
        return write_register8(CONTROL_REG, ctrl);
    }
    return false;
}

bool UnitDDS::writeOutput(const dds::Mode mode, const bool select, const uint32_t freq, const uint16_t deg)
{
    if (!is_valid_frequency(freq)) {
        M5_LIB_LOGE("freq must be between %u and %u (%u)", MINIMUM_FREQ, MAXIMUM_FREQ, freq);
        return false;
    }
    return writeFrequencyAndPhase(select, freq, select, deg) && writeMode(mode) && writeCurrent(select, select);
}

bool UnitDDS::sleep(const bool mclk, const bool DAC)
{
    if (!mclk && !DAC) {
        M5_LIB_LOGE("Sleep Target must be specified");
        return false;
    }

    uint8_t ctrl{};
    if (read_control(ctrl)) {
        ctrl &= ~0x18;
        ctrl |= mclk ? 0x10 : 0x00;
        ctrl |= DAC ? 0x08 : 0x00;
        return write_register8(CONTROL_REG, ctrl);
    }
    return false;
}

bool UnitDDS::wakeup()
{
    uint8_t ctrl{};
    if (read_control(ctrl)) {
        ctrl &= ~0x1C;  // SLEEP1,2,RESET to 0
        if (write_register8(CONTROL_REG, ctrl)) {
            // DAC outputs are enabled and updated 7 to 8 MCLK cycles after the RESET bit is set back to 0
            // About 0.8 us if MCLK is 10Mhz
            m5::utility::delayMicroseconds(2);  // A little longer
            return true;
        }
    }
    return false;
}

bool UnitDDS::reset()
{
    uint8_t ctrl{};
    if (read_control(ctrl)) {
        ctrl |= 0x04;
        return write_register8(CONTROL_REG, ctrl);
    }
    return false;
}

bool UnitDDS::read_control(uint8_t& ctrl)
{
    return readRegister8(CONTROL_REG, ctrl, 0);
}

bool UnitDDS::write_register8(const uint8_t reg, const uint8_t v)
{
    return writeRegister8(reg, v | 0x80);
}

}  // namespace unit
}  // namespace m5
