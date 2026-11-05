/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*!
  @file unit_DDS.hpp
  @brief DDS Unit for M5UnitUnified
*/
#ifndef M5_UNIT_DDS_UNIT_DDS_HPP
#define M5_UNIT_DDS_UNIT_DDS_HPP

#include <M5UnitComponent.hpp>

namespace m5 {
namespace unit {

/*!
  @namespace dds
  @brief For UnitDDS
 */
namespace dds {

/*!
  @enum Mode
  @brief Output mode
 */
enum class Mode : uint8_t {
    Reserved,  //!< Reserved
    Sin,       //!< Sin wave
    Triangle,  //!< Triangle wave
    Square,    //!< Square wave
    Sawtooth,  //!< Sawtooth wave (M5 extension)
               //!< Fixed at frequency 13600 and phase 0
    DC,        //!< DC (M5 extension)
};

}  // namespace dds

/*!
  @class m5::unit::UnitDDS
  @brief DDS unit with AD9833
*/
class UnitDDS : public Component {
    M5_UNIT_COMPONENT_HPP_BUILDER(UnitDDS, 0x31);

public:
    /*!
      @struct config_t
      @brief Settings for begin
     */
    struct config_t {
        bool start_output{true};         //!< Start output on begin if true
        dds::Mode mode{dds::Mode::Sin};  //!< Output mode if start output on begin
        bool select{};                   //!< Using bank if start output on begin
        uint32_t freq{10000};            //!< Frequency if start output on begin
        uint16_t deg{0};                 //!< Phase if start output on begin
    };

    explicit UnitDDS(const uint8_t addr = DEFAULT_ADDRESS) : Component(addr)
    {
        auto ccfg  = component_config();
        ccfg.clock = 400 * 1000U;
        component_config(ccfg);
    }
    virtual ~UnitDDS()
    {
    }

    virtual bool begin() override;

    ///@name Settings for begin
    ///@{
    /*! @brief Gets the configration */
    inline config_t config()
    {
        return _cfg;
    }
    //! @brief Set the configration
    inline void config(const config_t& cfg)
    {
        _cfg = cfg;
    }
    ///@}

    ///@name Properties
    ///@{
    //! @brief Gets written frequency 0 (Hz)
    uint16_t frequency0() const
    {
        return _freq[0];
    }
    //! @brief Gets written frequency 1 (Hz)
    uint16_t frequency1() const
    {
        return _freq[1];
    }
    ///@}

    ///@name Output mode
    ///@{
    /*!
      @brief Read the mode
      @param[out] mode Mode
      @return True if successful
     */
    bool readMode(dds::Mode& mode);
    /*!
      @brief Write the mode
      @param mode Mode
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    bool writeMode(const dds::Mode mode);
    ///@}

    ///@name Settings
    ///@{
    /*!
      @brief Write the frequency
      @param select Target bank 0 if false, bank 1 if true
      @param freq Frequency(Hz) 0 - 1Mhz
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    bool writeFrequency(const bool select, const uint32_t freq);
    /*!
      @brief Write the frequency to bank 0
      @param freq Frequency(Hz) 0 - 1Mhz
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    inline bool writeFrequency0(const uint32_t freq)
    {
        return writeFrequency(false, freq);
    }
    /*!
      @brief Write the frequency to bank 1
      @param freq Frequency(Hz) 0 - 1Mhz
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    inline bool writeFrequency1(const uint32_t freq)
    {
        return writeFrequency(true, freq);
    }
    /*!
      @brief Write the phase
      @param select Target bank 0 if false, bank 1 if true
      @param deg Phase (degree)
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    bool writePhase(const bool select, const uint16_t deg);
    /*!
      @brief Write the phase to bank 0
      @param deg Phase (degree)
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    inline bool writePhase0(const uint16_t deg)
    {
        return writePhase(false, deg);
    }
    /*!
      @brief Write the phase to bank 1
      @param deg Phase (degree)
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    inline bool writePhase1(const uint16_t deg)
    {
        return writePhase(true, deg);
    }
    /*!
      @brief Write the frequency and phase
      @param select_freq  Frequency target bank 0 if false, bank 1 if true
      @param freq Frequency(Hz) 0 - 1Mhz
      @param select_freq  Phase target bank 0 if false, bank 1 if true
      @param deg Phase (degree)
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    bool writeFrequencyAndPhase(const bool select_freq, const uint32_t freq, const bool select_phase,
                                const uint16_t deg);
    /*!
      @brief Write which bank setting to use
      @param select_freq  Frequecny using  bank 0 if false, bank 1 if true
      @param select_freq  Phase using bank 0 if false, bank 1 if true
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    bool writeCurrent(const bool select_freq, const bool select_phase);
    /*!
      @brief Write which bank frequency setting to use
      @param select_freq  Frequecny using  bank 0 if false, bank 1 if true
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    bool writeCurrentFrequency(const bool select);
    /*!
      @brief Write which bank phase setting to use
      @param select_freq  Phase using bank 0 if false, bank 1 if true
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    bool writeCurrentPhase(const bool select);
    ///@}

    ///@name Operation
    ///@{
    /*!
      @brief Write mode and settings
      @param mode Mode
      @param select Using bank
      @param freq Frequency(Hz) 0 - 1Mhz
      @param deg Phase (degree)
      @return True if successful
      @warning Frequency and phase settings are ignored for Mode::Sawtooth and Mode::DC
     */
    bool writeOutput(const dds::Mode mode, const bool select, const uint32_t freq, const uint16_t deg);
    /*!
      @brief Sleep
      @param mclk Sleep mclk (Keep output current value)
      @param DAC Sleep DAC (Stop output)
      @return True if successful
     */
    bool sleep(const bool mclk = true, const bool DAC = true);
    /*!
      @brief Wake up
      @return True if successful
      @note Wake up from sleep or reset
     */
    bool wakeup();
    /*!
      @brief Reset
      @return True if successful
      @note Fixes DAC output to mid-scale
     */
    bool reset();
    ///@}

    /*!
      @brief Read the description
      @param[out] str Description string buffer (at least 7 bytes)
      @return True if successful
      @note returns "ad9833" if M5Unit-DDS
     */
    bool readDescription(char str[7]);

protected:
    bool read_control(uint8_t& ctrl);
    bool write_register8(const uint8_t reg, const uint8_t v);

private:
    config_t _cfg{};
    dds::Mode _mode{};
    uint16_t _freq[2]{};
};

namespace dds {
namespace command {
///@cond
constexpr uint8_t READ_DESCRIPTION_REG{0x10};
constexpr uint8_t MODE_REG{0x20};
constexpr uint8_t CONTROL_REG{0x21};
constexpr uint8_t FREQUENCY_REG{0x30};
constexpr uint8_t PHASE_REG{0x34};
///@endcond
}  // namespace command
}  // namespace dds

}  // namespace unit
}  // namespace m5
#endif
