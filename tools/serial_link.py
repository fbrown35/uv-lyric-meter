"""USB Serial/JTAG connection that preserves control lines and disables hangup resets."""
import termios
import serial

class NoResetSerial(serial.Serial):
    # Serial.open normally toggles DTR and then RTS separately, which can reset P4.
    def _update_dtr_state(self):
        pass

    def _update_rts_state(self):
        pass

    def open(self):
        super().open()
        attributes = termios.tcgetattr(self.fileno())
        attributes[2] &= ~termios.HUPCL
        termios.tcsetattr(self.fileno(), termios.TCSANOW, attributes)

def open_device():
    return NoResetSerial('/dev/cu.usbmodem83201',115200,timeout=.2)
