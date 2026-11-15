# OrbitLink integrated version build (send/receive + XOR + Logger, macOS / Linux)
#   make        build
#   make run    build and run (automatic checks, exit code 0 if all pass)
#   make clean  delete the build folder and the executable
# To use another compiler: make CC=gcc

CC        := clang
BIN       := orbitlink_integrated
BUILD_DIR := build

# Choose the kernel location.
#   1st: FreeRTOS-Kernel inside this folder (if you cloned it yourself, see README)
#   2nd: sibling folder ../FreeRTOS_시뮬레이터/FreeRTOS-Kernel (Cho's PC)
#   Manual:  make KERNEL_DIR=/some/path/FreeRTOS-Kernel
ifneq ($(wildcard FreeRTOS-Kernel/tasks.c),)
KERNEL_DIR := FreeRTOS-Kernel
else
KERNEL_DIR := ../FreeRTOS_시뮬레이터/FreeRTOS-Kernel
endif

# If the kernel sources are missing, stop with a clear message instead of compile errors.
ifeq ($(wildcard $(KERNEL_DIR)/tasks.c),)
$(error FreeRTOS-Kernel not found. Get it as described in README.md)
endif

PORT_DIR := $(KERNEL_DIR)/portable/ThirdParty/GCC/Posix

# Build only the kernel files we need.
#   heap_4.c         : pvPortMalloc implementation (heap_4 supports freeing)
#   port.c           : POSIX port (1 task = 1 pthread)
#   wait_for_event.c : helper the port uses to put tasks to sleep and wake them
KERNEL_SRCS := $(KERNEL_DIR)/tasks.c \
               $(KERNEL_DIR)/queue.c \
               $(KERNEL_DIR)/list.c \
               $(KERNEL_DIR)/portable/MemMang/heap_4.c \
               $(PORT_DIR)/port.c \
               $(PORT_DIR)/utils/wait_for_event.c

APP_SRCS := src/msg_queue.c src/orbitlink_xor.c src/orbitlink_log.c src/app_main.c

# Even if the kernel path contains ../, all object files go into this folder's build/.
# (so nothing is created inside the reference FreeRTOS_시뮬레이터 folder)
APP_OBJS    := $(APP_SRCS:src/%.c=$(BUILD_DIR)/%.o)
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/kernel/,$(notdir $(KERNEL_SRCS:.c=.o)))
OBJS        := $(APP_OBJS) $(KERNEL_OBJS)

vpath %.c $(sort $(dir $(KERNEL_SRCS)))

INCLUDES := -I. -Iinclude -I$(KERNEL_DIR)/include -I$(PORT_DIR) -I$(PORT_DIR)/utils
# -MMD -MP : generate dependency files so only .c files using a changed header are rebuilt.
CFLAGS   := -std=gnu11 -g -O1 -Wall -Wextra -pthread -MMD -MP $(INCLUDES)
LDFLAGS  := -pthread

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: src/%.c FreeRTOSConfig.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel/%.o: %.c FreeRTOSConfig.h
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BUILD_DIR) $(BIN)

-include $(OBJS:.o=.d)

.PHONY: all run clean
