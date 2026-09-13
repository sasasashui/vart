#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vart/memory.h"

#define TEST_GUEST_ADDR UINT64_C(0x80000000)

static int expect_error(const char *name, uint64_t guest_addr, size_t size)
{
    VartMemoryRegion region;
    int ret;

    ret = vart_memory_region_create(&region, guest_addr, size, 0);
    if (ret == -EINVAL) {
        return 0;
    }
    if (ret == 0) {
        vart_memory_region_destroy(&region);
    }
    fprintf(stderr, "not ok - %s returned %d, expected %d\n",
            name, ret, -EINVAL);
    return -1;
}

int main(void)
{
    VartMemoryRegion region;
    const unsigned char input[] = { 0x12, 0x34, 0x56, 0x78 };
    long page_size;
    unsigned char *bytes;
    int ret;

    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        fprintf(stderr, "not ok - cannot determine host page size\n");
        return EXIT_FAILURE;
    }

    if (expect_error("zero size", TEST_GUEST_ADDR, 0) < 0 ||
        expect_error("unaligned address", TEST_GUEST_ADDR + 1,
                     (size_t)page_size) < 0 ||
        expect_error("unaligned size", TEST_GUEST_ADDR,
                     (size_t)page_size - 1) < 0) {
        return EXIT_FAILURE;
    }

    ret = vart_memory_region_create(&region, TEST_GUEST_ADDR,
                                    (size_t)page_size, 7);
    if (ret < 0) {
        fprintf(stderr, "not ok - valid region returned %d\n", ret);
        return EXIT_FAILURE;
    }

    bytes = region.host_addr;
    if (bytes[0] != 0 || bytes[page_size - 1] != 0 ||
        region.guest_addr != TEST_GUEST_ADDR ||
        region.size != (size_t)page_size || region.slot != 7 ||
        region.registered) {
        fprintf(stderr, "not ok - valid region state is incorrect\n");
        vart_memory_region_destroy(&region);
        return EXIT_FAILURE;
    }

    ret = vart_memory_region_write(&region, TEST_GUEST_ADDR + 16,
                                   input, sizeof(input));
    if (ret < 0 || memcmp(bytes + 16, input, sizeof(input)) != 0) {
        fprintf(stderr, "not ok - valid guest memory write failed\n");
        vart_memory_region_destroy(&region);
        return EXIT_FAILURE;
    }

    if (vart_memory_region_write(&region, TEST_GUEST_ADDR - 1,
                                 input, sizeof(input)) != -ERANGE ||
        vart_memory_region_write(&region,
                                 TEST_GUEST_ADDR + (uint64_t)page_size - 1,
                                 input, sizeof(input)) != -ERANGE ||
        vart_memory_region_write(&region, UINT64_MAX - 1,
                                 input, sizeof(input)) != -ERANGE ||
        vart_memory_region_write(&region, TEST_GUEST_ADDR,
                                 NULL, sizeof(input)) != -EINVAL ||
        vart_memory_region_write(&region,
                                 TEST_GUEST_ADDR + (uint64_t)page_size,
                                 NULL, 0) != 0) {
        fprintf(stderr, "not ok - guest memory write validation failed\n");
        vart_memory_region_destroy(&region);
        return EXIT_FAILURE;
    }

    vart_memory_region_destroy(&region);
    printf("ok - validate and write guest memory regions\n");
    return EXIT_SUCCESS;
}
