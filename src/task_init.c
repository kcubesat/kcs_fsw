/*
 * task_init.c
 *
 *  Created on: 13/07/2010
 *      Author: Johan
 */

#include <stdio.h>
#include <string.h>

#include <conf_nanomind.h>
#include <conf_io.h>
#ifdef WITH_ADCS
#include <conf_adcs.h>
#endif
#ifdef WITH_CDH
#include <conf_cdh.h>
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <dev/cpu.h>
#include <dev/spi.h>
#include <dev/arm/cpu_pm.h>
#include <util/crc32.h>
#include <io/nanomind.h>
#include <ftp/ftp_server.h>

#include <util/clock.h>

#if ENABLE_LOG_SERVER
#include <log/log.h>
#include <log/logd.h>
#include <log/log_event.h>
#include <log/handler_console.h>
#include <log/handler_ram.h>
#include <log/handler_rrstore.h>
#endif

#if ENABLE_FP_SERVER
#include <fp/fp.h>
#include <fp/fp_types.h>
#endif
#if ENABLE_PARAM_SERVER
#include <param/param_local.h>
#endif
#if ENABLE_RSH_SERVER
#include <rsh/rsh_server.h>
#endif
#if ENABLE_RSH_CLIENT
#include <rsh/rsh_client.h>
#endif

/* Global storage of spi device */
spi_dev_t spi_dev;
spi_dev_t dpi_dev7;

#ifdef WITH_STORAGE
#include <unistd.h>
#include <fcntl.h>

#include <fat_sd/ff.h>
#include <fat_sd/fat_spi_dev.h>
#include <fat_sd/cmd_fat.h>

#include <uffs/uffs_config.h>
#include <uffs/uffs_public.h>
#include <uffs/uffs_fs.h>
#include <uffs/uffs_utils.h>
#include <uffs/uffs_core.h>
#include <uffs/uffs_mtb.h>
#include <uffs/cmd_uffs.h>

#include <lzo/lzoutils.h>

#include <vfs/vfs.h>
#include <vfs/vfs_uffs.h>
#include <vfs/vfs_fat.h>

static struct vfs_partition part_table[] = {
#ifdef ENABLE_DF
	{
		.name = "data",
		.fops = &vfs_uffs_ops,
	},
#endif
#ifdef ENABLE_FLASH_FS
	{
		.name = "boot",
		.fops = &vfs_uffs_ops,
	},
#endif
#ifdef ENABLE_SD
	{
		.name = "sd",
		.true_name = "0:",
		.drive = 0,
		.fops = &vfs_fat_ops,
	},
#endif
};

#if defined(OBC_ROM) && (ENABLE_DF || ENABLE_FLASH_FS)
static void try_boot(void) {

	/* Pointer to start of memory */
	void * dst = (void *) 0x50000000;
	/* _Maximum_ size of image is 1MB */
	int	size = 1048576;

	/* Check for a boot config */
	FILE * fp;
	fp = fopen("/boot/boot.conf", "r");
	if (!fp)
		goto out;

	printf("Found /boot/boot.conf\r\n");

	char image[100], cs[20];

	/* Read image path */
	fgets(image, 100, fp);
	image[strlen(image)-1] = '\0';
	printf("Image %s\r\n", image);

	/* Read checksum */
	fgets(cs, 20, fp);
	cs[strlen(cs)-1] = '\0';

	unsigned int checksum;
	sscanf(cs, "%x", &checksum);
	printf("Checksum %#x\r\n", checksum);

	fclose(fp);

	/* Delete boot config to prevent boot-loop */
	printf("Removing /boot/boot.conf\r\n");
	remove("/boot/boot.conf");

	if (lzo_is_lzop_image(image)) {

		printf("Image is LZO compressed - decompressing\r\n");
		/* Decompress image into memory */
		if((size = lzo_decompress_image(image, dst, size)) < 0) {
			printf("Failed to decompress image\r\n");
			goto out;
		}

	} else {

		/* Open image */
		fp = fopen(image, "r");
		if (!fp) {
			printf("Failed to open file %s\r\n", image);
			goto out;
		}

		/* Read image size */
		struct stat st;
		if (stat(image, &st) != 0) {
			printf("Failed to stat image file\r\n");
			fclose(fp);
			goto out;
		}
		size = st.st_size;

		/* Copy image */
		printf("Copying %u bytes to %p\r\n", size, dst);

		int r;
		if ((r = fread(dst, 1, size, fp)) != size) {
			printf("Failed to copy %u bytes\r\n", size);
			fclose(fp);
			goto out;
		}

	}

	/* Checking checksum */
	unsigned int checksum_ram = chksum_crc32((unsigned char *) dst, size);
	printf("Checksum RAM: 0x%x, boot.conf: 0x%x\r\n", checksum_ram, checksum);
	if (checksum_ram != checksum)
		goto out;

	/* Jump to address */
	printf("Jumping to addr %p\r\n", dst);

	void (*jump) (void) = (void *) dst;
	jump();

out:
	printf("Running default from ROM\r\n");

}
#endif // defined(_OBC_ROM_)
#endif // WITH_STORAGE

