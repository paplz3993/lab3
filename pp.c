#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <stdint.h>
#include "bmp_utility.h"

#define HW_REGS_BASE (0xff200000)
#define HW_REGS_SPAN (0x00200000)
#define HW_REGS_MASK (HW_REGS_SPAN - 1)
#define LED_BASE     (0x1000)
#define PUSH_BASE    (0x3010)
#define VIDEO_BASE   (0x0000)

#define FPGA_ONCHIP_BASE (0xC8000000)
#define IMAGE_WIDTH      (320)
#define IMAGE_HEIGHT     (240)
#define IMAGE_SPAN       (IMAGE_WIDTH * IMAGE_HEIGHT * 2) // 2 bytes per pixel (RGB565)
#define IMAGE_MASK       (IMAGE_SPAN - 1)
#define ROW_STRIDE       (512) // DMA stride in pixels

int main(void) {
    volatile unsigned int *video_in_dma = NULL;
    volatile unsigned int *key_ptr = NULL;
    volatile unsigned short *video_mem = NULL;
    void *hw_virtual_base;
    void *video_virtual_base;
    int fd;

    // Open /dev/mem
    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
        printf("ERROR: could not open \"/dev/mem\"...\n");
        return 1;
    }

    // Map hardware registers (for DMA and keys)
    hw_virtual_base = mmap(NULL, HW_REGS_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, HW_REGS_BASE);
    if (hw_virtual_base == MAP_FAILED) {
        printf("ERROR: mmap() failed for HW_REGS_BASE...\n");
        close(fd);
        return 1;
    }

    // Map video memory (for display)
    video_virtual_base = mmap(NULL, IMAGE_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, FPGA_ONCHIP_BASE);
    if (video_virtual_base == MAP_FAILED) {
        printf("ERROR: mmap() failed for FPGA_ONCHIP_BASE...\n");
        munmap(hw_virtual_base, HW_REGS_SPAN);
        close(fd);
        return 1;
    }

    // Get pointers to registers
    video_in_dma = (unsigned int *)(hw_virtual_base + (VIDEO_BASE & HW_REGS_MASK));
    key_ptr = (unsigned int *)(hw_virtual_base + (PUSH_BASE & HW_REGS_MASK));
    video_mem = (unsigned short *)video_virtual_base;

    // Enable video DMA
    *(video_in_dma + 3) = 0x4; // Start DMA
    printf("Video DMA enabled: 0x%x\n", *(video_in_dma + 3));

    // Buffer for saving image
    unsigned short pixels[IMAGE_WIDTH][IMAGE_HEIGHT];
    const char *filename_color = "final_image_color.bmp";
    const char *filename_bw = "final_image_bw.bmp";

    // Main loop for video streaming
    while (1) {
        // Copy DMA buffer to video memory (assuming DMA writes to a buffer at video_in_dma)
        // Note: Adjust if DMA can write directly to FPGA_ONCHIP_BASE
        for (int y = 0; y < IMAGE_HEIGHT; y++) {
            for (int x = 0; x < IMAGE_WIDTH; x++) {
                // Read pixel from DMA buffer (adjust offset if needed)
                video_mem[y * IMAGE_WIDTH + x] = *(unsigned short *)((unsigned char *)(video_in_dma) + (y * ROW_STRIDE * 2) + (x * 2));
            }
        }

        // Check for key press (KEY1, KEY2, KEY3)
        unsigned int key_value = *key_ptr & 0xE; // Mask for KEY1 (0x2), KEY2 (0x4), KEY3 (0x8)
        if (key_value) {
            printf("Key pressed: 0x%x\n", key_value);

            // Copy current frame to pixels array
            for (int y = 0; y < IMAGE_HEIGHT; y++) {
                for (int x = 0; x < IMAGE_WIDTH; x++) {
                    pixels[x][y] = video_mem[y * IMAGE_WIDTH + x];
                }
            }

            // Save color image
            saveImageShort(filename_color, &pixels[0][0], IMAGE_WIDTH, IMAGE_HEIGHT);

            // Save grayscale image (uncomment if implemented)
            // saveImageGrayscale(filename_bw, &pixels[0][0], IMAGE_WIDTH, IMAGE_HEIGHT);

            // Wait for key release
            while (*key_ptr & 0xE) {
                usleep(10000); // 10ms delay
            }
        }

        usleep(16666); // ~60 FPS (1/60s = 16666us)
    }

    // Cleanup (unreachable in this loop, but included for completeness)
    if (munmap(hw_virtual_base, HW_REGS_SPAN) != 0) {
        printf("ERROR: munmap() failed for HW_REGS_BASE...\n");
    }
    if (munmap(video_virtual_base, IMAGE_SPAN) != 0) {
        printf("ERROR: munmap() failed for FPGA_ONCHIP_BASE...\n");
    }
    close(fd);
    return 0;
}
