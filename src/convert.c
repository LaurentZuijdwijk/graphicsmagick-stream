#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <wand/magick_wand.h>
#include "io.h"
#include "debug.h"

#define MIN(x,y) (x < y ? x : y)

static MagickWand *wand;
static PixelWand *background = NULL;
static char* formats[4] = {"JPG", "GIF", "PNG", "BMP"};

typedef struct {
  uint32_t scale_ratio;
  uint32_t scale_width;
  uint32_t scale_height;
  uint32_t crop_x;
  uint32_t crop_y;
  uint32_t crop_width;
  uint32_t crop_height;
  uint32_t rotate_degrees;
  uint32_t format;
} convert_t;

int convert_format (convert_t *opts) {
  if (!opts->format || opts->format > 4) return MagickPass;
  return MagickSetImageFormat(wand, formats[opts->format-1]);
}

int convert_rotate (convert_t *opts) {
  if (!opts->rotate_degrees) return MagickPass;
  if (opts->rotate_degrees == 360) opts->rotate_degrees = 0;

  char* exif_orientation = MagickGetImageAttribute(wand,  "EXIF:Orientation");
  if (exif_orientation != NULL) {
    int orientation = atoi(exif_orientation);

    switch (orientation) {
      case 3:
      opts->rotate_degrees += 180;
      break;

      case 6:
      opts->rotate_degrees += 90;
      break;

      case 8:
      opts->rotate_degrees += 270;
      break;
    }

    // just clear all exif data
    MagickProfileImage(wand, "exif", NULL, 0);
  }

  return opts->rotate_degrees ? MagickRotateImage(wand, background, opts->rotate_degrees) : MagickPass;
}

int convert_crop (convert_t *opts) {
  unsigned long crop_width = opts->crop_width;
  unsigned long crop_height = opts->crop_height;

  if (!crop_width && !opts->crop_x && !crop_height && !opts->crop_y) return MagickPass;
  if (!crop_width) crop_width = MagickGetImageWidth(wand);
  if (!crop_height) crop_height = MagickGetImageHeight(wand);

  return MagickCropImage(wand, crop_width, crop_height, opts->crop_x, opts->crop_y);
}

int convert_scale (convert_t *opts) {
  unsigned long scale_width = opts->scale_width;
  unsigned long scale_height = opts->scale_height;

  if (!scale_height && !scale_width) return MagickPass;

  unsigned long wid = MagickGetImageWidth(wand);
  unsigned long hei = MagickGetImageHeight(wand);
  double ratio = (double)wid / (double)hei;

  if (!scale_width) scale_width = wid;
  if (!scale_height) scale_height = hei;

  unsigned long new_wid = MIN(scale_width, wid);
  unsigned long new_hei = MIN(scale_height, hei);

  if (ratio > 1) new_hei = (double)new_wid / ratio;
  else new_wid = (double)new_hei * ratio;

  return MagickScaleImage(wand, new_wid, new_hei);
}

int parse (size_t size, unsigned char *data) {
  convert_t *opts = (convert_t*) data;

  size -= sizeof(convert_t);
  data += sizeof(convert_t);

  if (MagickReadImageBlob(wand, data, size) != MagickPass) return -2;

  if (convert_format(opts) != MagickPass) return -3;
  if (convert_scale(opts) != MagickPass)  return -4;
  if (convert_rotate(opts) != MagickPass) return -5;
  if (convert_crop(opts) != MagickPass)   return -6;

  data = MagickWriteImageBlob(wand, &size);
  return io_write(size, data);
}

int main (int argc, char *argv[]) {
  InitializeMagick(*argv);
  background = NewPixelWand();
  wand = NewMagickWand();
  return io_read(parse);
}