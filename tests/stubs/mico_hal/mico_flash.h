#pragma once
#include <stdint.h>
typedef int8_t mico_partition_t;
typedef struct { uint32_t partition_start_addr,partition_length; } mico_logic_partition_t;
enum { MICO_PARTITION_BOOTLOADER, MICO_PARTITION_APPLICATION, MICO_PARTITION_ATE,
       MICO_PARTITION_OTA_TEMP, MICO_PARTITION_RF_FIRMWARE, MICO_PARTITION_PARAMETER_1,
       MICO_PARTITION_PARAMETER_2, MICO_PARTITION_USER, MICO_PARTITION_SDS };
mico_logic_partition_t *MicoFlashGetInfo(mico_partition_t partition);
