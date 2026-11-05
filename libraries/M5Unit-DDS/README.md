# M5Unit - DDS

## Overview

### SKU:U105

DDS is a signal source Unit. It uses the AD9833 programmable waveform generator + STM32F0 micro controller. Based on I2C communication interface (addr:0x31) It can easily control the signal source to output multiple waveforms (sine wave, triangle wave, square wave output, sawtooth wave, signal output amplitude 0-0.6V) and adjust the frequency and phase.

It supports deep sleep mode, which can reduce the power consumption in the idle state. The Unit is suitable for the electronic circuit prototype design of various test instruments as a signal source.

## Related Link

- [Unit DDS - Document & Datasheet](https://docs.m5stack.com/en/unit/dds)

## License

- [M5Unit-DDS - MIT](LICENSE)


## M5UnitUnified
Library for Unit DDS using [M5UnitUnified](https://github.com/m5stack/M5UnitUnified).  
M5UnitUnified is a library for unified handling of various M5 units products.

### Supported units 
- Unit DDS (SKU:U105)

### Required Libraries:
- [M5UnitUnified](https://github.com/m5stack/M5UnitUnified)
- [M5Utility](https://github.com/m5stack/M5Utility)
- [M5HAL](https://github.com/m5stack/M5HAL)

See also [examples/UnitUnified](examples/UnitUnified)

### Doxygen document
[GitHub Pages](https://m5stack.github.io/M5Unit-DDS/)

If you want to generate documents on your local machine, execute the following command

```
bash docs/doxy.sh
```

It will output it under docs/html  
If you want to output Git commit hashes to html, do it for the git cloned folder.

#### Required
- [Doxygen](https://www.doxygen.nl/)
- [pcregrep](https://formulae.brew.sh/formula/pcre2)
- [Git](https://git-scm.com/) (Output commit hash to html)

