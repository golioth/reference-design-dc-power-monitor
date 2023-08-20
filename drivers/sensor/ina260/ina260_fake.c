#include <zephyr/init.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/fff.h>
#include <net/golioth/rpc.h>
#include <app/sensor/ina260.h>
#include <zcbor_encode.h>

int ina260_reg_read_delegate(const struct device *dev, uint8_t reg_addr, uint16_t *reg_data)
{
	switch (reg_addr) {
	case INA260_REG_VOLTAGE:
		*reg_data = (int16_t)0x0EF7; /* 0x0EF7 * 0.00125 = 4.987 V */
		break;

	case INA260_REG_POWER:
		*reg_data = (uint16_t)0x007A; /* 0x007A * 0.01 = 1.22 W */
		break;

	case INA260_REG_CURRENT:
		*reg_data = (int16_t)0x00CC; /* 0x00CC * 0.00125 = 0.255 A */
		break;

	default:
		return -EIO;
	};

	return 0;
}

DEFINE_FFF_GLOBALS;
FAKE_VALUE_FUNC(int, ina260_reg_read, const struct device *, uint8_t, uint16_t *);

int fake_ina260_init(void)
{
	ina260_reg_read_fake.custom_fake = ina260_reg_read_delegate;
	return 0;
}
SYS_INIT(fake_ina260_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
