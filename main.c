// SPDX-FileCopyrightText: 2023 Geoffrey D. Bennett <g@b4.vu>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <stdio.h>
#include <dirent.h>
#include <alsa/asoundlib.h>

#include "scarlett2-firmware.h"
#include "scarlett2-ioctls.h"
#include "scarlett2.h"

#define REQUIRED_HWDEP_VERSION_MAJOR 1

// USD IDs are 4:4 characters + newline/null
#define USBID_SIZE 10

// Focusrite USB VID
#define VENDOR_VID 0x1235
#define VENDOR_PREFIX "1235:"
#define VENDOR_PREFIX_LEN 5

// System-wide firmware directory
#define SYSTEM_FIRMWARE_DIR "/usr/lib/firmware/scarlett2"

// Relative-to-executable firmware directory
#define FIRMWARE_DIR "firmware"

// Supported devices
struct scarlett2_device {
  int         pid;
  const char *name;
};

struct scarlett2_device scarlett2_supported[] = {
  { 0x8203, "Scarlett 2nd Gen 6i6" },
  { 0x8204, "Scarlett 2nd Gen 18i8" },
  { 0x8201, "Scarlett 2nd Gen 18i20" },
  { 0x8211, "Scarlett 3rd Gen Solo" },
  { 0x8210, "Scarlett 3rd Gen 2i2" },
  { 0x8212, "Scarlett 3rd Gen 4i4" },
  { 0x8213, "Scarlett 3rd Gen 8i6" },
  { 0x8214, "Scarlett 3rd Gen 18i8" },
  { 0x8215, "Scarlett 3rd Gen 18i20" },
  { 0x8218, "Scarlett 4th Gen Solo" },
  { 0x8219, "Scarlett 4th Gen 2i2" },
  { 0x821a, "Scarlett 4th Gen 4i4" },
  { 0x8206, "Clarett USB 2Pre" },
  { 0x8207, "Clarett USB 4Pre" },
  { 0x8208, "Clarett USB 8Pre" },
  { 0x820a, "Clarett+ 2Pre" },
  { 0x820b, "Clarett+ 4Pre" },
  { 0x820c, "Clarett+ 8Pre" },
  { 0, NULL }
};

// found cards
struct sound_card {
  int         card_num;
  char        card_name[32];
  char        alsa_name[32];
  int         pid;
  const char *product_name;
  int         firmware_version;
};

// list of found cards
struct sound_card *found_cards = NULL;
int found_cards_count = 0;

// found firmware
struct found_firmware {
  char                             *fn;
  struct scarlett2_firmware_header *firmware;
};

// list of found firmware
struct found_firmware *found_firmwares = NULL;
int found_firmwares_count = 0;

// the name we were called as
const char *program_name;

// command-line parameters
const char *command = NULL;
int selected_card_num = -1;
struct sound_card *selected_card = NULL;
int selected_firmware_version = 0;
struct scarlett2_firmware_file *selected_firmware = NULL;

// the open card & ioctl protocol version
snd_hwdep_t *hwdep = NULL;
int protocol_version = 0;

// validate the VID and return the PID
static int check_usb_id(const char *card_name) {
  char proc_path[256];
  char usbid[USBID_SIZE];
  FILE *f;

  snprintf(proc_path, sizeof(proc_path), "/proc/asound/%s/usbid", card_name);
  f = fopen(proc_path, "r");
  if (!f)
    return 0;

  int ret = fread(usbid, 1, sizeof(usbid), f);

  fclose(f);

  if (ret != sizeof(usbid))
    return 0;

  usbid[USBID_SIZE - 1] = 0;

  if (strncmp(usbid, VENDOR_PREFIX, VENDOR_PREFIX_LEN) != 0)
    return 0;

  char *endptr;
  errno = 0;
  int pid = strtol(usbid + VENDOR_PREFIX_LEN, &endptr, 16);
  if (errno != 0 || *endptr != '\0')
    return 0;

  return pid;
}