#if ENABLE_FLASH_FS

/* UFFS includes */
#include <uffs/uffs_utils.h>
#include <uffs/uffs_mtb.h>
#include <uffs/uffs_fd.h>
#include <uffs/uffs_config.h>

/** Static memory buffer for UFFS */
#ifdef ENABLE_FLASH_FS
#include <uffs/at49bv320dt.h>
int flash_static_buffer[UFFS_STATIC_BUFF_SIZE(FLASH_PAGES_PER_BLOCK, FLASH_PAGE_SIZE, FLASH_PAR_BLOCKS) / sizeof(int)];
extern struct uffs_StorageAttrSt flash_storage;
uffs_Device flash_device = {.Init = flash_init_device, .Release = flash_release_device, .attr = &flash_storage};
uffs_MountTable flash_table = {.dev = &flash_device, .start_block = FLASH_FS_FIRST_BLOCK, .end_block = FLASH_FS_LAST_BLOCK, .mount = "/boot/"};
#endif
#if ENABLE_DF
#include <uffs/at45db642d.h>
int df_static_buffer[UFFS_STATIC_BUFF_SIZE(DF_PAGES_PER_BLOCK, DF_PAGE_SIZE, DF_PAR_BLOCKS) / sizeof(int)];
extern struct uffs_StorageAttrSt df_storage;
uffs_Device df_device = {.Init = df_init_device, .Release = df_release_device, .attr = &df_storage};
uffs_MountTable df_table = {.dev = &df_device, .start_block = DF_FS_FIRST_BLOCK, .end_block = DF_FS_LAST_BLOCK, .mount = "/data/"};
#endif

#endif

