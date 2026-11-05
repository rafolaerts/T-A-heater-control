/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
  UnitTest for UnitDDS
*/
#include <gtest/gtest.h>
#include <Wire.h>
#include <M5Unified.h>
#include <M5UnitUnified.hpp>
#include <googletest/test_template.hpp>
#include <googletest/test_helper.hpp>
#include <unit/unit_DDS.hpp>
#include <chrono>
#include <thread>
#include <iostream>
#include <random>
#include <algorithm>

using namespace m5::unit::googletest;
using namespace m5::unit;
using namespace m5::unit::dds;
using namespace m5::unit::dds::command;
using m5::unit::types::elapsed_time_t;

const ::testing::Environment* global_fixture = ::testing::AddGlobalTestEnvironment(new GlobalFixture<400000U>());

class TestDDS : public ComponentTestBase<UnitDDS, bool> {
protected:
    virtual UnitDDS* get_instance() override
    {
        auto ptr = new m5::unit::UnitDDS();
        return ptr;
    }
    virtual bool is_using_hal() const override
    {
        return GetParam();
    };
};

// INSTANTIATE_TEST_SUITE_P(ParamValues, TestDDS, ::testing::Values(false, true));
// INSTANTIATE_TEST_SUITE_P(ParamValues, TestDDS, ::testing::Values(true));
INSTANTIATE_TEST_SUITE_P(ParamValues, TestDDS, ::testing::Values(false));

namespace {

auto rng = std::default_random_engine{};
constexpr uint32_t MINIMUM_FREQ{0};
constexpr uint32_t MAXIMUM_FREQ{1000000};

constexpr Mode mode_table[] = {
    Mode::Sin, Mode::Triangle, Mode::Square, Mode::Sawtooth, Mode::DC,
};

constexpr uint32_t valid_freq_table[] = {
    MINIMUM_FREQ,
    MAXIMUM_FREQ / 2,
    MAXIMUM_FREQ,
};
constexpr uint32_t invalid_freq_table[] = {MAXIMUM_FREQ + 1, std::numeric_limits<uint32_t>::max()};
constexpr uint16_t deg_table[]          = {0, 180, 360, 361, std::numeric_limits<uint16_t>::max()};
constexpr bool bank_table[]             = {false, true};

uint8_t read_control(UnitDDS* u)
{
    uint8_t v{};
    u->readRegister8(CONTROL_REG, v, 0);
    return v;
}

}  // namespace

TEST_P(TestDDS, Basic)
{
    SCOPED_TRACE(ustr);

    char desc[7]{};
    EXPECT_TRUE(unit->readDescription(desc));
    EXPECT_TRUE(strcmp(desc, "ad9833") == 0) << desc;
}

TEST_P(TestDDS, Mode)
{
    SCOPED_TRACE(ustr);

    for (auto&& mode : mode_table) {
        EXPECT_TRUE(unit->writeMode(mode));
        Mode m{};
        EXPECT_TRUE(unit->readMode(m));
        EXPECT_EQ(m, mode);
    }
}