static struct scarlett2_device *get_device_for_pid(int pid) {
  for (int i = 0; scarlett2_supported[i].name; i++)
    if (scarlett2_supported[i].pid == pid)
      return &scarlett2_supported[i];

  return NULL;
}

static int get_firmware_version(const char *alsa_name) {
  int err;
  snd_ctl_t* ctl_handle;
  snd_ctl_elem_id_t* id;
  snd_ctl_elem_value_t* control;

  // Open the control interface for the specified sound card
  if ((err = snd_ctl_open(&ctl_handle, alsa_name, 0)) < 0) {
    fprintf(
      stderr,
      "Unable to open control interface for card %s: %s\n",
      alsa_name,
      snd_strerror(errno)
    );

    return -1;
  }

  snd_ctl_elem_id_alloca(&id);
  snd_ctl_elem_value_alloca(&control);

  // Set the control we're interested in
  snd_ctl_elem_id_set_interface(id, SND_CTL_ELEM_IFACE_CARD);
  snd_ctl_elem_id_set_name(id, "Firmware Version");

  snd_ctl_elem_value_set_id(control, id);

  // Read the control value
  if (snd_ctl_elem_read(ctl_handle, control) < 0) {
    fprintf(
      stderr,
      "Found supported Scarlett2 device at %s, but cannot read the\n"
      "Firmware Version ALSA control (need a newer kernel version or\n"
      "the updated snd-usb-audio Scarlett2 protocol driver).\n\n",
      alsa_name
    );

    snd_ctl_close(ctl_handle);
    return -1;
  }

  int version = snd_ctl_elem_value_get_integer(control, 0);

  snd_ctl_close(ctl_handle);

  return version;
}

static void enum_cards(void) {
  int card_num = -1;

  if (snd_card_next(&card_num) < 0 || card_num < 0)
    return;

  while (card_num >= 0) {
    char card_name[32];
    snprintf(card_name, sizeof(card_name), "card%d", card_num);

    int pid = check_usb_id(card_name);
    if (!pid)
      goto next;

    struct scarlett2_device *dev = get_device_for_pid(pid);
    if (!dev)
      goto next;

    found_cards_count++;
    found_cards = realloc(
      found_cards,
      sizeof(*found_cards) * found_cards_count
    );
    if (!found_cards) {
      perror("realloc");
      exit(EXIT_FAILURE);
    }

    struct sound_card *sc = &found_cards[found_cards_count - 1];
    sc->card_num = card_num;
    strcpy(sc->card_name, card_name);
    snprintf(sc->alsa_name, sizeof(sc->alsa_name), "hw:%d", card_num);
    sc->pid = pid;
    sc->product_name = dev->name;
    sc->firmware_version = get_firmware_version(sc->alsa_name);

next:
    if (snd_card_next(&card_num) < 0)
      break;
  }
}

static void check_card_selection(void) {
  if (!found_cards_count) {
    fprintf(stderr, "No supported devices found\n");
    exit(EXIT_FAILURE);
  }
  if (selected_card_num == -1) {
    if (found_cards_count > 1) {
      fprintf(stderr, "Error: more than one supported device found\n");
      fprintf(
        stderr,
        "Use '%s list' and '%s -c <card_num> ...' to select a device\n",
        program_name,
        program_name
      );
      exit(EXIT_FAILURE);
    }
    selected_card_num = found_cards[0].card_num;
  }

  for (int i = 0; i < found_cards_count; i++)
    if (found_cards[i].card_num == selected_card_num) {
      selected_card = &found_cards[i];
      printf("Selected device %s\n", selected_card->product_name);
      return;
    }

  fprintf(stderr, "Error: selected card %d not found\n", selected_card_num);
  fprintf(
    stderr,
    "Use '%s list' to list supported devices\n",
    program_name
  );
  exit(EXIT_FAILURE);
}

