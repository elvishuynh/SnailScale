Replace AS2305 and 1M Resistor with [[GLF71311]] for proper slew rate control.
	Remove PWM soft start loop in `periph_3v3_on()`

  
[[SnailScale FLPR Bare Metal]]
	FLPR runs a full zephyr build from sram requiring 64kb in both overlay files
	This needs to be revisited for better power management
