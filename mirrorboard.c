#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>


int uinput;
struct uinput_user_dev uinput_device;
int evdev = -1;
char name[256]= "Unknown";

__u16 REMAPPED_CODES[][2] = {
  // Number row
  {KEY_1, KEY_0},
  {KEY_2, KEY_9},
  {KEY_3, KEY_8},
  {KEY_4, KEY_7},
  {KEY_5, KEY_6},
    
  // First letter row
  {KEY_P, KEY_Q},
  {KEY_W, KEY_O},
  {KEY_E, KEY_I},
  {KEY_R, KEY_U},
  {KEY_T, KEY_Y},

  // Second letter row
  {KEY_A, KEY_SEMICOLON},
  {KEY_S, KEY_L},
  {KEY_D, KEY_K},
  {KEY_F, KEY_J},
  {KEY_G, KEY_H},

  // Third letter row
  {KEY_Z, KEY_SLASH},
  {KEY_X, KEY_DOT},
  {KEY_C, KEY_COMMA},
  {KEY_V, KEY_M},
  {KEY_B, KEY_N},

  // Special keys
  {KEY_CAPSLOCK, KEY_ENTER},
  {KEY_TAB, KEY_BACKSPACE}
};


unsigned long MIRROR_SPACE_BURST_LIMIT = 250000; // A quarter of a second, better be fast...

void setupInputDevice(char *device) {
  if ((evdev = open(device, O_RDONLY)) < 0) {
    printf("Cannot open evdev (the input) device. Please check the given path: %s\n", device);
    exit(1);
  }

  int value = 1;
  ioctl(evdev, EVIOCGRAB, &value);

  if(ioctl(evdev, EVIOCGNAME(sizeof(name)), name) < 0) {
    perror("evdev ioctl");
  }

  printf("%s: %s\n", device, name);
}


void setupOutputDevice() {
  uinput = open("/dev/input/uinput", O_WRONLY | O_NDELAY);
  
  memset(&uinput_device, 0, sizeof(uinput_device));

  strncpy(uinput_device.name, "MirrorBoard", UINPUT_MAX_NAME_SIZE);

  uinput_device.id.version = 4;
  uinput_device.id.bustype = BUS_USB;

  // Setup the uinput device as keyboard event emitting, and
  // ALL YOUR KEYS ARE BELONG TO DEVICE!
  
  ioctl(uinput, UI_SET_EVBIT, EV_KEY);

  int i;
  for (i=0; i < 256; i++) {
    ioctl(uinput, UI_SET_KEYBIT, i);
  }

  /* Create input device into input sub-system */

  write(uinput, &uinput_device, sizeof(uinput_device));

  if (ioctl(uinput, UI_DEV_CREATE)) {
    printf("Unable to create UINPUT device.");
  }
}


int setupDevices(char *inputdevice) {
  sleep(1);

  // Open and take control of the evdev input device
  setupInputDevice(inputdevice);

  // Setup an UInput output device
  setupOutputDevice();
}


__u16 findMapping(__u16 original) {
  int idx;

  for (idx = 0; idx < (sizeof(REMAPPED_CODES)/sizeof(__u16)/2); idx++) {
    if (REMAPPED_CODES[idx][0] == original) {
      return REMAPPED_CODES[idx][1];
    } else if (REMAPPED_CODES[idx][1] == original) {
      return REMAPPED_CODES[idx][0];
    }
  }

  return KEY_RESERVED;
}



unsigned char outsideMirror = 1;
unsigned char outside[256];
unsigned char inside[256];

unsigned long mirrorStartTime = -1;
unsigned long mirrorCount = 0;

void goInside(struct input_event evt) {
#ifdef DEBUG
  printf(">> Going inside\n");
#endif
  
  if (outsideMirror) {
    outsideMirror = 0;
    mirrorStartTime = evt.time.tv_sec * 1000000 + evt.time.tv_usec;
    mirrorCount = 0;

  } else {
    printf("Warning: already inside the mirror!\n");
  }
}

void goOutside() {
#ifdef DEBUG
  printf(">> Going outside\n");
#endif

  if (!outsideMirror) {
    outsideMirror = 1;

  } else {
    printf("Warning: already outside the mirror!\n");
  }
}

void swallowEvent(struct input_event evt) {
  // Nothing. That's why it's called swallowing :-p
#ifdef DEBUG
  printf(">> Swallowing event with code %d\n", evt.code);
#endif
}

void mark(unsigned char map[256], struct input_event evt) {
#ifdef DEBUG
  printf(">> Marking %d in %p\n", evt.code, map);
#endif

  if (!map[evt.code]) {
    map[evt.code] = 1;

  } else {
    printf("Warning: attempt to mark already marked value %d!\n", evt.code);
  }
}

