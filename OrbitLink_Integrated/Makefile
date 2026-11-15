# OrbitLink 통합본(송수신 + XOR + Logger) 빌드 (macOS / Linux)
#   make        빌드
#   make run    빌드 후 실행 (자동 판정, 통과하면 종료 코드 0)
#   make clean  build 폴더와 실행 파일 삭제
# 다른 컴파일러를 쓰려면: make CC=gcc

CC        := clang
BIN       := orbitlink_integrated
BUILD_DIR := build

# 커널 위치를 고른다.
#   1순위: 이 폴더 안의 FreeRTOS-Kernel (팀원이 직접 받은 경우, README 참고)
#   2순위: 옆 폴더 ../FreeRTOS_시뮬레이터/FreeRTOS-Kernel (경연님 PC)
#   직접 지정: make KERNEL_DIR=/어딘가/FreeRTOS-Kernel
ifneq ($(wildcard FreeRTOS-Kernel/tasks.c),)
KERNEL_DIR := FreeRTOS-Kernel
else
KERNEL_DIR := ../FreeRTOS_시뮬레이터/FreeRTOS-Kernel
endif

# 커널 소스가 없으면 컴파일 오류 대신 알아보기 쉬운 메시지를 내고 멈춘다.
ifeq ($(wildcard $(KERNEL_DIR)/tasks.c),)
$(error FreeRTOS-Kernel 을 찾을 수 없습니다. README.md 의 '커널 받기' 절을 따라 받으세요)
endif

PORT_DIR := $(KERNEL_DIR)/portable/ThirdParty/GCC/Posix

# 커널에서 필요한 파일만 빌드한다.
#   heap_4.c         : pvPortMalloc 구현 (시나리오마다 만들고 지우므로 해제가 되는 heap_4)
#   port.c           : POSIX 포트 (태스크 1개 = pthread 1개)
#   wait_for_event.c : 포트가 태스크를 재우고 깨울 때 쓰는 유틸
KERNEL_SRCS := $(KERNEL_DIR)/tasks.c \
               $(KERNEL_DIR)/queue.c \
               $(KERNEL_DIR)/list.c \
               $(KERNEL_DIR)/portable/MemMang/heap_4.c \
               $(PORT_DIR)/port.c \
               $(PORT_DIR)/utils/wait_for_event.c

APP_SRCS := src/msg_queue.c src/orbitlink_xor.c src/orbitlink_log.c src/app_main.c

# 커널 경로에 ../ 가 들어가도 오브젝트 파일은 전부 이 폴더의 build/ 안에만 만든다.
# (그래야 참고용 FreeRTOS_시뮬레이터 폴더에 아무것도 생기지 않는다)
APP_OBJS    := $(APP_SRCS:src/%.c=$(BUILD_DIR)/%.o)
KERNEL_OBJS := $(addprefix $(BUILD_DIR)/kernel/,$(notdir $(KERNEL_SRCS:.c=.o)))
OBJS        := $(APP_OBJS) $(KERNEL_OBJS)

vpath %.c $(sort $(dir $(KERNEL_SRCS)))

INCLUDES := -I. -Iinclude -I$(KERNEL_DIR)/include -I$(PORT_DIR) -I$(PORT_DIR)/utils
# -MMD -MP : 헤더가 바뀌면 그 헤더를 쓰는 .c 만 다시 컴파일하도록 의존성 파일을 만든다.
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
