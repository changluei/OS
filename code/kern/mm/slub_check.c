#include <assert.h>
#include <pmm.h>
#include <slub.h>
#include <stdio.h>
#include <string.h>

static void
slub_check_distinct(void **objects, size_t count) {
    size_t first_index;
    for (first_index = 0; first_index < count; first_index++) {
        size_t second_index;
        for (second_index = first_index + 1; second_index < count;
             second_index++) {
            assert(objects[first_index] != objects[second_index]);
        }
    }
}

static void
slub_check_bytes(void *object, size_t size, uint8_t value) {
    uint8_t *bytes = (uint8_t *)object;
    size_t byte_index;
    for (byte_index = 0; byte_index < size; byte_index++) {
        assert(bytes[byte_index] == value);
    }
}

int
slub_check(void) {
    size_t free_before = nr_free_pages();
    assert(slub_init() == SLUB_OK);
    assert(slub_alloc(0) == NULL);
    assert(slub_free(NULL) == SLUB_OK);

    void *objects[16];
    size_t object_sizes[16] = {
        1, 7, 8, 15, 24, 63, 64, 127,
        128, 255, 256, 511, 512, 1024, 2048, 2049
    };
    size_t object_index;
    for (object_index = 0; object_index < 16; object_index++) {
        objects[object_index] = slub_alloc(object_sizes[object_index]);
        assert(objects[object_index] != NULL);
        memset(objects[object_index], (char)(object_index + 1),
               object_sizes[object_index]);
        slub_check_bytes(objects[object_index], object_sizes[object_index],
                         (uint8_t)(object_index + 1));
    }
    slub_check_distinct(objects, 16);
    assert(slub_nr_live_objects() == 16);

    for (object_index = 0; object_index < 16; object_index += 2) {
        assert(slub_free(objects[object_index]) == SLUB_OK);
    }
    assert(slub_nr_live_objects() == 8);
    for (object_index = 1; object_index < 16; object_index += 2) {
        assert(slub_free(objects[object_index]) == SLUB_OK);
    }
    assert(slub_nr_live_objects() == 0);
    assert(slub_nr_slabs() == 0);

    void *reuse_objects[32];
    for (object_index = 0; object_index < 32; object_index++) {
        reuse_objects[object_index] = slub_alloc(24);
        assert(reuse_objects[object_index] != NULL);
    }
    for (object_index = 0; object_index < 16; object_index++) {
        assert(slub_free(reuse_objects[object_index]) == SLUB_OK);
    }
    for (object_index = 0; object_index < 16; object_index++) {
        reuse_objects[object_index] = slub_alloc(24);
        assert(reuse_objects[object_index] != NULL);
    }
    for (object_index = 0; object_index < 32; object_index++) {
        assert(slub_free(reuse_objects[object_index]) == SLUB_OK);
    }
    assert(slub_nr_live_objects() == 0);
    assert(slub_nr_slabs() == 0);

    void *double_free_object = slub_alloc(32);
    void *live_object = slub_alloc(32);
    assert(double_free_object != NULL && live_object != NULL);
    assert(slub_free(double_free_object) == SLUB_OK);
    assert(slub_free(double_free_object) == SLUB_ERR_DOUBLE_FREE);
    assert(slub_free(live_object) == SLUB_OK);

    void *large_object = slub_alloc(PGSIZE * 2 + 17);
    assert(large_object != NULL);
    memset(large_object, 0xa5, PGSIZE * 2 + 17);
    slub_check_bytes(large_object, PGSIZE * 2 + 17, 0xa5);
    assert(slub_free(large_object) == SLUB_OK);

    assert(slub_nr_live_objects() == 0);
    assert(slub_nr_slabs() == 0);

    assert(slub_alloc(80) != NULL);
    assert(slub_alloc(PGSIZE + 1) != NULL);
    assert(slub_nr_live_objects() == 2);
    slub_shutdown();
    assert(slub_nr_live_objects() == 0);
    assert(slub_nr_slabs() == 0);
    assert(slub_alloc(16) == NULL);
    assert(slub_init() == SLUB_OK);
    assert(nr_free_pages() == free_before);
    cprintf("slub_check() succeeded!\n");
    return SLUB_OK;
}
