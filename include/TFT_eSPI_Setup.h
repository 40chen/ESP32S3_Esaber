#pragma once

// ============================================================================
// TFT_eSPI 配置 —— 经 build_flags 的 -include 强制注入（绕过库自带的 User_Setup）
// 目标屏：GC9A01 圆形 240×240，FSPI 端口
// ============================================================================

#define USER_SETUP_LOADED        // 告诉库：配置已由外部提供，不要用默认值

#define GC9A01_DRIVER            // 圆屏驱动型号
#define TFT_WIDTH 240
#define TFT_HEIGHT 240
#define USE_FSPI_PORT            // 用 FSPI（可指定引脚的那组）

// ---- 引脚（TFT_RST = -1：没接复位线，故 DisplayDriver 有 repairPanel 二次修复）----
#define TFT_MOSI 18
#define TFT_SCLK 17
#define TFT_CS 16
#define TFT_DC 15
#define TFT_RST -1

// ---- 字库：GLCD 默认 + 两级常用字号（开机页/二维码标题用）----
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

// ---- SPI 时钟：40MHz 写入足够稳，读回 16MHz（读屏用得少）----
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
