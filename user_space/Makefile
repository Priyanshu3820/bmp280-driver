# User space BMP280 reader Makefile

CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = bmp280_reader

# The source normally lives one directory up (project root).
# If it has been copied next to this Makefile, use that copy instead.
SOURCE := $(firstword $(wildcard bmp280_reader.c) $(wildcard ../bmp280_reader.c))

all: $(TARGET)

$(TARGET): $(SOURCE)
	@if [ -z "$(SOURCE)" ]; then \
		echo "ERROR: bmp280_reader.c not found in ./ or ../"; \
		echo "Copy bmp280_reader.c here or build from the project root with 'make'."; \
		exit 1; \
	fi
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCE)

clean:
	rm -f $(TARGET)

install: $(TARGET)
	@echo "Load the kernel module with: sudo insmod driver.ko"
	@echo "Then run the user space program with: sudo ./$(TARGET)"

.PHONY: all clean install
