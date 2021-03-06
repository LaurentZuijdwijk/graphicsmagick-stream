#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "io.h"
#include "debug.h"
#include <archive.h>
#include <archive_entry.h>
#include <wand/magick_wand.h>
#include "../lib/lib/libimagequant.h"
#include "./lodepng.cpp"

#define MIN(x,y) (x < y ? x : y)


static PixelWand *default_background = NULL;
static char* formats[] = {"NOOP", "INFO", "JPEG", "GIF", "PNG", "BMP", "PDF"};
enum {NOOP=0, INFO=1, JPEG=2, GIF=3, PNG=4, BMP=5, PDF=6};
enum {CONTAIN=0x01, FIXED=0x02, COVER=0x04, MULTIPAGE=0x08};

typedef struct {
  uint32_t scale_options;
  uint32_t scale_width;
  uint32_t scale_height;
  uint32_t crop_x;
  uint32_t crop_y;
  uint32_t crop_width;
  uint32_t crop_height;
  uint32_t rotate_degrees;
  uint32_t density;
  uint32_t page_start;
  uint32_t page_end;
  uint32_t format;
  uint32_t split;
  uint32_t quality;

} convert_t;

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t format;
  uint32_t pages;
} convert_info_t;

struct bufferAndSize {
  size_t size;
  unsigned char *data;
};

int convert_format (MagickWand *input, convert_t *opts) {
  if (opts->format < JPEG || opts->format > BMP) return MagickPass;
  return MagickSetImageFormat(input, formats[opts->format]);
}

int convert_density (MagickWand *input, convert_t* opts) {
  if (!opts->density) return MagickPass;
  return MagickSetResolution(input, opts->density, opts->density);
}

int convert_quality (MagickWand *output, uint32_t quality) {
  if (!quality) return MagickPass;
  
  return MagickSetCompressionQuality(output, quality);
}

