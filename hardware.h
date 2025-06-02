#include "pico/stdlib.h"
#include "hardware/exception.h"
#include "hardware/timer.h"
#include "hardware/structs/systick.h"
#include "hardware/watchdog.h"
#include "ssio.h"

#define PIN_SCL	0
#define PIN_SDA	1

#define PIN_VPP 2

#define PIN_BUSY_INDICATOR PICO_DEFAULT_LED_PIN

void systick_int_handler(void);

bool TimerCallback(struct repeating_timer* timer) {
	return true;
};

static inline void TimerInit(void) {
	systick_hw->csr = 2;
	systick_hw->rvr = 0x4E;
	systick_hw->cvr = 0;
	exception_set_exclusive_handler(SYSTICK_EXCEPTION, systick_int_handler);
	struct repeating_timer timer;
	add_repeating_timer_ms(1, TimerCallback, NULL, &timer);
}

static inline void IOInit(void) {
	unsigned int offset = pio_add_program(pio0, &ssio_program);
	pio_sm_config c = ssio_program_get_default_config(offset);
	sm_config_set_out_pins(&c, PIN_SDA, 1);
	sm_config_set_in_pins(&c, PIN_SDA);
	sm_config_set_set_pins(&c, PIN_SDA, 1);
	sm_config_set_jmp_pin(&c, PIN_SDA);
	sm_config_set_sideset_pins(&c, PIN_SCL);
	sm_config_set_out_shift(&c, false, false, 24);
	sm_config_set_in_shift(&c, false, true, 16);
	sm_config_set_clkdiv(&c, 16.625f);
	pio_sm_set_pindirs_with_mask(pio0, 0, (1u << PIN_SCL) | (1u << PIN_SDA), (1u << PIN_SCL) | (1u << PIN_SDA));
	pio_sm_set_pins_with_mask(pio0, 0, 1u << PIN_SCL, (1u << PIN_SCL) | (1u << PIN_SDA));
	pio_gpio_init(pio0, PIN_SCL);
	pio_gpio_init(pio0, PIN_SDA);
	hw_set_bits(&pio0->input_sync_bypass, 1u << PIN_SDA);
	pio_sm_init(pio0, 0, offset + ssio_wrap_target, &c);
	pio_sm_set_enabled(pio0, 0, true);

	gpio_init(PIN_VPP);
	gpio_set_dir(PIN_VPP, GPIO_OUT);

	gpio_init(PIN_BUSY_INDICATOR);
	gpio_set_dir(PIN_BUSY_INDICATOR, GPIO_OUT);
}

static inline void delayTicks(unsigned int tick) {
	while (--tick);
}

static inline void TargetHardwareReset(void) {
	while (!pio_sm_is_tx_fifo_empty(pio0, 0));
	delayTicks(0x400);
	pio_sm_exec(pio0, 0, 0x30C7);
	busy_wait_ms(100);
	pio0->irq_force = 0x80;
	while (pio_sm_is_exec_stalled(pio0, 0));
	pio_sm_exec_wait_blocking(pio0, 0, 0xB842);
}

static inline void TargetRegisterWrite(unsigned char regIdx, unsigned short data) {
	pio_sm_put_blocking(pio0, 0, ((unsigned int)regIdx << 25) | (data << 8));
}

static inline unsigned short TargetRegisterRead(unsigned char regIdx) {
	pio_sm_put_blocking(pio0, 0, ((unsigned int)regIdx << 25) | 0x1000000u);
	return pio_sm_get_blocking(pio0, 0);
}

static inline void EnableVPP(void) {
	gpio_put(PIN_VPP, 1);
}

static inline void DisableVPP(void) {
	gpio_put(PIN_VPP, 0);
}

static inline void TurnOnBusyIndicator(void) {
	gpio_put(PIN_BUSY_INDICATOR, 1);
}

static inline void TurnOffBusyIndicator(void) {
	gpio_put(PIN_BUSY_INDICATOR, 0);
}

static inline void reboot(void) {
	watchdog_enable(100, 1);
	watchdog_reboot(0, 0, 0);
	while (true);
}