static void add_found_firmware(
  char *fn,
  struct scarlett2_firmware_header *firmware
) {
  if (firmware->usb_vid != VENDOR_VID)
    return;

  for (int i = 0; i < found_firmwares_count; i++) {
    struct found_firmware *found_firmware = &found_firmwares[i];

    // already have this firmware under a different name?
    if (found_firmware->firmware->usb_vid == firmware->usb_vid &&
        found_firmware->firmware->usb_pid == firmware->usb_pid &&
        found_firmware->firmware->firmware_version ==
          firmware->firmware_version)
      return;
  }

  found_firmwares_count++;
  found_firmwares = realloc(
    found_firmwares,
    sizeof(*found_firmwares) * found_firmwares_count
  );
  if (!found_firmwares) {
    perror("realloc");
    exit(EXIT_FAILURE);
  }

  struct found_firmware *fw = &found_firmwares[found_firmwares_count - 1];
  fw->fn = fn;
  fw->firmware = firmware;
}

static void enum_firmware_dir(const char *dirname) {
  DIR *dir;
  struct dirent *entry;

  if ((dir = opendir(dirname)) == NULL) {
    if (errno == ENOENT)
      return;
    fprintf(stderr, "Unable to opendir %s: %s\n", dirname, strerror(errno));
    return;
  }

  while ((entry = readdir(dir)) != NULL) {

    // Check if the file is a .bin file
    if (!strstr(entry->d_name, ".bin"))
      continue;

    // Construct full path
    size_t path_len = strlen(dirname) + strlen(entry->d_name) + 2;
    char *full_path = malloc(path_len);

    if (!full_path) {
      perror("malloc");
      return;
    }

    snprintf(full_path, path_len, "%s/%s", dirname, entry->d_name);

    // Parse the firmware file
    struct scarlett2_firmware_header *firmware =
      scarlett2_read_firmware_header(full_path);
    if (firmware) {
      add_found_firmware(full_path, firmware);
    } else {
      fprintf(stderr, "Failed to read firmware file: %s\n", full_path);
      free(full_path);
    }
  }

  closedir(dir);
}

static char *get_exec_dir(void) {
  char *path = malloc(PATH_MAX);

  if (!path) {
    perror("malloc");
    return NULL;
  }

  ssize_t len = readlink("/proc/self/exe", path, PATH_MAX);
  if (len == -1) {
    perror("readlink");
    free(path);
    return NULL;
  }

  path[len] = '\0';

  char *last_slash = strrchr(path, '/');
  if (!last_slash) {
    fprintf(stderr, "Unable to find last slash in path: %s\n", path);
    free(path);
    return NULL;
  }

  *last_slash = '\0';

  return path;
}

static char *get_firmware_exec_dir(void) {
  char *exec_dir = get_exec_dir();
  if (!exec_dir)
    return NULL;

  char *firmware_dir = malloc(strlen(exec_dir) + strlen(FIRMWARE_DIR) + 2);
  if (!firmware_dir) {
    perror("malloc");
    free(exec_dir);
    return NULL;
  }

  snprintf(firmware_dir, strlen(exec_dir) + strlen(FIRMWARE_DIR) + 2,
           "%s/%s", exec_dir, FIRMWARE_DIR);

  free(exec_dir);

  return firmware_dir;
}

static int found_firmware_cmp(const void *p1, const void *p2) {
  const struct found_firmware *ff1 = p1;
  const struct found_firmware *ff2 = p2;
  struct scarlett2_firmware_header *f1 = ff1->firmware;
  struct scarlett2_firmware_header *f2 = ff2->firmware;
  struct scarlett2_device *d1 = get_device_for_pid(f1->usb_pid);
  struct scarlett2_device *d2 = get_device_for_pid(f2->usb_pid);

  // compare location in scarlett2_supported array
  if (d1 < d2)
    return -1;
  if (d1 > d2)
    return 1;

  // compare firmware version, newest first
  if (f1->firmware_version < f2->firmware_version)
    return 1;
  if (f1->firmware_version > f2->firmware_version)
    return -1;

  return 0;
}

