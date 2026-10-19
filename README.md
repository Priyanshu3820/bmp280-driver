# BMP280 Driver with User Space Program

This project provides a complete BMP280 barometric sensor driver for Linux kernel with a user space program to read temperature, pressure, and altitude data.

## Components

1. **Kernel Module (`driver.c`)**:
   - I2C driver for BMP280 sensor
   - Reads temperature and pressure
   - Calculates altitude from pressure and temperature
   - Provides character device `/dev/bmp280` for user space access

2. **User Space Program (`bmp280_reader.c`)**:
   - Opens the device file `/dev/bmp280`
   - Reads sensor data continuously
   - Displays temperature, pressure, and altitude separately

## Files

### Kernel Module
- `driver.c`: Complete BMP280 driver implementation
- `Makefile`: Builds BOTH the kernel module and the user space reader

### User Space Program
- `bmp280_reader.c`: User space program to read sensor data
- `user_space/Makefile`: Build script for the user space program

## Compilation Instructions

### Build everything (recommended)

Run this from the project root:

```bash
make
```

This produces:
- `driver.ko` (kernel module, in the project root)
- `user_space/bmp280_reader` (the user space executable)

> **Note:** The executable is created inside the `user_space/` directory,
> not in the project root. This is a common reason people think the
> executable was not built.

### Build only the kernel module

```bash
make module
```

### Build only the user space program

```bash
make user
# or
make -C user_space
```

### Build the user space program manually (no Makefile)

```bash
gcc -Wall -Wextra -O2 -o bmp280_reader bmp280_reader.c
```

This puts the executable named `bmp280_reader` in the current directory.
(If you omit `-o`, gcc creates `a.out` instead.)

## Loading and Running

```bash
# Load the kernel module (requires root)
sudo insmod driver.ko

# Run the user space program (requires root for device access)
sudo ./user_space/bmp280_reader

# Unload the kernel module when done
sudo rmmod driver
```

## Usage

The user space program continuously reads and displays each reading
on its own line:

```
Temperature: 25.32 °C
Pressure: 101325 Pa
Altitude: 42.00 meters
```

The program updates every second. Press Ctrl+C to exit.

## Driver Features

- **Temperature**: Measured with oversampling x16 accuracy
- **Pressure**: Measured with oversampling x16 accuracy
- **Altitude**: Calculated using barometric formula with temperature correction
- **Calibration**: Uses factory-calibrated coefficients (dig_T and dig_P)

## Hardware Requirements

- BMP280 barometric sensor
- I2C communication interface
- Proper kernel headers installed

## Dependencies

- Linux kernel development headers
- I2C device support
- GCC compiler for user space program

## Troubleshooting

### "I compiled but there is no executable"

1. The root `Makefile` builds **both** outputs. After `make`, look for the
   executable at `user_space/bmp280_reader` (not in the project root).
2. If you ran the kernel-module build only, the user program was not built.
   Run `make user` or `make -C user_space`.
3. If you compiled by hand with `gcc bmp280_reader.c` (no `-o`), gcc wrote
   `a.out`, not `bmp280_reader`. Use `gcc -o bmp280_reader bmp280_reader.c`.
4. Confirm the source file exists next to the Makefile or one directory up:
   `ls bmp280_reader.c user_space/`
5. Check the binary exists and is executable: `ls -l user_space/bmp280_reader`

### "No such device" error

1. Ensure the kernel module is loaded: `lsmod | grep driver`
2. Check if the device file exists: `ls -la /dev/bmp280`
3. Verify kernel module compilation: `make module`

### Permission denied

1. Run the program with sudo: `sudo ./user_space/bmp280_reader`
2. Check device file permissions: `ls -la /dev/bmp280`

### I2C communication errors

1. Ensure sensor is properly connected
2. Verify I2C bus number in driver code (`I2C_BUS_AVAILABLE`)
3. Check if the device is recognized: `i2cdetect -y 1`