void unmark(unsigned char map[256], struct input_event evt) {
#ifdef DEBUG
  printf(">> Unmarking %d in %p\n", evt.code, map);
#endif

  if (map[evt.code]) {
    map[evt.code] = 0;

  } else {
    printf("Warning: attempt to unmark already unmarked value %d!\n", evt.code);
  }
}

void passEvent(struct input_event evt) {
#ifdef DEBUG
  printf(">> Passing event %d\n", evt.code);
#endif

  write(uinput, &evt, sizeof(evt));  
}

unsigned char marked(unsigned char map[256], struct input_event evt) {
  return map[evt.code];
}

unsigned char burst(struct input_event evt) {
  unsigned long evttime = evt.time.tv_sec * 1000000 + evt.time.tv_usec;

  if (mirrorCount == 0 && (evttime - mirrorStartTime) < MIRROR_SPACE_BURST_LIMIT) {
    return 1;

#ifdef DEBUG
    printf(">> BURST!");
#endif

  } else {
    return 0;
  }
}

void remapEvent(struct input_event evt) {
  __u16 newcode = findMapping(evt.code);

  if (newcode != KEY_RESERVED) {
    evt.code = newcode;

  } else {
    printf("Debug: rewrite requested, but no rewrite for this event. Passing as-is!\n");
  }

  passEvent(evt);
}


void emitSpace() {
  struct input_event evt;

  // Space pressed
  memset(&evt, 0, sizeof(evt));
  gettimeofday(&evt.time, NULL);
  
  evt.type = EV_KEY;
  evt.code = KEY_SPACE;
  evt.value = 1;
  passEvent(evt);
  
  evt.type = EV_SYN;
  evt.code = SYN_REPORT;
  evt.value = 0;
  passEvent(evt);

  // Space released
  memset(&evt, 0, sizeof(evt));
  gettimeofday(&evt.time, NULL);
  
  evt.type = EV_KEY;
  evt.code = KEY_SPACE;
  evt.value = 0;
  passEvent(evt);
  
  evt.type = EV_SYN;
  evt.code = SYN_REPORT;
  evt.value = 0;
  passEvent(evt);  
}


void processEvent(struct input_event evt) {
  if (outsideMirror) {
    // Outside mirror...
      
    switch (evt.value) {
    case 1:if (evt.code == KEY_SPACE) {
	goInside(evt);
	swallowEvent(evt);

      } else {
	mark(outside, evt);
	passEvent(evt);
      }
      break;
	
    case 0:if (marked(outside, evt)) {
	unmark(outside, evt);
	passEvent(evt);

      } else {
	unmark(inside, evt);
	remapEvent(evt);
      }
      break;
	
    case 2:if (marked(outside, evt)) {
	passEvent(evt);
	  
      } else {
	remapEvent(evt);
      }
      break;
    }
      
  } else {
    // Inside mirror
      
    switch (evt.value) {
    case 1:
      mark(inside, evt);
      mirrorCount++;
      remapEvent(evt);
      break;
	  
    case 0:
      if (evt.code == KEY_SPACE) {
	goOutside();
	if (burst(evt)) {
	  emitSpace();

	} else {
	  swallowEvent(evt);
	}

      } else {
	if (marked(inside, evt)) {
	  unmark(inside, evt);
	  remapEvent(evt);

	} else {
	  passEvent(evt);
	}
      }
      break;
	
    case 2:
      if (evt.code == KEY_SPACE) {
	swallowEvent(evt);

      } else {
	if (marked(inside, evt)) {
	  remapEvent(evt);

	} else {
	  passEvent(evt);
	}
      }
    }
  }
}


void mainLoop() {
  int rb;
  struct input_event ev[64];

  memset(&outside, 0, sizeof(outside));
  memset(&inside, 0, sizeof(inside));
  
  while (1) {
    rb=read(evdev,ev,sizeof(struct input_event)*64);

    if (rb < (int) sizeof(struct input_event)) {
      perror("evtest: short read");
      exit (1);
    }

    int counter;
    
    for (counter = 0;
	 counter < (int) (rb / sizeof(struct input_event));
	 counter++) {
      if (EV_KEY == ev[counter].type) {
	processEvent(ev[counter]);
      }
    }
  }
}



void cleanup() {
  struct input_event ev[64];
  
  sleep(1);
  read(evdev, ev, sizeof(struct input_event)*64);
  
  close(evdev);
  exit(0);
}



int main (int argc, char** argv) {
  printf("Okay... mirrorboard activating on %s in 1 second. Release all keys!\n", argv[1]);
  
  setupDevices(argv[1]);
  
  mainLoop();

  printf("Mirrorboard is terminating in 1 second. Release all keys!\n");

  sleep(1);
  cleanup();
}


