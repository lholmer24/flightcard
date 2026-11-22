# Host unit tests for the portable Flightcard core.
ROOT := ..
CC ?= gcc
CFLAGS := -std=c11 -D_GNU_SOURCE -DFC_HOST_TEST -O2 -g \
  -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation \
  -I$(ROOT)/include -I$(ROOT)/vendor -I$(ROOT)/assets/generated
LDLIBS := -lm
GEN := $(ROOT)/assets/generated

SRC := test_main.c \
  $(ROOT)/src/timezone.c $(ROOT)/src/format.c $(ROOT)/src/ranking.c \
  $(ROOT)/src/types.c $(ROOT)/src/json.c $(ROOT)/src/http.c \
  $(ROOT)/src/dns_proto.c $(ROOT)/src/ntp.c $(ROOT)/src/clock.c \
  $(ROOT)/src/config_store.c $(ROOT)/src/cli.c $(ROOT)/src/light.c \
  $(ROOT)/src/demo.c $(ROOT)/src/sprite.c $(ROOT)/src/log.c \
  $(ROOT)/assets/generated/fc_types.c $(ROOT)/assets/generated/fc_sprites.c

all: test

assets:
	python3 $(ROOT)/tools/convert_assets.py

$(GEN)/fc_types.c $(GEN)/fc_sprites.c:
	python3 $(ROOT)/tools/convert_assets.py

test: $(SRC) $(GEN)/fc_types.c $(GEN)/fc_sprites.c
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

run: test
	./test

clean:
	rm -f test

.PHONY: all assets run clean
