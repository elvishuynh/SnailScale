~~Replace AS2305 and 1M Resistor with [[GLF71311]]  TPS22917DBVT for proper slew rate control.~~
	~~Remove PWM soft start loop in `periph_3v3_on()`~~
	- TPS22917 load switch active on D10 (P1.06)

[[SnailScale FLPR Bare Metal]]
	FLPR runs a full zephyr build from sram requiring 64kb in both overlay files
	This needs to be revisited for better power management

~~[[AT42QT1010 as GPIO Hardware Trigger]]
	Rewire VIN to draw power from SOURCE or 3v3.
	Alternatively, find a different touch IC that lets you configure low power timing (more research on this later)~~
	- Replaced with Azoteq IQS231B00004000TSR on D5 (IO1/SCL) and D6 (IO2/SDA)
	- Startup bit-bang I2C sequence switches IQS231B to standalone mode, D6 floats at runtime for normal sensitivity

[[PT18LEDV0 CT Slew Rate Capacitor Tuning]]
	- Slew rate capacitor on TPS22917 CT pin needs tuning to limit inrush current for PT18LEDV0 onboard 100µF capacitor without collapsing 200mA PMIC rail

[[IQS231B Failsafe Heartbeat]]
	- 500µs failsafe heartbeat pulse is currently filtered out transparently by 30ms software debounce in touch_sensor.c
	- Revisit disabling via OTP Bank 2 (register 0x06) if permanent OTP burn is desired in the future

[[Adafruit DRV2605L + VL120628H LRA Haptics]]
	- Implement haptic feedback driver on shared i2c22 bus (powered via TPS22917 VOUT)

Add [[SYSTEM OFF and Wake Up]] by turning off FLPR and keeping IMU powered to feed GPIO interrupt based on picking up the scale

Check internal micro resistors on silicon, might be able to get more power savings by using external resistors instead?