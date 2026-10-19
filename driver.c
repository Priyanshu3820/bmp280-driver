#include <linux/module.h>
#include <linux/printk.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/cdev.h>
#include <linux/i2c-dev.h>
#include <linux/kernel.h>
#include <linux/kconfig.h>
#include <linux/types.h>
#include <linux/math64.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PRIYANSHU");
MODULE_DESCRIPTION("bmp280 sensor driver");

#define DRIVER_NAME "bmp280"
#define DRIVER_CLASS "bmp280Class"

static struct i2c_adapter *bmp_i2c_adapter = NULL;
static struct i2c_client *bmp280_i2c_client = NULL;

#define I2C_BUS_AVAILABLE 1
#define SLAVE_DEVICE_NAME "BMP280"
#define BMP280_SLAVE_ADDRESS 0x76 // bmp280 i2c address

static const struct i2c_device_id bmp_id[] = {
	{ SLAVE_DEVICE_NAME, 0 },
	{ }
};

static struct i2c_driver bmp_driver = {
	.driver = {
		.name = SLAVE_DEVICE_NAME,
		.owner = THIS_MODULE,
	},
	.id_table = bmp_id,
};

static struct i2c_board_info bmp_i2c_board_info = {
	I2C_BOARD_INFO(SLAVE_DEVICE_NAME, BMP280_SLAVE_ADDRESS)
};

static dev_t myDeviceNr;
static struct class *myClass;
static struct cdev myDevice;

/*
 * Calibration coefficients.
 * Per the Bosch BMP280 datasheet:
 *   - dig_T1 and dig_P1 are unsigned 16-bit
 *   - dig_T2..T3 and dig_P2..P9 are signed 16-bit
 */
static u16 dig_T1;
static s16 dig_T2, dig_T3;

static u16 dig_P1;
static s16 dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

/*
 * t_fine: fine-resolution temperature, computed by read_temperature() and
 * consumed by read_pressure(), exactly as required by the Bosch datasheet.
 */
static s32 t_fine;

/* Function prototypes */
s32 read_temperature(void);
s32 read_pressure(void);
s32 calculate_altitude(s32 pressure, s32 temperature);

/**
 * @brief Read current temperature from BMP280 sensor
 *
 * @return temperature in degree
 */
s32 read_temperature(void) {
	int var1, var2;
	s32 raw_temp;
	s32 d1, d2, d3;

	if (bmp280_i2c_client == NULL)
		return 0;

	/* Read Temperature */
	d1 = i2c_smbus_read_byte_data(bmp280_i2c_client, 0xFA);
	d2 = i2c_smbus_read_byte_data(bmp280_i2c_client, 0xFB);
	d3 = i2c_smbus_read_byte_data(bmp280_i2c_client, 0xFC);
	raw_temp = ((d1 << 16) | (d2 << 8) | d3) >> 4;

	/* Official Bosch 32-bit formula (datasheet section 3.11.3) */
	var1 = ((((raw_temp >> 3) - ((s32)dig_T1 << 1))) * ((s32)dig_T2)) >> 11;
	var2 = (((((raw_temp >> 4) - ((s32)dig_T1)) * ((raw_temp >> 4) - ((s32)dig_T1))) >> 12) * ((s32)dig_T3)) >> 14;

	t_fine = var1 + var2;

	/* Output in 0.01 deg C */
	return (t_fine * 5 + 128) >> 8;
}

/**
 * @brief Read current pressure from BMP280 sensor
 *
 * @return pressure in Pascals
 */
