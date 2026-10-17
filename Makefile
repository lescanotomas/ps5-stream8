ifdef PS5_PAYLOAD_SDK
include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
$(error PS5_PAYLOAD_SDK is undefined)
endif

ELF := payloads/stream8-v0.3.0-audio-1080p60-30mbps-fw1360.elf
SRC := src/stream8_server.c
CFLAGS := -O2 -Wall -Werror \
	-DSTREAM8_RESOLUTION_INDEX=3 \
	-DSTREAM8_FPS=60 \
	-DSTREAM8_OPEN_BITRATE_BPS=30000000U \
	-DSTREAM8_OPEN_VBV_BPS=36000000U \
	-DSTREAM8_RUNTIME_BITRATE_BPS=30000000U \
	-DSTREAM8_RUNTIME_VBV_BPS=30000000U \
	-DSTREAM8_NATIVE_BUFFER_UPDATE=1 \
	-DSTREAM8_GAME_LIVE_RATE_BUDGET=1

.PHONY: all clean

all: $(ELF)

$(ELF): $(SRC)
	@mkdir -p payloads
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(ELF)