static void enum_firmwares(void) {

  /* look for firmware files in the exec-relative firmware directory */
  char *firmware_dir = get_firmware_exec_dir();
  if (firmware_dir) {
    enum_firmware_dir(firmware_dir);
    free(firmware_dir);
  }

  /* look for firmware files in the system firmware directory */
  enum_firmware_dir(SYSTEM_FIRMWARE_DIR);

  qsort(
    found_firmwares,
    found_firmwares_count,
    sizeof(*found_firmwares),
    found_firmware_cmp
  );
}

static struct found_firmware *get_latest_firmware(int pid) {
  for (int i = 0; i < found_firmwares_count; i++) {
    struct found_firmware *found_firmware = &found_firmwares[i];

    // found_firmwares is sorted by version descending so first is
    // latest
    if (found_firmware->firmware->usb_pid == pid)
      return found_firmware;
  }

  return NULL;
}

static struct found_firmware *get_firmware_for_version(int pid, int version) {
  for (int i = 0; i < found_firmwares_count; i++) {
    struct found_firmware *found_firmware = &found_firmwares[i];

    if (found_firmware->firmware->usb_pid == pid &&
        found_firmware->firmware->firmware_version == version)
      return found_firmware;
  }

  return NULL;
}

static void check_firmware_selection(void) {

  struct found_firmware *ff;

  // no firmware version specified, use latest
  if (!selected_firmware_version) {
    ff = get_latest_firmware(selected_card->pid);

    if (!ff) {
      fprintf(
        stderr,
        "No firmware available for %s\n",
        selected_card->product_name
      );
      exit(EXIT_FAILURE);
    }

    // check if latest firmware is newer
    if (selected_card->firmware_version >= ff->firmware->firmware_version) {
      fprintf(
        stderr,
        "Firmware %d for %s is already up to date\n",
        selected_card->firmware_version,
        selected_card->product_name
      );
      exit(EXIT_FAILURE);
    }

  // firmware version specified, check if it's available
  } else {
    ff = get_firmware_for_version(
      selected_card->pid,
      selected_firmware_version
    );

    if (!ff) {
      fprintf(
        stderr,
        "No firmware version %d available for %s\n",
        selected_firmware_version,
        selected_card->product_name
      );
      exit(EXIT_FAILURE);
    }
  }

  // read the firmware file
  selected_firmware = scarlett2_read_firmware_file(ff->fn);

  if (!selected_firmware) {
    fprintf(stderr, "Unable to load firmware\n");
    exit(EXIT_FAILURE);
  }

  // double-check the PID
  if (selected_firmware->header.usb_pid != selected_card->pid) {
    fprintf(
      stderr,
      "Firmware file is for a different device (PID %04x != %04x)\n",
      selected_firmware->header.usb_pid,
      selected_card->pid
    );
    exit(EXIT_FAILURE);
  }

  // display the firmware version and filename
  printf(
    "Found firmware version %d for %s:\n"
    "  %s\n",
    selected_firmware->header.firmware_version,
    selected_card->product_name,
    ff->fn
  );
}

static void usage(void) {
  printf(
    "Scarlett2 Firmware Management Tool Version %s\n"
    "(Scarlett 2nd, 3rd, and 4th Gen, Clarett USB, and Clarett+ series)\n"
    "\n"
    "Usage: %s [options] [command]\n"
    "\n"
    "Commonly-used commands:\n"
    "  -h, help              Display this information\n"
    "  -l, list              List currently connected devices and\n"
    "                        if a firmware update is available\n"
    "  -u, update            Update firmware on the device\n"
    "  about                 Display more information\n"
    "\n"
    "Lesser-used commands:\n"
    "  list-all              List all supported products and\n"
    "                        available firmware versions\n"
    "  reboot                Reboot device\n"
    "  reset-config          Reset configuration to factory defaults\n"
    "  erase-firmware        Reset device to factory firmware\n"
    "\n"
    "Lesser-used options:\n"
    "  -c NUM, --card NUM    Select a specific device\n"
    "                        (only needed if more than one connected)\n"
    "  --fw-ver NUM          Select a specific firmware version\n"
    "\n"
    "Support: https://github.com/geoffreybennett/scarlett2\n"
    "Configuration GUI: https://github.com/geoffreybennett/alsa-scarlett-gui\n"
    "\n",
    VERSION,
    program_name
  );
}

