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
	~~- Implement haptic feedback driver on shared i2c22 bus (powered via TPS22917 VOUT)~~
	- Revisit haptic waveform selection across gestures
	- Implement a depress and release feeling for touch interactions (distinct tactile feedback on initial press-down as well as release)
	- Figure out how to mitigate or optimize the large amount of power drawn by the vibration motor (peak current and battery consumption)

Add [[SYSTEM OFF and Wake Up]] by turning off FLPR and keeping IMU powered to feed GPIO interrupt based on picking up the scale

[[Revisit IQS231B Proximity Sensing]]
	- Proximity sensing on IO1 (D5) is currently disabled to eliminate false triggers from ambient noise and thermal drift
	- Revisit enabling proximity sensing if hover wake or approach detection is needed later

Check internal micro resistors on silicon, might be able to get more power savings by using external resistors instead?

[[IQS231B Touch Sensor Interrupt vs Active Debounce Polling]]
	- IQS231B open-drain active-low output on D6 (P1.08) has slow rising edge via internal pull-up which can fail to trigger GPIOTE edge interrupt on release
	- Currently using edge-triggered wake + active 30ms timer debounce polling while touched (with 3s stuck-line recovery guard)
	- Revisit whether an external hardware pull-up resistor, IQS231B OTP Bank 3 configuration, or Zephyr GPIOTE PORT sense configuration can achieve reliable pure interrupt-driven release without timer polling

[[Revisit Touch Sensor Gesture and Threshold Logic]]
	- Current touch logic implements zero-delay instant tare on release (100ms min tap duration), hold >= 1000ms to arm CAL mode with 2000ms tap confirmation, 3s stuck-line recovery guard with 500ms release lockout, threshold 0x28 (164 counts), and OTP Bank 2 0xB3 (12-in/8-out increased debounce)
	- Revisit later once final enclosure and electrode geometry are assembled to fine-tune tap thresholds, hold durations, and noise immunity margins

[[Wait for Stillness Tuning]]
	- Current stillness detection is way too forgiving: FLPR IMU stillness threshold is set to 500,000 variance with only 1 required read (`STILLNESS_REQUIRED_READS 1` in `app_flpr/src/main.c`), and cpuapp tares anyway after 4s timeout
	- Revisit and tighten the variance threshold and required consecutive reads so slight vibrations, hand tremors, or unstable surfaces do not falsely qualify as still before taring

[[Scale Tare Averaging Phase Optimization]]
	- Current tare routine takes 10 samples with 100ms sleep (1000ms total averaging) in scale_logic.c
	- Revisit tare averaging logic to determine if sample count or interval can be safely reduced while maintaining weight accuracy