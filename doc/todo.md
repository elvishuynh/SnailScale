Replace AS2305 and 1M Resistor with [[GLF71311]] for proper slew rate control.
	Remove PWM soft start loop in `periph_3v3_on()`
  
[[SnailScale FLPR Bare Metal]]
	FLPR runs a full zephyr build from sram requiring 64kb in both overlay files
	This needs to be revisited for better power management

[[AT42QT1010 as GPIO Hardware Trigger]]
	Rewire VIN to draw power from SOURCE or 3v3 so we can use it to turn scale on and off
	Alternatively, find a different touch IC that lets you configure low power timing (more research on this later)