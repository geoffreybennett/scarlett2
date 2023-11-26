// SPDX-FileCopyrightText: 2023 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scarlett2-firmware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <openssl/sha.h>

static int verify_sha256(
  const unsigned char *data,
  size_t length,
  const unsigned char *expected_hash
) {
  unsigned char computed_hash[SHA256_DIGEST_LENGTH];
  SHA256(data, length, computed_hash);
  return memcmp(computed_hash, expected_hash, SHA256_DIGEST_LENGTH) == 0;
}

static struct scarlett2_firmware_file *read_header(FILE *file) {
  struct scarlett2_firmware_file *firmware = calloc(
    1, sizeof(struct scarlett2_firmware_file)
  );
  if (!firmware) {
    perror("Failed to allocate memory for firmware structure");
    goto error;
  }

  size_t read_count = fread(
    &firmware->header, sizeof(struct scarlett2_firmware_header), 1, file
  );

  if (read_count != 1) {
    if (feof(file))
      fprintf(stderr, "Unexpected end of file\n");
    else
      perror("Failed to read header");
    goto error;
  }

  if (strncmp(firmware->header.magic, MAGIC_STRING, 8) != 0) {
    fprintf(stderr, "Invalid magic number\n");
    goto error;
  }

  firmware->header.usb_vid = ntohs(firmware->header.usb_vid);
  firmware->header.usb_pid = ntohs(firmware->header.usb_pid);
  firmware->header.firmware_version = ntohl(firmware->header.firmware_version);
  firmware->header.firmware_length = ntohl(firmware->header.firmware_length);

  return firmware;

error:
  free(firmware);
  return NULL;
}

struct scarlett2_firmware_header *scarlett2_read_firmware_header(
  const char *fn
) {
  FILE *file = fopen(fn, "rb");
  if (!file) {
    perror("fopen");
    fprintf(stderr, "Unable to open %s\n", fn);
    return NULL;
  }

  struct scarlett2_firmware_file *firmware = read_header(file);
  if (!firmware) {
    fprintf(stderr, "Error reading firmware header from %s\n", fn);
    return NULL;
  }

  fclose(file);

  return realloc(firmware, sizeof(struct scarlett2_firmware_header));
}

struct scarlett2_firmware_file *scarlett2_read_firmware_file(const char *fn) {
  FILE *file = fopen(fn, "rb");
  if (!file) {
    perror("fopen");
    fprintf(stderr, "Unable to open %s\n", fn);
    return NULL;
  }

  struct scarlett2_firmware_file *firmware = read_header(file);
  if (!firmware) {
    fprintf(stderr, "Error reading firmware header from %s\n", fn);
    return NULL;
  }

  firmware->firmware_data = malloc(firmware->header.firmware_length);
  if (!firmware->firmware_data) {
    perror("Failed to allocate memory for firmware data");
    goto error;
  }

  size_t read_count = fread(
    firmware->firmware_data, 1, firmware->header.firmware_length, file
  );

  if (read_count != firmware->header.firmware_length) {
    if (feof(file))
      fprintf(stderr, "Unexpected end of file\n");
    else
      perror("Failed to read firmware data");
    fprintf(stderr, "Error reading firmware data from %s\n", fn);
    goto error;
  }

  if (!verify_sha256(
    firmware->firmware_data,
    firmware->header.firmware_length,
    firmware->header.sha256
  )) {
    fprintf(stderr, "Corrupt firmware (failed checksum) in %s\n", fn);
    goto error;
  }

  fclose(file);
  return firmware;

error:
  scarlett2_free_firmware_file(firmware);
  fclose(file);
  return NULL;
}

void scarlett2_free_firmware_header(struct scarlett2_firmware_header *firmware) {
  if (firmware)
    free(firmware);
}

void scarlett2_free_firmware_file(struct scarlett2_firmware_file *firmware) {
  if (firmware) {
    free(firmware->firmware_data);
    free(firmware);
  }
}