static void short_help(void) {
  fprintf(stderr, "Use '%s help' for help\n", program_name);
  exit(EXIT_FAILURE);
}

static void about(void) {
  printf(
    "Scarlett2 Firmware Management Tool Version %s\n"
    "\n"
    "ABOUT\n"
    "-----\n"
    "\n"
    "Provides firmware management for Focusrite(R) interfaces using the\n"
    "Scarlett2 USB protocol, including Scarlett 2nd, 3rd, 4th Gen, Clarett\n"
    "USB, and Clarett+ series.\n"
    "\n"
    "REQUIREMENTS\n"
    "------------\n"
    "\n"
    "Requires Linux kernel 6.8 or later, or a backported version of the\n"
    "Scarlett2 USB protocol driver from\n"
    "  https://github.com/geoffreybennett/scarlett-gen2/\n"
    "\n"
    "Requires device firmware to be placed in:\n"
    "  %s or\n"
    "  %s\n"
    "\n"
    "Obtain firmware from:\n"
    "  TBA\n"
    "\n"
    "COPYRIGHT AND LEGAL INFORMATION\n"
    "-------------------------------\n"
    "\n"
    "Copyright 2023 Geoffrey D. Bennett <g@b4.vu>\n"
    "License: GPL-3.0-or-later\n"
    "\n"
    "This program is free software: you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation, either version 3 of the License, or (at\n"
    "your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful, but\n"
    "WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU\n"
    "General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program. If not, see https://www.gnu.org/licenses/\n"
    "\n"
    "Focusrite, Scarlett, Clarett, and Vocaster are trademarks or\n"
    "registered trademarks of Focusrite Audio Engineering Limited in\n"
    "England, USA, and/or other countries. Use of these trademarks does not\n"
    "imply any affiliation or endorsement of this software.\n"
    "\n"
    "SUPPORT AND ADDITIONAL SOFTWARE\n"
    "-------------------------------\n"
    "\n"
    "For support, please open an issue on GitHub:\n"
    "  https://github.com/geoffreybennett/scarlett2\n"
    "\n"
    "GUI control panel available at:\n"
    "  https://github.com/geoffreybennett/alsa-scarlett-gui\n"
    "\n"
    "DONATIONS\n"
    "---------\n"
    "\n"
    "This software was developed over hundreds of hours using my personal\n"
    "resources. If you find it useful, please consider a donation to show\n"
    "your appreciation:\n"
    "  https://liberapay.com/gdb\n"
    "  https://www.paypal.me/gdbau\n"
    "\n",
    VERSION, SYSTEM_FIRMWARE_DIR, get_firmware_exec_dir()
  );
  exit(0);
}

