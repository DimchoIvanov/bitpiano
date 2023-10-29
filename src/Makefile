#all: dplay.c
#	cc dplay.c wav_parser.c -lasound -o dplay
#
#clean:
#	rm -f dplay

TARGET:=bitpiano
SRCS = bitpiano.c
#OBJS := $(SRCS:.c=.o)
OBJS = $(addsuffix .o,$(basename $(SRCS)))
LDFLAGS+=-lasound -lm

%.o : %.c
	$(CC) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

.PHONY: all

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGET) $(OBJS)