s32 read_pressure(void) {
	s64 var1, var2, p;
	s32 raw_pressure;
	s32 d1, d2, d3;

	if (bmp280_i2c_client == NULL)
		return 0;

	/* Read Pressure */
	d1 = i2c_smbus_read_byte_data(bmp280_i2c_client, 0xF7);
	d2 = i2c_smbus_read_byte_data(bmp280_i2c_client, 0xF8);
	d3 = i2c_smbus_read_byte_data(bmp280_i2c_client, 0xF9);
	raw_pressure = ((d1 << 16) | (d2 << 8) | d3) >> 4;

	/* Official Bosch 64-bit integer formula (datasheet section 3.11.3).
	 * Requires t_fine, which is produced by read_temperature(). */
	var1 = ((s64)t_fine) - 128000;
	var2 = var1 * var1 * (s64)dig_P6;
	var2 = var2 + ((var1 * (s64)dig_P5) << 17);
	var2 = var2 + (((s64)dig_P4) << 35);
	var1 = ((var1 * var1 * (s64)dig_P3) >> 8) + ((var1 * (s64)dig_P2) << 12);
	var1 = (((((s64)1) << 47) + var1)) * ((s64)dig_P1) >> 33;

	if (var1 == 0)
		return 0; /* Avoid division by zero */

	p = 1048576 - raw_pressure;
	p = (((p << 31) - var2) * 3125);
	p = div64_s64(p, var1);
	var1 = (((s64)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
	var2 = (((s64)dig_P8) * p) >> 19;
	p = ((p + var1 + var2) >> 8) + (((s64)dig_P7) << 4);

	/* Output in Pascals (e.g. ~101325 Pa) */
	return (s32)(p >> 8);
}

/**
 * @brief Calculate altitude from pressure
 *
 * Uses the standard ISA approximation:
 *     h = 44330 * (1 - (P / P0) ^ (1/5.255))
 * linearised for small (1 - P/P0), giving h ~= 8436 * (1 - P/P0).
 *
 * @param pressure in Pascals
 * @param temperature in 0.01 degC (not used by this approximation)
 * @return altitude in meters
 */
s32 calculate_altitude(s32 pressure, s32 temperature) {
	const s32 P0 = 101325;   /* sea level pressure in Pa */
	s64 x, altitude;

	(void)temperature;       /* reserved for a temperature-corrected formula */

	if (pressure <= 0)
		return 0;

	/* x = (1 - P/P0) scaled by 1e6.
	 * 64-bit division must go through div64_s64(); a plain '/' would emit
	 * __aeabi_ldivmod, which the kernel does not provide on ARM. */
	x = div64_s64((s64)(P0 - pressure) * 1000000, (s64)P0);

	/* altitude(m) ~= 8436 * x  (44330 / 5.255 ~= 8436) */
	altitude = div64_s64((s64)8436 * x, 1000000);

	if (altitude < 0)
		altitude = 0;

	return (s32)altitude;
}

/**
 * @brief Get data out of buffer
 */
static ssize_t driver_read(struct file *File, char *user_buffer, size_t count, loff_t *offs) {
	int to_copy, not_copied, delta;
	char out_string[100];
	s32 temperature_raw;
	s32 pressure_raw;
	s32 altitude_raw;
	int len;

	/* Signal EOF after the first read, otherwise tools like `cat` would
	 * loop forever re-reading the sensor. */
	if (*offs > 0)
		return 0;

	/* Get temperature and pressure (t_fine is populated by read_temperature) */
	temperature_raw = read_temperature();
	pressure_raw = read_pressure();

	/* Calculate altitude */
	altitude_raw = calculate_altitude(pressure_raw, temperature_raw);

	/* Format output (%02d keeps 28.05 C from printing as 28.5 C) */
	len = snprintf(out_string, sizeof(out_string),
	               "Temperature: %d.%02d C\nPressure: %d Pa\nAltitude: %d meters\n",
	               temperature_raw / 100, abs(temperature_raw % 100),
	               pressure_raw,
	               altitude_raw);

	if (len < 0)
		return -EIO;

	/* Copy no more than the caller asked for */
	to_copy = min((size_t)len, count);

	/* Copy Data to user */
	not_copied = copy_to_user(user_buffer, out_string, to_copy);

	/* Calculate delta */
	delta = to_copy - not_copied;

	/* Advance the file offset */
	*offs += delta;

	return delta;
}

/**
 * @brief This function is called, when the device file is opened
 */
static int driver_open(struct inode *deviceFile, struct file *instance) {
	printk("MyDeviceDriver - Open was called\n");
	return 0;
}

/**
 * @brief This function is called, when the device file is close
 */
static int driver_close(struct inode *deviceFile, struct file *instance) {
	printk("MyDeviceDriver - Close was called\n");
	return 0;
}

/**
 * @brief Handle lseek() on the device file
 *
 * Without a .llseek handler a character device rejects lseek() with -ESPIPE,
 * so userspace cannot rewind the file offset to trigger another read.
 */
static loff_t driver_llseek(struct file *File, loff_t offset, int whence) {
	loff_t newpos;

	switch (whence) {
	case SEEK_SET:
		newpos = offset;
		break;
	case SEEK_CUR:
		newpos = File->f_pos + offset;
		break;
	case SEEK_END:
		newpos = 0;   /* stream device: end == start */
		break;
	default:
		return -EINVAL;
	}

	if (newpos < 0)
		return -EINVAL;

	File->f_pos = newpos;
	return newpos;
}

/* Map the file operations */
static struct file_operations fops = {
	.owner = THIS_MODULE,
	.open = driver_open,
	.release = driver_close,
	.read = driver_read,
	.llseek = driver_llseek,
};

/**
 * @brief function, which is called after loading module to kernel, do initialization there
 */
static int __init ModuleInit(void) {
	int ret;
	int val;
	u8 id;
	printk("MyDeviceDriver - Hello Kernel\n");

	/* Allocate Device Nr */
	ret = alloc_chrdev_region(&myDeviceNr, 0, 1, DRIVER_NAME);
	if (ret < 0) {
		printk("Device Nr. could not be allocated!\n");
		return ret;
	}
	printk("MyDeviceDriver - Device Nr %d was registered\n", myDeviceNr);

	/* Create Device Class */
	myClass = class_create(DRIVER_CLASS);
	if (IS_ERR(myClass)) {
		printk("Device Class can not be created!\n");
		ret = PTR_ERR(myClass);
		myClass = NULL;
		goto ClassError;
	}

	/* Create Device file */
	if (IS_ERR(device_create(myClass, NULL, myDeviceNr, NULL, DRIVER_NAME))) {
		printk("Can not create device file!\n");
		ret = -ENODEV;
		goto FileError;
	}

	/* Initialize Device file */
	cdev_init(&myDevice, &fops);

	/* register device to kernel */
	if (cdev_add(&myDevice, myDeviceNr, 1) < 0) {
		printk("Registering of device to kernel failed!\n");
		ret = -ENODEV;
		goto AddError;
	}

	/* Get I2C adapter. On a Pi Zero the I2C bus must be enabled
	 * (dtparam=i2c_arm=on); otherwise this returns NULL. */
	bmp_i2c_adapter = i2c_get_adapter(I2C_BUS_AVAILABLE);
	if (bmp_i2c_adapter == NULL) {
		printk("Failed to get I2C adapter %d (is I2C enabled?)\n", I2C_BUS_AVAILABLE);
		ret = -ENODEV;
		goto AddError;
	}

	/* Create I2C client device */
	bmp280_i2c_client = i2c_new_client_device(bmp_i2c_adapter, &bmp_i2c_board_info);
	if (IS_ERR(bmp280_i2c_client)) {
		printk("Failed to create I2C client device\n");
		ret = PTR_ERR(bmp280_i2c_client);
		bmp280_i2c_client = NULL;
		goto AdapterError;
	}

	/* Register the I2C driver */
	if (i2c_add_driver(&bmp_driver)) {
		printk("Can't add I2C driver...\n");
		ret = -ENODEV;
		goto ClientError;
	}
	printk("BMP280 Driver added!\n");

	/* Read Chip ID */
	id = i2c_smbus_read_byte_data(bmp280_i2c_client, 0xD0);
	if (id != 0x58) {
		printk("Invalid BMP280 Chip ID: 0x%x (expected 0x58)\n", id);
		ret = -ENODEV;
		goto DriverError;
	}
	printk("BMP280 Chip ID: 0x%x\n", id);

	/* Read Calibration Values with error checking.
	 * i2c_smbus_read_word_data() returns 0..0xFFFF on success or a negative
	 * errno, so check the raw value before casting into the signed/unsigned
	 * coefficient types. This preserves the sign of negative coefficients
	 * (common for dig_P2/P4/P8 etc.). */
	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x88);
	if (val < 0) { printk("Failed to read dig_T1 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_T1 = (u16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x8A);
	if (val < 0) { printk("Failed to read dig_T2 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_T2 = (s16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x8C);
	if (val < 0) { printk("Failed to read dig_T3 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_T3 = (s16)val;

	/* Read Pressure Calibration Values */
	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x8E);
	if (val < 0) { printk("Failed to read dig_P1 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P1 = (u16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x90);
	if (val < 0) { printk("Failed to read dig_P2 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P2 = (s16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x92);
	if (val < 0) { printk("Failed to read dig_P3 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P3 = (s16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x94);
	if (val < 0) { printk("Failed to read dig_P4 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P4 = (s16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x96);
	if (val < 0) { printk("Failed to read dig_P5 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P5 = (s16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x98);
	if (val < 0) { printk("Failed to read dig_P6 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P6 = (s16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x9A);
	if (val < 0) { printk("Failed to read dig_P7 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P7 = (s16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x9C);
	if (val < 0) { printk("Failed to read dig_P8 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P8 = (s16)val;

	val = i2c_smbus_read_word_data(bmp280_i2c_client, 0x9E);
	if (val < 0) { printk("Failed to read dig_P9 calibration data\n"); ret = -EIO; goto DriverError; }
	dig_P9 = (s16)val;

	/* Complete BMP280 initialization sequence */
#define BMP280_CTRL_MEAS_REG 0xF4
#define BMP280_CONFIG_REG    0xF5
#define BMP280_POWER_MODE_NORMAL 0b11

	/* Set configuration: standby 1000ms, IIR filter 16 samples */
	if (i2c_smbus_write_byte_data(bmp280_i2c_client, BMP280_CONFIG_REG, 0b00100000) < 0) {
		printk("Failed to set BMP280 config register\n");
		ret = -EIO;
		goto DriverError;
	}

	/* Set measurement control:
	   - Temperature oversampling x16 (0b101)
	   - Pressure oversampling x16 (0b101)
	   - Power mode normal (0b11)
	*/
	if (i2c_smbus_write_byte_data(bmp280_i2c_client, BMP280_CTRL_MEAS_REG,
	                              (5 << 5) | (5 << 2) | (BMP280_POWER_MODE_NORMAL)) < 0) {
		printk("Failed to set BMP280 measurement control\n");
		ret = -EIO;
		goto DriverError;
	}

	printk("BMP280 initialization complete\n");
	return 0;

	/* Unwind in reverse order of setup */
DriverError:
	i2c_del_driver(&bmp_driver);
ClientError:
	i2c_unregister_device(bmp280_i2c_client);
	bmp280_i2c_client = NULL;
AdapterError:
	i2c_put_adapter(bmp_i2c_adapter);
	bmp_i2c_adapter = NULL;
AddError:
	cdev_del(&myDevice);
FileError:
	device_destroy(myClass, myDeviceNr);
ClassError:
	class_destroy(myClass);
	unregister_chrdev_region(myDeviceNr, 1);
	return ret;
}

/**
 * @brief function, which is called when removing module from kernel
 * free alocated resources
 */
static void __exit ModuleExit(void) {
	printk("MyDeviceDriver - Goodbye, Kernel!\n");
	if (bmp280_i2c_client)
		i2c_unregister_device(bmp280_i2c_client);
	i2c_del_driver(&bmp_driver);
	if (bmp_i2c_adapter)
		i2c_put_adapter(bmp_i2c_adapter);
	cdev_del(&myDevice);
	device_destroy(myClass, myDeviceNr);
	class_destroy(myClass);
	unregister_chrdev_region(myDeviceNr, 1);
}

module_init(ModuleInit);
module_exit(ModuleExit);