static void parse_args(int argc, char *argv[]) {
  for (int i = 1; i < argc; i++) {

    char *arg = argv[i];

    // -c, --card
    if (strncmp(arg, "-c", 2) == 0 ||
        strcmp(arg, "--card") == 0 ||
        strncmp(arg, "--card=", 7) == 0) {

      char *card_num_str;

      // support -cN
      if (strncmp(arg, "-c", 2) == 0 && strlen(arg) > 2) {
        card_num_str = arg + 2;

      // and --card=N
      } else if (strncmp(arg, "--card=", 7) == 0) {
        card_num_str = arg + 7;

      // and -c N, --card N
      } else {
        if (i + 1 >= argc) {
          fprintf(
            stderr, "Missing argument for %s (requires a card number)\n", arg
          );
          exit(EXIT_FAILURE);
        }

        card_num_str = argv[++i];
      }

      // make sure -c, --card hasn't already been seen
      if (selected_card_num != -1) {
        fprintf(stderr, "Error: multiple cards specified\n");
        exit(EXIT_FAILURE);
      }

      // parse N
      char *endptr;
      errno = 0;
      selected_card_num = strtol(card_num_str, &endptr, 10);
      if (errno != 0 || *endptr != '\0' || selected_card_num < 0) {
        fprintf(
          stderr,
          "Invalid argument '%s' (should be a card number)\n",
          card_num_str
        );
        exit(EXIT_FAILURE);
      }

    // --fw-ver
    } else if (strcmp(arg, "--fw-ver") == 0 ||
               strncmp(arg, "--fw-ver=", 9) == 0) {

      char *fw_ver_str;

      // support --fw-ver=N
      if (strncmp(arg, "--fw-ver=", 9) == 0) {
        fw_ver_str = arg + 9;

      } else {
        if (i + 1 >= argc) {
          fprintf(
            stderr,
            "Missing argument for %s (requires a firmware version number)\n",
            arg
          );
          exit(EXIT_FAILURE);
        }

        fw_ver_str = argv[++i];
      }

      // make sure --fw-ver hasn't already been seen
      if (selected_firmware_version) {
        fprintf(stderr, "Error: multiple firmware versions specified\n");
        exit(EXIT_FAILURE);
      }

      // parse N
      char *endptr;
      errno = 0;
      selected_firmware_version = strtol(fw_ver_str, &endptr, 10);
      if (errno != 0 || *endptr != '\0' || selected_firmware_version <= 0) {
        fprintf(
          stderr,
          "Invalid argument '%s' (should be a firmware version number)\n",
          fw_ver_str
        );
        exit(EXIT_FAILURE);
      }

    // short-form commands
    } else if (arg[0] == '-') {
      char *short_command = NULL;

      if (strlen(arg) == 2)
        switch (arg[1]) {
          case 'h': short_command = "help";   break;
          case 'l': short_command = "list";   break;
          case 'u': short_command = "update"; break;
        }

      if (!short_command) {
        fprintf(stderr, "Unknown argument: %s\n", arg);
        short_help();
      }

      if (command) {
        fprintf(
          stderr,
          "Cannot specify %s when a command has already been specified\n",
          arg
        );
        short_help();
      }

      command = short_command;

    // command
    } else if (!command) {
      command = arg;

    // command already specified
    } else {
      fprintf(
        stderr,
        "Cannot specify '%s' when a command has already been specified\n",
        arg
      );
      short_help();
    }
  }

  // check if a card was specified but no command
  if (!command && selected_card_num != -1) {
    fprintf(stderr, "No command specified\n");
    short_help();
  }
}

static void list_cards(void) {
  if (!found_cards) {
    printf("No supported devices found.\n");
    return;
  }

  printf(
    "Found %d supported device%s:\n",
    found_cards_count,
    found_cards_count > 1 ? "s" : ""
  );
  for (int i = 0; i < found_cards_count; i++) {
    struct sound_card *sc = &found_cards[i];
    struct found_firmware *ff = get_latest_firmware(sc->pid);
    int upgrade_version = 0;

    if (ff) {
      struct scarlett2_firmware_header *firmware = ff->firmware;
      if (firmware->firmware_version > sc->firmware_version)
        upgrade_version = firmware->firmware_version;
    }

    if (upgrade_version) {
      printf(
        "  %s: %s (firmware %d, update to %d available)\n",
        sc->card_name,
        sc->product_name,
        sc->firmware_version,
        upgrade_version
      );
    } else {
      printf(
        "  %s: %s (firmware version %d)\n",
        sc->card_name,
        sc->product_name,
        sc->firmware_version
      );
    }
  }
}

static int is_connected(int pid) {
  for (int i = 0; i < found_cards_count; i++)
    if (found_cards[i].pid == pid)
      return 1;

  return 0;
}

