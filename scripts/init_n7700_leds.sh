#!/bin/sh

device=/proc/ich7_gpio

echo 'deleteled system_led' > $device
echo 'createled system_led orange' > $device
echo 'addledcmd system_led bright_0 GPIO_USE_SEL2 3 1' > $device
echo 'addledcmd system_led bright_0 GP_IO_SEL2 3 0' > $device
echo 'addledcmd system_led bright_0 GP_LVL2 3 1' > $device
echo 'addledcmd system_led bright_1 GPIO_USE_SEL2 3 1' > $device
echo 'addledcmd system_led bright_1 GP_IO_SEL2 3 0' > $device
echo 'addledcmd system_led bright_1 GP_LVL2 3 0' > $device
echo 'addledcmd system_led get_bright GP_LVL2 3 1' > $device
echo 'runledcmd system_led bright_0' > $device
echo 'registerled system_led' > $device

echo 'deleteled usb_copy' > $device
echo 'createled usb_copy blue' > $device
echo 'addledcmd usb_copy bright_0 GPO_BLINK 24 0' > $device
echo 'addledcmd usb_copy bright_0 GPIO_USE_SEL 24 1' > $device
echo 'addledcmd usb_copy bright_0 GP_IO_SEL 24 0' > $device
echo 'addledcmd usb_copy bright_0 GP_LVL 24 1' > $device
echo 'addledcmd usb_copy bright_1 GPO_BLINK 24 0' > $device
echo 'addledcmd usb_copy bright_1 GPIO_USE_SEL 24 1' > $device
echo 'addledcmd usb_copy bright_1 GP_IO_SEL 24 0' > $device
echo 'addledcmd usb_copy bright_1 GP_LVL 24 0' > $device
echo 'addledcmd usb_copy get_bright GP_LVL 24 1' > $device
echo 'addledcmd usb_copy blink_on GPO_BLINK 24 1' > $device
echo 'addledcmd usb_copy blink_on GPIO_USE_SEL 24 1' > $device
echo 'addledcmd usb_copy blink_on GP_IO_SEL 24 0' > $device
echo 'addledcmd usb_copy blink_on GP_LVL 24 0' > $device
echo 'addledcmd usb_copy get_blink GPO_BLINK 24 0' > $device
echo 'runledcmd usb_copy bright_0' > $device
echo 'registerled usb_copy' > $device

echo 'deleteled fail' > $device
echo 'createled fail red' > $device
echo 'addledcmd fail bright_0 GPO_BLINK 25 0' > $device
echo 'addledcmd fail bright_0 GPIO_USE_SEL 25 1' > $device
echo 'addledcmd fail bright_0 GP_IO_SEL 25 0' > $device
echo 'addledcmd fail bright_0 GP_LVL 25 1' > $device
echo 'addledcmd fail bright_1 GPO_BLINK 25 0' > $device
echo 'addledcmd fail bright_1 GPIO_USE_SEL 25 1' > $device
echo 'addledcmd fail bright_1 GP_IO_SEL 25 0' > $device
echo 'addledcmd fail bright_1 GP_LVL 25 0' > $device
echo 'addledcmd fail get_bright GP_LVL 25 1' > $device
echo 'addledcmd fail blink_on GPO_BLINK 25 1' > $device
echo 'addledcmd fail blink_on GPIO_USE_SEL 25 1' > $device
echo 'addledcmd fail blink_on GP_IO_SEL 25 0' > $device
echo 'addledcmd fail blink_on GP_LVL 25 0' > $device
echo 'addledcmd fail get_blink GPO_BLINK 25 0' > $device
echo 'runledcmd fail bright_0' > $device
echo 'registerled fail' > $device