void vTaskInit(void * pvParameters) {


	/* Setup the SPI0 hardware */
	spi_dev.variable_ps = 0;			// Set CS once, not for each read/write operation
	spi_dev.pcs_decode = 1;				// Use chip select mux
	spi_dev.index = 0;					// Use SPI0
	spi_init_dev(&spi_dev);

	/* Setup the SPI7 hardware (accel_sensor) */
	spi_dev7.vaiable_ps = 7;	// Set CS once,
	spi_dev7.pcs_decode = 1;	// Use chip select mux //????
	spi_dev7.index = 7;		// Use SPI7
	spi_init_dev(&spi_dev7);

#ifdef WITH_STORAGE
	/* Initialize VFS */
	vfs_init(part_table, sizeof(part_table)/sizeof(part_table[0]));

	int result;
#ifdef ENABLE_SD

	static spi_chip_t spi_dummy_chip;
	spi_dummy_chip.cs = 0;
	spi_dummy_chip.bits = 8;
	spi_dummy_chip.spi_dev = &spi_dev;
	int i;
	for (i = 0; i < 10; i++) {
		spi_write(&spi_dummy_chip, 0xFF);
		spi_read(&spi_dummy_chip);
	}

	/* Setup the SD card SPI chip, must be started at low speed */
	static FATFS fs0;
	static spi_chip_t spi_sd_chip;
	spi_sd_chip.spi_dev = &spi_dev;		// A pointer to the physical device SPI0
	spi_sd_chip.baudrate = 10000000;	// This is only the initial baud rate, it will be increased by the driver
	spi_sd_chip.spi_mode = 0;			// SPI mode
	spi_sd_chip.bits = 8;				// Default value for transferring bytes
	spi_sd_chip.cs = CONFIG_SD_CS;		// The SD card is on chip-select 0
	spi_sd_chip.reg = CONFIG_SD_CS / 4;	// The SD card is on cs register 0
	spi_sd_chip.spck_delay = 0;			// No delays
	spi_sd_chip.trans_delay = 0;		// No delays
	spi_setup_chip(&spi_sd_chip);

	vTaskDelay(100);
	result = sd_spi_init(&spi_sd_chip);

	if (result == 0) {
		result = f_mount(0, &fs0);
		printf("SD card detected, mount result %d\r\n", result);
	} else {
		printf("SD card not found\r\n");
	}

#endif // SD

#ifdef ENABLE_DF

	/* Setup the DF card SPI chip */
	static spi_chip_t spi_df_chip;
	spi_df_chip.spi_dev = &spi_dev;		// A pointer to the physical device SPI0
	spi_df_chip.baudrate = 10000000;	// This is only the initial baud rate, it will be increased by the driver
	spi_df_chip.spi_mode = 3;			// SPI mode
	spi_df_chip.bits = 8;				// Default value for transferring bytes
	spi_df_chip.cs = 0;					// The DF chip is on chip-select 0
	spi_df_chip.reg = 0;				// The DF chip is on cs register 0
	spi_df_chip.stay_act = 0;			// (This function is not supported on ARM)
	spi_df_chip.spck_delay = 0;			// No delays
	spi_df_chip.trans_delay = 0;		// No delays
	spi_setup_chip(&spi_df_chip);

	result = df_spi_uffs_init(&spi_df_chip);

	if (result != 0) {
		printf("Failed to initialise DataFlash\r\n");
	} else {
		uffs_MemSetupStaticAllocator(&df_device.mem, df_static_buffer, sizeof(df_static_buffer));
		uffs_RegisterMountTable(&df_table);
	}

#endif // DF

#ifdef ENABLE_FLASH_FS

#if 0
	#include <dev/arm/flash.h>
	portENTER_CRITICAL();
	flash_init();
	flash_erase_chip(0x48000000);
	portEXIT_CRITICAL();
#endif

	uffs_MemSetupStaticAllocator(&flash_device.mem, flash_static_buffer, sizeof(flash_static_buffer));
	uffs_RegisterMountTable(&flash_table);

#endif

#if ENABLE_DF || ENABLE_FLASH_FS

	result = uffs_InitMountTable();
	if (result != U_SUCC)
		printf("Failed to mount file system (ret=%d)\r\n", result);

#if defined(OBC_ROM)
	try_boot();
#endif

#endif // ENABLE_DF || ENABLE_FLASH_FS

#if ENABLE_LOG_SERVER
	/* Logging daemon */
	logd_add_handler(HANDLER_ID_CONSOLE, entry_console, NULL, NULL, LOG_DEBUG);
	logd_add_handler(HANDLER_ID_RAM, entry_ram, query_ram, count_ram, LOG_DEBUG);
#if defined(WITH_STORAGE) && defined(ENABLE_RRSTORE)
	if (log_rrstore_init("/boot/log.rrs") == 0)
		logd_add_handler(HANDLER_ID_RRSTORE, entry_rrstore, query_rrstore , NULL, LOG_DEBUG);
#endif
	logd_start(2048, OBC_PORT_LOGD, LOG_DEBUG, clock_get_time, "/boot/logcounter");
	log_add_collector(NODE_OBC, OBC_PORT_LOGD, LOG_DEBUG);

	/* Log bootcause */
	cpu_reset_cause_t cause = cpu_read_reset_cause();
	switch (cause) {
	case CPU_RESET_DATA_ABORT:
	case CPU_RESET_PREFETCH_ABORT:
	case CPU_RESET_SOFT_RESET:
	case CPU_RESET_STACK_OVERFLOW:
	case CPU_RESET_UNDEFINED_INSTRUCTION:
		log_error("OBC_RESET_CRASH", NULL);
		break;
	case CPU_RESET_IRQ_SPAM:
		log_error("OBC_RESET_IRQ_SPAM", NULL);
		break;
	case CPU_RESET_SUPERVISOR:
		log_error("OBC_RESET_SUPERVISOR", NULL);
		break;
	case CPU_RESET_USER:
		log_info("OBC_RESET_USER", NULL);
		break;
	default:
	case CPU_RESET_NONE:
		log_info("OBC_RESET_NONE", NULL);
		break;
	}
#endif // ENABLE_LOG_SERVER

#if ENABLE_LOG_CLIENT
	void cmd_log_setup(void);
	cmd_log_setup();
#endif

#ifdef ENABLE_SD
	ftp_register_backend(BACKEND_FAT, &backend_fat);
#endif
#if defined(ENABLE_DF) || defined(ENABLE_FLASH_FS)
	ftp_register_backend(BACKEND_UFFS, &backend_uffs);
#endif

#ifdef ENABLE_RSH_SERVER
	static rsh_server_params_t rsh_param;
	rsh_param.server_port = OBC_PORT_RSH;
	rsh_param.worker_read_timeout = 1000;
	rsh_param.worker_stack_size = 1024*4;
	rsh_param.worker_task_priority = 1;
	xTaskCreate(rsh_server, (const signed char*) "RSH", 1024*4, &rsh_param, 1, NULL);
#endif

#if ENABLE_RSH_CLIENT
	rsh_client_setup();
#endif

#if ENABLE_FLASH_FS
	cmd_uffs_setup();
#endif

#if ENABLE_SD
	cmd_fat_setup();
#endif

#if defined(ENABLE_FLASH_FS) || defined(ENABLE_SD)
	void cmd_fs_setup();
	cmd_fs_setup();
#endif
#endif // WITH_STORAGE

	/* Register FTP backends */
	ftp_register_backend(BACKEND_RAM, &backend_ram);

#if ENABLE_PARAM_SERVER
	/* Local store */
	extern const param_type_table_t nanomind_types[];
	param_local_init(nanomind_types);
	/* Server */
	void param_server(void * pvParameters);
	xTaskCreate(param_server, (const signed char *) "Param", 1000, NULL, 1, NULL);
#if ENABLE_PARAM_CLIENT
	/* CMD */
	void cmd_param_local_setup(void);
	cmd_param_local_setup();
	void cmd_param_query_setup(void);
	cmd_param_query_setup();
#endif
#ifdef ENABLE_FLASH_FS
	param_store_load("/boot/startup.param");
#endif
#endif

#if ENABLE_ADCS
	extern void task_adcs(void *parameters);
	extern void task_adcs_mag(void *parameters);
	extern void task_adcs_server(void *parameters);
	xTaskCreate(task_adcs, (const signed char*) "ADCS thread", 1024*8, NULL, 2, NULL);
	xTaskCreate(task_adcs_server, (const signed char*) "ADCS srv thread", 1024*4, NULL, 2, NULL);
	xTaskCreate(task_adcs_mag, (const signed char*) "ADCS mag thread", 1000, NULL, 3, NULL);
#if ENABLE_ADCS_CLIENT
	void cmd_adcs_setup(void);
	cmd_adcs_setup();
#endif
#endif

#ifdef ENABLE_NANOPOWER_CLIENT
	void cmd_eps_setup(void);
	cmd_eps_setup();
#endif

#ifdef ENABLE_NANOPOWER2_CLIENT
	void cmd_eps_setup(void);
	cmd_eps_setup();
#endif

#ifdef ENABLE_NANOHUB_CLIENT
	void cmd_hub_setup(void);
	cmd_hub_setup();
#endif

#ifdef ENABLE_NANOCOM_CLIENT
	void cmd_com_setup(int node_arg);
	cmd_com_setup(5);
#endif

#ifdef ENABLE_NANOMIND_CLIENT
	void cmd_obc_setup(void);
	cmd_obc_setup();
#endif

#ifdef ENABLE_NANOCAM_CLIENT
	void cmd_cam_setup(void);
	cmd_cam_setup();
#endif

#if ENABLE_RRSTORE
	void cmd_rrstore_setup(void);
	cmd_rrstore_setup();
#endif

#if ENABLE_LZO
	void cmd_lzo_setup(void);
	cmd_lzo_setup();
#endif

#if ENABLE_SUPERVISOR
	void cmd_sv_setup(void);
	cmd_sv_setup();
#endif

#ifdef ENABLE_RTC
	void cmd_rtc_setup(void);
	cmd_rtc_setup();
#endif


	/** Board configuration */

	void cmd_periph_setup(void);
	cmd_periph_setup();

	void cmd_panels_setup(void);
	cmd_panels_setup();

#if ENABLE_TASK_CONNLESS
	void task_server_connless(void * param);
	void cmd_task_server_connless_setup(void);
	xTaskCreate(task_server_connless, (const signed char*) "CONNLESS", 1024*4, NULL, 1, NULL);
	cmd_task_server_connless_setup();
#endif

#if ENABLE_TASK_HK
	extern void task_hk_collector(void * param);
	xTaskCreate(task_hk_collector, (const signed char*) "HKcol", 1024*4, NULL, 1, NULL);
#endif

#if ENABLE_CPP
	void test_cpp_call(void);
	test_cpp_call();
#endif

#if ENABLE_MPIO
	void cmd_mpio_setup(void);
	cmd_mpio_setup();
#endif

	/** End of init */
	vTaskDelete(NULL);

}