// list all supported products and available firmware versions
static void list_all(void) {
  struct scarlett2_device *dev;

  // no firmware found in either directory
  if (!found_firmwares_count) {
    printf("No firmware found.\n\n");
    printf(
      "Scarlett2 firmware files should be placed in:\n"
      "  %s or\n"
      "  %s\n\n",
      SYSTEM_FIRMWARE_DIR,
      get_firmware_exec_dir()
    );
  }

  printf(
    "USB Product ID, Product Name, and Firmware versions available "
    "(* = connected)\n"
  );

  // explain the '*' prefix
  for (dev = scarlett2_supported; dev->name; dev++) {
    printf(
      "%c%04x %-25s",
      is_connected(dev->pid) ? '*' : ' ',
      dev->pid,
      dev->name
    );

    int first = 1;
    for (int i = 0; i < found_firmwares_count; i++) {
      struct found_firmware *found_firmware = &found_firmwares[i];
      struct scarlett2_firmware_header *firmware = found_firmware->firmware;

      if (firmware->usb_pid != dev->pid)
        continue;

      if (!first)
        printf(", ");

      printf("%d", firmware->firmware_version);
      first = 0;
    }

    // display the running firmware version if connected
    if (is_connected(dev->pid)) {
      printf(" (running: ");

      int first = 1;
      for (int i = 0; i < found_cards_count; i++) {
        struct sound_card *sc = &found_cards[i];

        if (sc->pid != dev->pid)
          continue;

        if (!first)
          printf(", ");

        printf("%d", sc->firmware_version);
        first = 0;
      }

      printf(")");
    }

    printf("\n");
  }
}

// open the device
static void open_card(char *alsa_name) {
  int err;

  if (hwdep)
    return;

  err = scarlett2_open_card(alsa_name, &hwdep);
  if (err < 0) {
    fprintf(
      stderr,
      "Unable to open card %s: %s\n",
      alsa_name,
      snd_strerror(errno)
    );
    exit(EXIT_FAILURE);
  }

  err = scarlett2_get_protocol_version(hwdep);
  if (err < 0) {
    fprintf(
      stderr,
      "Unable to get protocol version on card %s: %s\n",
      alsa_name,
      snd_strerror(errno)
    );
    exit(EXIT_FAILURE);
  }
  protocol_version = err;
  if (SCARLETT2_HWDEP_VERSION_MAJOR(protocol_version) !=
        REQUIRED_HWDEP_VERSION_MAJOR) {
    fprintf(
      stderr,
      "Unsupported protocol version %d.%d.%d on card %s\n",
      SCARLETT2_HWDEP_VERSION_MAJOR(protocol_version),
      SCARLETT2_HWDEP_VERSION_MINOR(protocol_version),
      SCARLETT2_HWDEP_VERSION_SUBMINOR(protocol_version),
      alsa_name
    );
    exit(EXIT_FAILURE);
  }
}

static void reboot_card(void) {
  open_card(selected_card->alsa_name);

  printf("Rebooting interface...\n");

  int err = scarlett2_reboot(hwdep);
  if (err < 0) {
    fprintf(
      stderr,
      "Unable to reboot card %s: %s\n",
      selected_card->alsa_name,
      snd_strerror(errno)
    );
    exit(EXIT_FAILURE);
  }
}

static void monitor_erase_progress(void) {
  int last_progress = 0;
  int progress = 0;
  for (int i = 0; i < 10; i++) {
    progress = scarlett2_get_erase_progress(hwdep);
    if (progress < 0) {
      fprintf(
        stderr,
        "Unable to get erase progress on card %s: %s\n",
        selected_card->alsa_name,
        snd_strerror(progress)
      );
      exit(EXIT_FAILURE);
    }

    if (progress == 255)
      break;

    if (progress > last_progress) {
      printf("\rErase progress: %d%%", progress);
      fflush(stdout);
      last_progress = progress;
      i = 0;
    } else if (progress < last_progress) {
      fprintf(
        stderr,
        "\nErase progress went backwards! (%d%% -> %d%%)\n",
        last_progress,
        progress
      );
      exit(EXIT_FAILURE);
    }
    usleep(50000);
  }

  if (progress != 255) {
    fprintf(
      stderr,
      "\nUnable to get erase progress on card %s: timed out\n",
      selected_card->alsa_name
    );
    exit(EXIT_FAILURE);
  } else {
    printf("\rErase progress: Done!\n");
  }
}