TEST_P(TestDDS, Settings)
{
    SCOPED_TRACE(ustr);

    for (auto&& mode : mode_table) {
        EXPECT_TRUE(unit->writeMode(mode));
        Mode m{};
        EXPECT_TRUE(unit->readMode(m));
        EXPECT_EQ(m, mode);

        //
        for (auto&& f : valid_freq_table) {
            EXPECT_TRUE(unit->writeFrequency(false, f)) << f;
            EXPECT_TRUE(unit->writeFrequency(true, f)) << f;
        }
        for (auto&& f : invalid_freq_table) {
            EXPECT_FALSE(unit->writeFrequency(false, f)) << f;
            EXPECT_FALSE(unit->writeFrequency(true, f)) << f;
        }

        //
        for (auto&& d : deg_table) {
            EXPECT_TRUE(unit->writePhase(false, d)) << d;
            EXPECT_TRUE(unit->writePhase(true, d)) << d;
        }

        //
        for (auto&& fb : bank_table) {
            for (auto&& db : bank_table) {
                for (auto&& f : valid_freq_table) {
                    for (auto&& d : deg_table) {
                        auto s = m5::utility::formatString("%u:%u %u:%u", fb, f, db, d);
                        SCOPED_TRACE(s);
                        EXPECT_TRUE(unit->writeFrequencyAndPhase(fb, f, db, d));
                    }
                }
            }
        }

        for (auto&& fb : bank_table) {
            for (auto&& db : bank_table) {
                for (auto&& f : invalid_freq_table) {
                    for (auto&& d : deg_table) {
                        auto s = m5::utility::formatString("%u:%u %u:%u", fb, f, db, d);
                        SCOPED_TRACE(s);
                        EXPECT_FALSE(unit->writeFrequencyAndPhase(fb, f, db, d));
                    }
                }
            }
        }

        //
        for (auto&& fb : bank_table) {
            EXPECT_TRUE(unit->writeCurrentFrequency(fb));
            uint8_t c = read_control(unit.get());
            EXPECT_EQ((fb ? 0x40 : 0x00), c & (fb ? 0x40 : 0x00)) << c;

            for (auto&& db : bank_table) {
                auto s = m5::utility::formatString("%u:%u", fb, db);
                SCOPED_TRACE(s);

                EXPECT_TRUE(unit->writeCurrent(fb, db));
                c = read_control(unit.get());
                EXPECT_EQ((fb ? 0x40 : 0x00), c & (fb ? 0x40 : 0x00)) << c;
                EXPECT_EQ((db ? 0x20 : 0x00), c & (db ? 0x20 : 0x00)) << c;

                EXPECT_TRUE(unit->writeCurrentPhase(db));
                c = read_control(unit.get());
                EXPECT_EQ((db ? 0x20 : 0x00), c & (db ? 0x20 : 0x00)) << c;
            }
        }
    }
}

TEST_P(TestDDS, Output)
{
    SCOPED_TRACE(ustr);

    for (auto&& m : mode_table) {
        for (auto&& b : bank_table) {
            for (auto&& f : valid_freq_table) {
                for (auto&& d : deg_table) {
                    auto s = m5::utility::formatString("%u:%u:%u:%u", m, b, f, d);
                    SCOPED_TRACE(s);
                    EXPECT_TRUE(unit->writeOutput(m, b, f, d));
                }
            }
        }
    }

    for (auto&& m : mode_table) {
        for (auto&& b : bank_table) {
            for (auto&& f : invalid_freq_table) {
                for (auto&& d : deg_table) {
                    auto s = m5::utility::formatString("%u:%u:%u:%u", m, b, f, d);
                    SCOPED_TRACE(s);
                    EXPECT_FALSE(unit->writeOutput(m, b, f, d));
                }
            }
        }
    }
}

TEST_P(TestDDS, Sleep)
{
    SCOPED_TRACE(ustr);

    EXPECT_FALSE(unit->sleep(false, false));

    EXPECT_TRUE(unit->sleep(true, false));
    uint8_t c = read_control(unit.get());
    EXPECT_EQ(0x10, c & 0x10) << c;

    EXPECT_TRUE(unit->sleep(false, true));
    c = read_control(unit.get());
    EXPECT_EQ(0x08, c & 0x08) << c;

    EXPECT_TRUE(unit->sleep());  // true,true
    c = read_control(unit.get());
    EXPECT_EQ(0x18, c & 0x18) << c;

    EXPECT_TRUE(unit->reset());
    c = read_control(unit.get());
    EXPECT_EQ(0x04, c & 0x04) << c;

    EXPECT_TRUE(unit->wakeup());
    c = read_control(unit.get());
    EXPECT_EQ(0, c & 0x1C);
}
