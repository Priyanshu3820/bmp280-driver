#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define DEVICE_FILE "/dev/bmp280"

int main(void)
{
    int fd;
    char buffer[512];
    ssize_t bytes_read;

    /* Open the device file */
    fd = open(DEVICE_FILE, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Error opening device file %s: %s\n",
                DEVICE_FILE, strerror(errno));
        return 1;
    }

    printf("BMP280 Sensor Data Reader\n");
    printf("==========================\n\n");

    while (1) {
        float temperature = 0.0f;
        int pressure = 0;
        int altitude = 0;
        char *p;

        /* Read data from device */
        bytes_read = read(fd, buffer, sizeof(buffer) - 1);
        if (bytes_read == -1) {
            fprintf(stderr, "Error reading from device: %s\n", strerror(errno));
            break;
        }

        if (bytes_read == 0) {
            printf("End of data stream\n");
            break;
        }

        /* Null-terminate the buffer */
        buffer[bytes_read] = '\0';

        /* Extract temperature */
        p = strstr(buffer, "Temperature: ");
        if (p)
            sscanf(p, "Temperature: %f", &temperature);

        /* Extract pressure */
        p = strstr(buffer, "Pressure: ");
        if (p)
            sscanf(p, "Pressure: %d", &pressure);

        /* Extract altitude */
        p = strstr(buffer, "Altitude: ");
        if (p)
            sscanf(p, "Altitude: %d", &altitude);

        /* Display the readings separately */
        printf("\n");
        printf("Temperature: %.2f C\n", temperature);
        printf("Pressure: %d Pa\n", pressure);
        printf("Altitude: %d meters\n", altitude);

        /* The driver signals EOF after a single read; rewind so the next
         * read returns fresh data instead of 0 (EOF). */
        if (lseek(fd, 0, SEEK_SET) == -1) {
            fprintf(stderr, "Error rewinding device: %s\n", strerror(errno));
            break;
        }

        /* Clear buffer for next read */
        memset(buffer, 0, sizeof(buffer));

        /* Wait a bit before next read */
        usleep(1000000); /* 1 second */

        printf("\n--- Press Ctrl+C to exit ---\n");
    }

    /* Close the device file */
    close(fd);

    return 0;
}
