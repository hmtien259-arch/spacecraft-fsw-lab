# labs/02-rtos/Makefile
#
# Builds the FreeRTOS-on-POSIX housekeeping labs (Session 5 two-task
# pipeline and Session 6 four-task system) against a locally fetched copy
# of the FreeRTOS kernel. The kernel itself is NOT vendored into this repo
# (see fetch_freertos in ../../tools/scripts) - run `make deps` once, or
# just `make`, which fetches it automatically if missing.

KERNEL_DIR := FreeRTOS/FreeRTOS-Kernel
POSIX_PORT := $(KERNEL_DIR)/portable/ThirdParty/GCC/Posix

CC      := gcc
CFLAGS  := -Wall -Wextra -std=gnu11 -g -O0 \
           -I. \
           -I$(KERNEL_DIR)/include \
           -I$(POSIX_PORT)
LDFLAGS := -pthread

KERNEL_SRCS := $(KERNEL_DIR)/list.c \
               $(KERNEL_DIR)/queue.c \
               $(KERNEL_DIR)/tasks.c \
               $(KERNEL_DIR)/timers.c \
               $(KERNEL_DIR)/portable/MemMang/heap_3.c \
               $(POSIX_PORT)/port.c \
               $(POSIX_PORT)/utils/wait_for_event.c

.PHONY: all deps clean pipeline ipc

deps:
	../../tools/scripts/fetch_freertos.sh

all: pipeline ipc

# Session 5 - two task sampler/telemetry pipeline
pipeline: deps hk_freertos.c freertos_hooks.c ../01-foundation/hk_telemetry.c
	$(CC) $(CFLAGS) hk_freertos.c freertos_hooks.c $(KERNEL_SRCS) -o build/hk_freertos $(LDFLAGS)

# Session 6 - four task system (sampler, telemetry, reporter, safe-mode handler)
ipc: deps hk_freertos_ipc.c freertos_hooks.c ../01-foundation/hk_telemetry.c
	$(CC) $(CFLAGS) hk_freertos_ipc.c freertos_hooks.c $(KERNEL_SRCS) -o build/hk_freertos_ipc $(LDFLAGS)

build:
	mkdir -p build

pipeline ipc: | build

clean:
	rm -rf build