static void reset_config(void) {
  open_card(selected_card->alsa_name);

  printf("Resetting configuration to factory default...\n");

  // send request to erase config
  int err = scarlett2_erase_config(hwdep);
  if (err < 0) {
    fprintf(
      stderr,
      "Unable to reset configuration on card %s: %s\n",
      selected_card->alsa_name,
      snd_strerror(errno)
    );
    exit(EXIT_FAILURE);
  }

  monitor_erase_progress();
}

static void erase_firmware(void) {
  open_card(selected_card->alsa_name);

  printf("Erasing upgrade firmware...\n");

  // send request to erase firmware
  int err = scarlett2_erase_firmware(hwdep);
  if (err < 0) {
    fprintf(
      stderr,
      "Unable to erase upgrade firmware on card %s: %s\n",
      selected_card->alsa_name,
      snd_strerror(errno)
    );
    exit(EXIT_FAILURE);
  }

  monitor_erase_progress();
}

static void update_firmware(void) {
  open_card(selected_card->alsa_name);

  // write the firmware
  size_t offset = 0;
  size_t len = selected_firmware->header.firmware_length;
  unsigned char *buf = selected_firmware->firmware_data;

  while (offset < len) {
    int err = snd_hwdep_write(hwdep, buf + offset, len - offset);
    if (err < 0) {
      fprintf(
        stderr,
        "Unable to write firmware to card %s: %s\n",
        selected_card->alsa_name,
        snd_strerror(errno)
      );
      exit(EXIT_FAILURE);
    }

    if (!err) {
      fprintf(
        stderr,
        "Unable to write firmware to card %s: offset %lu (len %lu) "
          "returned 0\n",
        selected_card->alsa_name,
        offset,
        len
      );
      exit(EXIT_FAILURE);
    }

    offset += err;

    int progress = (offset * 100) / len;
    printf("\rFirmware write progress: %d%%", progress);
    fflush(stdout);
  }

  printf("\rFirmware write progress: Done!\n");
}

int main(int argc, char *argv[]) {
  program_name = argv[0];

  parse_args(argc, argv);

  if (!command)
    command = "list";

  if (!strcmp(command, "help")) {
    usage();
  } else if (!strcmp(command, "about")) {
    about();
  } else if (!strcmp(command, "list")) {
    enum_cards();
    enum_firmwares();
    list_cards();
  } else if (!strcmp(command, "list-all")) {
    enum_cards();
    enum_firmwares();
    list_all();
  } else if (!strcmp(command, "reboot")) {
    enum_cards();
    check_card_selection();
    reboot_card();
  } else if (!strcmp(command, "reset-config")) {
    enum_cards();
    check_card_selection();
    reset_config();
    reboot_card();
  } else if (!strcmp(command, "erase-firmware")) {
    enum_cards();
    check_card_selection();
    reset_config();
    erase_firmware();
    reboot_card();
  } else if (!strcmp(command, "update")) {
    enum_cards();
    enum_firmwares();
    check_card_selection();
    check_firmware_selection();

    printf(
      "Updating %s from firmware version %d to %d\n",
      selected_card->product_name,
      selected_card->firmware_version,
      selected_firmware->header.firmware_version
    );

    reset_config();
    erase_firmware();
    update_firmware();
    reboot_card();
  } else {
    fprintf(stderr, "Unknown command: %s\n\n", command);
    short_help();
  }

  return 0;
}
