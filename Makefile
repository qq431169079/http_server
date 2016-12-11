CC = gcc
TARGET = http_server
CFLAGS = -Os -std=c11
SRC = src/http_server.c
OUT_DIR = out/
MKDIR = mkdir -p
RM = rm -rf

.PHONY:		all clean

all:		$(TARGET)

clean:
		@$(RM) $(OUT_DIR)

$(TARGET):	$(SRC)
		@$(MKDIR) $(OUT_DIR)
		@$(CC) $(CFLAGS) $< -o $(OUT_DIR)$@
