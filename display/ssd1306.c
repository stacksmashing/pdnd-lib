#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "ssd1306.h"
#include "splash.h"

#define WIDTH 128
#define HEIGHT 64

// set this flag when using the SH1106 display
// #define OLED_SH1106

#ifdef OLED_SH1106
#define COLUMN_OFFSET 0x02
#else
#define COLUMN_OFFSET 0x00
#endif

const uint8_t i2caddr = 0x3c;
static uint8_t contrast = 0x0;

uint8_t *ssd1306_buffer = NULL;

static int ssd1306_get_rotation() {
  return 4;
}

#define ssd1306_swap(a, b)                                                     \
  (((a) ^= (b)), ((b) ^= (a)), ((a) ^= (b))) ///< No-temp-var swap operation

void ssd1306_clear_display(ssd1306_context *ctx) {
  memset(ctx->buffer, 0, WIDTH * ((HEIGHT + 7) / 8));
}
void ssd1306_fill_display(ssd1306_context *ctx) {
  memset(ctx->buffer, 0xff, WIDTH * ((HEIGHT + 7) / 8));
}

static void ssd1306_command_list(ssd1306_context *ctx, const uint8_t *c, uint8_t n) {
  // Code for hackers by hackers
  // It was late and I really just wanted to test the display :(
  char *b = malloc(n+1);
  b[0] = 0x0;
  memcpy(&b[1], c, n);
  pio_i2c_write_blocking(ctx->pio, ctx->sm, i2caddr, b, n+1);
  free(b);
}

static void ssd1306_command1(ssd1306_context *ctx, uint8_t c) {
  char f[2];
  f[0] = 0x0;
  f[1] = c;
  pio_i2c_write_blocking(ctx->pio, ctx->sm, i2caddr, f, 2);
}

void ssd1306_write_pixel(ssd1306_context *ctx, int16_t x, int16_t y, uint16_t color) {
    if ((x >= 0) && (x < WIDTH) && (y >= 0) && (y < HEIGHT)) {
    // Pixel is in-bounds. Rotate coordinates if needed.
    switch (ssd1306_get_rotation()) {
    case 1:
      ssd1306_swap(x, y);
      x = WIDTH - x - 1;
      break;
    case 2:
      x = WIDTH - x - 1;
      y = HEIGHT - y - 1;
      break;
    case 3:
      ssd1306_swap(x, y);
      y = HEIGHT - y - 1;
      break;
    }
    switch (color) {
    case SSD1306_WHITE:
      ctx->buffer[x + (y / 8) * WIDTH] |= (1 << (y & 7));
      break;
    case SSD1306_BLACK:
      ctx->buffer[x + (y / 8) * WIDTH] &= ~(1 << (y & 7));
      break;
    case SSD1306_INVERSE:
      ctx->buffer[x + (y / 8) * WIDTH] ^= (1 << (y & 7));
      break;
    }
  }
}

void ssd1306_draw_bitmap(ssd1306_context *ctx, int16_t x, int16_t y, const uint8_t bitmap[],
                              int16_t w, int16_t h, uint16_t color) {

  int16_t byteWidth = (w + 7) / 8; // Bitmap scanline pad = whole byte
  uint8_t byte = 0;

  for (int16_t j = 0; j < h; j++, y++) {
    for (int16_t i = 0; i < w; i++) {
      if (i & 7)
        byte <<= 1;
      else
        byte = bitmap[j * byteWidth + i / 8];
      if (byte & 0x80)
        ssd1306_write_pixel(ctx, x + i, y, color);
    }
  }
}


