Replace AS2305 and 1M Resistor with ~~[[GLF71311]]~~  TPS22917DBVT for proper slew rate control.
	Remove PWM soft start loop in `periph_3v3_on()`

[[SnailScale FLPR Bare Metal]]
	FLPR runs a full zephyr build from sram requiring 64kb in both overlay files
	This needs to be revisited for better power management

~~[[AT42QT1010 as GPIO Hardware Trigger]]
	Rewire VIN to draw power from SOURCE or 3v3.
	Alternatively, find a different touch IC that lets you configure low power timing (more research on this later)~

Add [[SYSTEM OFF and Wake Up]] by turning off FLPR and keeping IMU powered to feed GPIO interrupt based on picking up the scale

Check internal micro resistors on silicon, might be able to get more power savings by using external resistors instead?