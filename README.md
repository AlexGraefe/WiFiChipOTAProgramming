A repo demonstrating the WiFi capabilities of the IRIS board.

## Installation
Follow: https://docs.zephyrproject.org/latest/develop/getting_started/index.html

Add this line to ~/.bashrc:

```bash
export ZEPHYR_BASE=~/zephyrproject/zephyr
```

### VSCode
Follow: https://docs.zephyrproject.org/latest/develop/tools/vscode.html

For automatic source of the zephyr venv, do the following:
1. crtl + shift + P
2. Type Python: Select Interpreter
3. Enter ~/zephyrproject/.venv/bin/python

Add a folder secret to modules/udp_socket_demo with makros:

```C
#define BITCRAZE_SSID "my_ssid"
#define BITCRAZE_PASSWORD "my_password"
```

## Building

We are using sysbuild to build both MCUBoot and the application.
```bash
west build -p auto -b ubx_evk_iris_w1@fidelix --sysbuild .
```

Build the PC site:

```bash
cd PC_site
cmake -B build
cmake --build build
```

## Flashing
```bash
west flash
```

## Running this example
Flash the board. Then, change the version number in [VERSION](VERSION) and recompile. Do not flash the board. After resetting the board, run the following command:

```bash
./PC_site/build/tcp_socket_server
```

This script flashes the newly compiled firmware onto the board. Reset the board and check its serial output. You should see the firmware version changed.

## Explanation

### Memory Setup
The IRIS-W106 has no internal flash. 
e hence cannot boot from flash and write onto it at the same time. 
To overcome this limitaton, we have to load the entire firmware into the RAM and execute from RAM.
Through this, the Flash is free to use.

We use MCUBoot's RAM loading feature for this. MCUBoot needs three spaces in flash. One for the MCUBoot firmware and two versions of the application firmware.
You can find the definition of these in the following file: ~/zephyrproject/zephyr/boards/u-blox/ubx_evk_iris_w1/ubx_evk_iris_w1_rw612.dts.
More specifically they are 

```
boot_partition: partition@0 {
				label = "mcuboot";
				reg = <0x00000000 DT_SIZE_K(128)>;
			};

			slot0_partition: partition@20000 {
				label = "image-0";
				reg = <0x00020000 DT_SIZE_M(3)>;
			};

			slot1_partition: partition@320000 {
				label = "image-1";
				reg = <0x00320000 DT_SIZE_M(3)>;
			};
```

The base address of the flash is 0x18000000 (cf. datasheet of the RW612).
To enable RAM loading, we have to split the RAM into RAM used for the application and RAM for the MCUBoot. We do this in [boards/ubx_evk_iris_w1.overlay](boards/ubx_evk_iris_w1.overlay):

```
&sram {
	#address-cells = <1>;
	#size-cells = <1>;

	/* RW6XX SRAM can be access by either code or data bus, determined
	 * by the address used to access the memory.
	 * Applications can override the reg properties of either
	 * sram_data or sram_code nodes to change the balance of SRAM access partitioning.
	 */

	sram_appl: memory@0 {
		compatible = "mmio-sram";
		reg = <0x0000000 DT_SIZE_K(1024)>;
	};

	sram_boot: memory@0x100000 {
		compatible = "mmio-sram";
		reg = <0x100000 DT_SIZE_K(64)>;
	};
};


/ {
	aliases {
		spi = &flexcomm0;
	};

	chosen {
		zephyr,sram = &sram_appl;
		zephyr,flash = &sram_appl;
	};
};
```
There, we also assign zephyr, flash to the RAM to move all parts of the firmware to the flash. For MCUBoot, we do a similar change in [sysbuild/mcuboot.overlay](sysbuild/mcuboot.overlay):

```
/ {
	chosen {
        zephyr,sram=&sram_boot;
		/* With Sysbuild, enforce MCUboot code in boot_partition */
		zephyr,code-partition = &boot_partition;
	};
};
```

It also changes zephyr,code-partition to point to the flash's boot partition (~/zephyrproject/zephyr/boards/u-blox/ubx_evk_iris_w1/ubx_evk_iris_w1_rw612.dts already does the assignment of it to slot0_partition for the application).
In [sysbuild.conf](sysbuild.conf), we then enable MCUBoot and the RAM loading feature.
In [sysbuild/mcuboot.conft](sysbuild/mcuboot.conf), we do more MCUBoot related settings. The most important is the one 

```
CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_START=0x10000000
```

This address points to the beginning of sram_appl and hence we must tell MCUBoot to copy the application to.

As we set zephyr,flash to point to the RAM, we have to tell Zephyr where to flash the application (this is only relevant when using west flash).
This, in [boards/ubx_evk_iris_w1.conf](boards/ubx_evk_iris_w1.conf), we set

```
CONFIG_FLASH_BASE_ADDRESS=0x18020000
```

which is the starting address of slot0_partition.

### Programming over WiFi
MCUBoot works as follows. It reads the header of both images (directly after the starting address), checks if the first four bytes are equal to 0x96f3b83dU (this is the test value to check if it is a valid image).
Then, it reads the version numbers from beginning of header + 0x14.
The image with the largest version number is executed.

We use this behavior to savely flash a new image without writing over the current version. The code for this can be found in [modules/tcp_socket/tcp_socket.c](modules/tcp_socket/tcp_socket.c).
The code reads the two headers, checks from which slot the image was booted and writes the received firmware into the space of the other image.

**Important** As a result of this behavior, the firmware version has to change when updating the firmware. If the firmware version is older, MCUBoot will not boot to this, but to the current version. If both versions are the same, the behavior is undefined.