void ssd1306_display(ssd1306_context *ctx) {
  static uint8_t pageCommands[] = {
      0xB0,                  // page 0, set to 0xB0 + actual page index!
      0x00 | COLUMN_OFFSET,  // lower columns address = 0x00 for ssd1306, 0x02 for sh1106
      0x10 | 0x00            // upper columns address = 0x00
  };
  
  uint16_t pages = ((HEIGHT + 7) / 8);
  uint8_t *ptr = ctx->buffer;

  char *pageBuffer = malloc(1 + WIDTH);
  memset(pageBuffer, 0x00, 1 + WIDTH);
  pageBuffer[0] = 0x40; // indicate this is data instead of command

  for (uint16_t row=0; row<pages; row++) {
    pageCommands[0] = 0xB0 + row; // set page index
    ssd1306_command_list(ctx, pageCommands, sizeof(pageCommands));

    memcpy(&pageBuffer[1], ptr + (row * WIDTH), WIDTH);
    pio_i2c_write_blocking(ctx->pio, ctx->sm, i2caddr, pageBuffer, 1 + WIDTH);
  }

  free(pageBuffer);
}
/*!
    @brief  Allocate RAM for image buffer, initialize peripherals and pins.
    @param  vcs
            VCC selection. Pass SSD1306_SWITCHCAPVCC to generate the display
            voltage (step up) from the 3.3V source, or SSD1306_EXTERNALVCC
            otherwise. Most situations with Adafruit SSD1306 breakouts will
            want SSD1306_SWITCHCAPVCC.
    @param  addr
            I2C address of corresponding SSD1306 display (or pass 0 to use
            default of 0x3C for 128x32 display, 0x3D for all others).
            SPI displays (hardware or software) do not use addresses, but
            this argument is still required (pass 0 or any value really,
            it will simply be ignored). Default if unspecified is 0.
    @param  reset
            If true, and if the reset pin passed to the constructor is
            valid, a hard reset will be performed before initializing the
            display. If using multiple SSD1306 displays on the same bus, and
            if they all share the same reset pin, you should only pass true
            on the first display being initialized, false on all others,
            else the already-initialized displays would be reset. Default if
            unspecified is true.
    @param  periphBegin
            If true, and if a hardware peripheral is being used (I2C or SPI,
            but not software SPI), call that peripheral's begin() function,
            else (false) it has already been done in one's sketch code.
            Cases where false might be used include multiple displays or
            other devices sharing a common bus, or situations on some
            platforms where a nonstandard begin() function is available
            (e.g. a TwoWire interface on non-default pins, as can be done
            on the ESP8266 and perhaps others).
    @return true on successful allocation/init, false otherwise.
            Well-behaved code should check the return value before
            proceeding.
    @note   MUST call this function before any drawing or updates!
*/
bool ssd1306_begin(ssd1306_context *ctx, uint8_t vccstate) {
  if(ctx == NULL) {
    return false;
  }

  ctx->buffer = malloc(WIDTH * ((HEIGHT + 7) / 8));
  if(ctx->buffer == NULL) {
    return false;
  }
  
  ssd1306_clear_display(ctx);
  ssd1306_draw_bitmap(ctx, (WIDTH - splash1_width) / 2, (HEIGHT - splash1_height) / 2,
               splash1_data, splash1_width, splash1_height, 1);

   // Init sequence
  static const uint8_t  init1[] = {SSD1306_DISPLAYOFF,         // 0xAE
                                          SSD1306_SETDISPLAYCLOCKDIV, // 0xD5
                                          0x80, // the suggested ratio 0x80
                                          SSD1306_SETMULTIPLEX}; // 0xA8
  ssd1306_command_list(ctx, init1, sizeof(init1));
  ssd1306_command1(ctx, HEIGHT - 1);

  static const uint8_t  init2[] = {SSD1306_SETDISPLAYOFFSET, // 0xD3
                                          0x0,                      // no offset
                                          SSD1306_SETSTARTLINE | 0x0, // line #0
                                          SSD1306_CHARGEPUMP};        // 0x8D
  ssd1306_command_list(ctx, init2, sizeof(init2));

  ssd1306_command1(ctx, (vccstate == SSD1306_EXTERNALVCC) ? 0x10 : 0x14);

  static const uint8_t  init3[] = {SSD1306_MEMORYMODE, // 0x20
                                          0x00, // 0x0 act like ks0108
                                          SSD1306_SEGREMAP | 0x1,
                                          SSD1306_COMSCANDEC};
  ssd1306_command_list(ctx, init3, sizeof(init3));

  uint8_t comPins = 0x02;
  contrast = 0x8F;

  if ((WIDTH == 128) && (HEIGHT == 32)) {
    comPins = 0x02;
    contrast = 0x8F;
  } else if ((WIDTH == 128) && (HEIGHT == 64)) {
    comPins = 0x12;
    contrast = (vccstate == SSD1306_EXTERNALVCC) ? 0x9F : 0xCF;
  } else if ((WIDTH == 96) && (HEIGHT == 16)) {
    comPins = 0x2; // ada x12
    contrast = (vccstate == SSD1306_EXTERNALVCC) ? 0x10 : 0xAF;
  } else {
    // Other screen varieties -- TBD
  }

  ssd1306_command1(ctx, SSD1306_SETCOMPINS);
  ssd1306_command1(ctx, comPins);
  ssd1306_command1(ctx, SSD1306_SETCONTRAST);
  ssd1306_command1(ctx, contrast);

  ssd1306_command1(ctx, SSD1306_SETPRECHARGE); // 0xd9
  ssd1306_command1(ctx, (vccstate == SSD1306_EXTERNALVCC) ? 0x22 : 0xF1);
  static const uint8_t  init5[] = {
      SSD1306_SETVCOMDETECT, // 0xDB
      0x40,
      SSD1306_DISPLAYALLON_RESUME, // 0xA4
      SSD1306_NORMALDISPLAY,       // 0xA6
      SSD1306_DEACTIVATE_SCROLL,
      SSD1306_DISPLAYON}; // Main screen turn on
  ssd1306_command_list(ctx, init5, sizeof(init5));



  ssd1306_display(ctx);
  return true; // Success
}