int convert_rotate (MagickWand *input, convert_t *opts) {
  if (!opts->rotate_degrees) return MagickPass;
  if (opts->rotate_degrees == 360) opts->rotate_degrees = 0;

  char* exif_orientation = MagickGetImageAttribute(input,  "EXIF:Orientation");

  if (exif_orientation != NULL) {
    switch (atoi(exif_orientation)) {
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
    MagickProfileImage(input, "exif", NULL, 0);
  }

  return opts->rotate_degrees ? MagickRotateImage(input, default_background, opts->rotate_degrees) : MagickPass;
}

int convert_crop (MagickWand *input, convert_t *opts) {
  unsigned long crop_width = opts->crop_width;
  unsigned long crop_height = opts->crop_height;

  if (!crop_width && !opts->crop_x && !crop_height && !opts->crop_y) return MagickPass;
  if (!crop_width) crop_width = MagickGetImageWidth(input);
  if (!crop_height) crop_height = MagickGetImageHeight(input);

  return MagickCropImage(input, crop_width, crop_height, opts->crop_x, opts->crop_y);
}

int convert_scale (MagickWand *input, convert_t *opts) {
  unsigned long scale_width = opts->scale_width;
  unsigned long scale_height = opts->scale_height;

  if (!scale_height && !scale_width) return MagickPass;

  unsigned long wid = MagickGetImageWidth(input);
  unsigned long hei = MagickGetImageHeight(input);
  double ratio = (double)wid / (double)hei;

  unsigned long new_wid = 0;
  unsigned long new_hei = 0;

  if (opts->scale_options & FIXED) {
    new_wid = scale_width;
    new_hei = scale_height;
  } else {
    new_wid = MIN(scale_width, wid);
    new_hei = MIN(scale_height, hei);

    if (new_hei && new_wid) {
      double new_ratio = (double)new_wid / (double)new_hei;

      if (opts->scale_options & COVER) {
        if (new_ratio > ratio) new_hei = 0;
        else new_wid = 0;
      }
      if (opts->scale_options & CONTAIN) {
        if (new_ratio > ratio) new_wid = 0;
        else new_hei = 0;
      }
    }
  }
  if (!new_wid) new_wid = ratio * (double)new_hei;
  if (!new_hei) new_hei = (double)new_wid / ratio;
  return MagickScaleImage(input, new_wid, new_hei);
}

int convert_remove_range(MagickWand *input, uint32_t start, uint32_t end) {
  MagickSetImageIndex(input, start);
  while (start++ < end) {
    if (MagickRemoveImage(input) != MagickPass) return MagickFail;
  }
  return MagickPass;
}

int convert_adjoin (MagickWand *input, MagickWand **output, convert_t *opts) {
  unsigned long pages = MagickGetNumberImages(input);
  uint32_t start = opts->page_start;
  uint32_t end = opts->page_end;

  if (pages < 2 || opts->format == INFO) return MagickPass;

  if (end && end < pages) {
    if (convert_remove_range(input, end, pages) != MagickPass) return MagickFail;
    pages = end;
  }

  if (start && pages) {
    start = MIN(start-1, pages);
    if (convert_remove_range(input, 0, start) != MagickPass) return MagickFail;
    pages -= start;
  }

  if (!pages) return MagickFail;

  if (!opts->split) {
    MagickResetIterator(input);
    input = MagickAppendImages(input, 1);
  }
  *output = input;
  if (*output == NULL) return MagickFail;

  if (opts->scale_height && !(opts->scale_options & MULTIPAGE)) opts->scale_height *= pages;

  return MagickPass;
}

void to_convert_info (MagickWand *input, convert_info_t *res) {
  res->width = MagickGetImageWidth(input);
  res->height = MagickGetImageHeight(input);
  res->format = NOOP;

  res->pages = MagickGetNumberImages(input);

  char *fmt = MagickGetImageFormat(input);
  int i;

  for (i = JPEG; i <= PDF; i++) {
    if (strcmp(fmt, formats[i])) continue;
    res->format = i;
    break;
  }
}

int convert (MagickWand *input, MagickWand **output, convert_t *opts, unsigned char* data, size_t size) {
  if (convert_density(input, opts) != MagickPass) return -7;
  if (MagickReadImageBlob(input, data, size) != MagickPass) return -2;
  if (convert_adjoin(input, output, opts) != MagickPass) return -8;

  input = *output;
  uint32_t quality = 75;
  if(opts->quality != 0){
    quality = opts->quality;
  }
  if(convert_quality(input, quality) != MagickPass) return -8;

  MagickResetIterator(input);
  MagickNextImage(input); // Has to be called after MagickResetIterator to set the first picture as the current
  do {
    if (convert_format(input, opts) != MagickPass) return -3;
    if (convert_crop(input, opts) != MagickPass)   return -6;
    if (convert_scale(input, opts) != MagickPass)  return -4;
    if (convert_rotate(input, opts) != MagickPass) return -5;
  } while (MagickNextImage(input));
  return 0;
}

void destroy (MagickWand *input, MagickWand *output) {
  if (input != NULL) DestroyMagickWand(input);
  if (output != NULL && input != output) DestroyMagickWand(output);
}

int write_archive_from_mem(char *outname, MagickWand *wand)
{


  int archiveSize = 0;
  int pageNumber = 1;
  struct archive *a;
  struct archive_entry *entry;
  a = archive_write_new();
  archive_write_set_format_ustar(a);
  archive_write_open_filename(a, outname);
  char filename[13];
  MagickResetIterator(wand);
  MagickNextImage(wand); // Has to be called after MagickResetIterator to set the first picture as the current
  do { 
    unsigned char *data;
    size_t size;
    data = MagickWriteImageBlob(wand, &size);
    entry = archive_entry_new();
    snprintf(filename, 13, "page_%d", pageNumber++);
    archive_entry_set_pathname(entry, filename);
    archive_entry_set_size(entry, size);
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_write_header(a, entry);
    archiveSize += archive_write_data(a, data, size);
    archive_entry_free(entry);
  } while (MagickNextImage(wand));
  archive_write_close(a);
  archive_write_free(a);
  return archiveSize;
}

struct bufferAndSize pngQuant(MagickWand *output){

      unsigned long wid = MagickGetImageWidth(output);
      unsigned long hei = MagickGetImageHeight(output);
      unsigned char *bmp_buffer;
      bmp_buffer = (unsigned char*) malloc(wid*hei*4);
      MagickGetImagePixels(output,0,0,wid,hei,"RGBA",CharPixel,bmp_buffer);

      liq_attr *attr = liq_attr_create();
      liq_image *qimage = liq_image_create_rgba(attr, bmp_buffer, wid, hei, 0);
      liq_set_max_colors(attr, 255);
      liq_set_speed(attr, 10);
      liq_result *res = liq_quantize_image(attr, qimage);

      int png_buffer_size = wid*hei;
      unsigned char *png_buffer = malloc(png_buffer_size);

      liq_write_remapped_image(res, qimage, png_buffer, png_buffer_size);
      const liq_palette *pal = liq_get_palette(res);

      LodePNGState state;

      lodepng_state_init(&state);
      /*optionally customize the state*/
      state.info_png.color.bitdepth = 8;
      state.info_png.color.colortype = LCT_PALETTE;
      state.info_raw.colortype = LCT_PALETTE;
      state.info_raw.bitdepth = 8;
      state.encoder.auto_convert = 0;
      state.encoder.add_id = 0;
      state.encoder.zlibsettings.nicematch = 258;
      int i = 0;
      for (i=0; i < pal->count; ++i)
      {
        lodepng_palette_add(&state.info_png.color, pal->entries[i].r , pal->entries[i].g, pal->entries[i].b, pal->entries[i].a);
        lodepng_palette_add(&state.info_raw, pal->entries[i].r , pal->entries[i].g, pal->entries[i].b, pal->entries[i].a);
      }
      unsigned char *data;
      size_t size = 0;
      lodepng_encode(&data, &size, png_buffer, wid, hei, &state);
      // writtendata = io_write(size, data);
      lodepng_state_cleanup(&state);
      free(bmp_buffer);
      free(png_buffer);
      liq_attr_destroy(attr);
      liq_image_destroy(qimage);
      liq_result_destroy(res);

      lodepng_state_cleanup(&state);
      struct bufferAndSize bs;
      bs.data = data;
      bs.size = size;
      return bs;
}


int parse (size_t size, unsigned char *data) {
  int writtendata = 0;
  MagickWand *input = NewMagickWand();
  MagickWand *output = input;

  convert_t *opts = (convert_t*) data;
  convert_info_t info_data;

  size -= sizeof(convert_t);
  data += sizeof(convert_t);

  int status = convert(input, &output, opts, data, size);

  if (status < 0) {
    destroy(input, output);
    return status;
  } 

  if (opts->format == INFO) {
    to_convert_info(output, &info_data);
    data = (unsigned char*) &info_data;
    size = sizeof(convert_info_t);
    writtendata = io_write(size, data);
  } else {
    if (opts->split) {
      // If we split the file then we will make a tarball containing all the pages
      char* filename;
      filename = tmpnam (NULL);

      size = write_archive_from_mem(filename, output);
      writtendata = io_write_file_to_stdout(filename);
      unlink(filename);
    } else {
 
    char *format = MagickGetImageFormat(output);   

      if(strcmp(format,"PNG") == 0 && opts->quality != 0){
        struct bufferAndSize d = pngQuant(output);
        
        writtendata = io_write(d.size, d.data);
        free(d.data);
        free(format);

      }else{

        data = MagickWriteImageBlob(output, &size);
        writtendata = io_write(size, data);
        free(data);
      }

      destroy(input, output);
    }
  }
  return writtendata;
}



int main (int argc, char *argv[]) {
  InitializeMagick(*argv);
  default_background = NewPixelWand();
  return io_read(parse);
}
