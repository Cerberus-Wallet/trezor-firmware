from typing import *


# extmod/modcerberusio/modcerberusio-flash.h
class FlashOTP:
    """
    """

    def __init__(self) -> None:
        """
        """

    def write(self, block: int, offset: int, data: bytes) -> None:
        """
        Writes data to OTP flash
        """

    def read(self, block: int, offset: int, data: bytearray) -> None:
        """
        Reads data from OTP flash
        """

    def lock(self, block: int) -> None:
        """
        Lock OTP flash block
        """

    def is_locked(self, block: int) -> bool:
        """
        Is OTP flash block locked?
        """


# extmod/modcerberusio/modcerberusio-hid.h
class HID:
    """
    USB HID interface configuration.
    """

    def __init__(
        self,
        iface_num: int,
        ep_in: int,
        ep_out: int,
        emu_port: int,
        report_desc: bytes,
        subclass: int = 0,
        protocol: int = 0,
        polling_interval: int = 1,
        max_packet_len: int = 64,
    ) -> None:
        """
        """

    def iface_num(self) -> int:
        """
        Returns the configured number of this interface.
        """

    def write(self, msg: bytes) -> int:
        """
        Sends message using USB HID (device) or UDP (emulator).
        """

    def write_blocking(self, msg: bytes, timeout_ms: int) -> int:
        """
        Sends message using USB HID (device) or UDP (emulator).
        """


# extmod/modcerberusio/modcerberusio-poll.h
def poll(ifaces: Iterable[int], list_ref: list, timeout_ms: int) -> bool:
    """
    Wait until one of `ifaces` is ready to read or write (using masks
    `list_ref`:
    `list_ref[0]` - the interface number, including the mask
    `list_ref[1]` - for touch event, tuple of:
                    (event_type, x_position, y_position)
                  - for button event (T1), tuple of:
                    (event type, button number)
                  - for USB read event, received bytes
    If timeout occurs, False is returned, True otherwise.
    """


# extmod/modcerberusio/modcerberusio-sbu.h
class SBU:
    """
    """

    def __init__(self) -> None:
        """
        """

    def set(self, sbu1: bool, sbu2: bool) -> None:
        """
        Sets SBU wires to sbu1 and sbu2 values respectively
        """


# extmod/modcerberusio/modcerberusio-usb.h
class USB:
    """
    USB device configuration.
    """

    def __init__(
        self,
        vendor_id: int,
        product_id: int,
        release_num: int,
        device_class: int = 0,
        device_subclass: int = 0,
        device_protocol: int = 0,
        manufacturer: str = "",
        product: str = "",
        interface: str = "",
        usb21_enabled: bool = True,
        usb21_landing: bool = True,
    ) -> None:
        """
        """

    def add(self, iface: HID | VCP | WebUSB) -> None:
        """
        Registers passed interface into the USB stack.
        """

    def open(self, serial_number: str) -> None:
        """
        Initializes the USB stack.
        """

    def close(self) -> None:
        """
        Cleans up the USB stack.
        """


# extmod/modcerberusio/modcerberusio-vcp.h
class VCP:
    """
    USB VCP interface configuration.
    """

    def __init__(
        self,
        iface_num: int,
        data_iface_num: int,
        ep_in: int,
        ep_out: int,
        ep_cmd: int,
        emu_port: int,
    ) -> None:
        """
        """

    def iface_num(self) -> int:
        """
        Returns the configured number of this interface.
        """


# extmod/modcerberusio/modcerberusio-webusb.h
class WebUSB:
    """
    USB WebUSB interface configuration.
    """

    def __init__(
        self,
        iface_num: int,
        ep_in: int,
        ep_out: int,
        emu_port: int,
        subclass: int = 0,
        protocol: int = 0,
        polling_interval: int = 1,
        max_packet_len: int = 64,
    ) -> None:
        """
        """

    def iface_num(self) -> int:
        """
        Returns the configured number of this interface.
        """

    def write(self, msg: bytes) -> int:
        """
        Sends message using USB WebUSB (device) or UDP (emulator).
        """
from . import fatfs, sdcard
POLL_READ: int  # wait until interface is readable and return read data
POLL_WRITE: int  # wait until interface is writable
TOUCH: int  # interface id of the touch events
TOUCH_START: int  # event id of touch start event
TOUCH_MOVE: int  # event id of touch move event
TOUCH_END: int  # event id of touch end event
BUTTON: int  # interface id of button events
BUTTON_PRESSED: int  # button down event
BUTTON_RELEASED: int  # button up event
BUTTON_LEFT: int  # button number of left button
BUTTON_RIGHT: int  # button number of right button
USB_CHECK: int # interface id for check of USB data connection
WireInterface = Union[HID, WebUSB]
