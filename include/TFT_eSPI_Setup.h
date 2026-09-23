#pragma once

#define USER_SETUP_LOADED

#define GC9A01_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 240
#define USE_FSPI_PORT

#define TFT_MOSI 18
#define TFT_SCLK 17
#define TFT_CS 16
#define TFT_DC 15
#define TFT_RST -1

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4

#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
