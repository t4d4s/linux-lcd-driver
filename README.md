# 🖥️👻 Linux LCD Driver

> A Linux kernel module and user-space application for controlling an LCD display via the `/dev` filesystem.

---

## Overview

`Linux LCD Driver` is a loadable kernel module (LKM) written in C that exposes an LCD display as a character device. A companion user-space application communicates with the driver through the device file, allowing text and commands to be sent to the LCD from any program on the system.

## Repository Structure

```
linux-lcd-driver/
├── kernel-module/    # Loadable kernel module source (LKM)
└── user-space/       # User-space application for interacting with the driver
```

---

## Requirements

- Linux kernel headers matching your running kernel
- GCC and GNU Make
- Root privileges (for loading/unloading the module)

On Ubuntu/Debian:

```bash
sudo apt install build-essential linux-headers-$(uname -r)
```

---

## Building

### Kernel module

```bash
cd kernel-module
make
```

This produces `lcd_driver.ko` — the compiled kernel module.

### User-space application

```bash
cd user-space
make
```

---

## Usage

### Load the module

```bash
sudo insmod lcd_driver.ko
```

### Confirm it loaded

```bash
dmesg | tail
lsmod | grep lcd_driver
```

### Send text to the LCD

Once the module is loaded and the device file is available under `/dev`, use the user-space application or write directly to the device:

```bash
# Using the user-space app
./lcd "Hello, World!"

# Or write directly to the device file
echo "Hello, World!" | sudo tee /dev/lcd_driver
```

### Unload the module

```bash
sudo rmmod lcd_driver
```

---

## Cleaning Build Outputs

```bash
make clean
```

Removes all generated `.ko`, `.o`, `.mod`, and `.cmd` files.

---

## Contributions

Contributions are welcome! If you'd like to improve the project or add new features, please submit a pull request.

---

## Author

This project is maintained by [Tadas](https://github.com/t4d4s). Feel free to reach out with any questions or feedback.
