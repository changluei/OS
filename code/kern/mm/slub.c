#include <list.h>
#include <pmm.h>
#include <slub.h>
#include <string.h>

#define SLUB_CACHE_COUNT 9
#define SLUB_ALIGNMENT 8
#define SLUB_MAX_CACHED_SIZE 2048
#define SLUB_SLAB_MAGIC 0x534c5542534c4142ULL
#define SLUB_OBJECT_FREE_MAGIC 0x534c554246524545ULL
#define SLUB_OBJECT_ALLOC_MAGIC 0x534c5542414c4c43ULL
#define SLUB_DIRECT_MAGIC 0x534c554244495245ULL

struct slub_cache;
struct slub_slab;

struct slub_object_header {
    uint64_t magic;
    void *owner;
    struct slub_object_header *next;
};

struct slub_slab {
    uint64_t magic;
    struct slub_cache *cache;
    list_entry_t list_link;
    struct slub_object_header *free_head;
    struct Page *page;
    size_t total_objects;
    size_t free_objects;
};

struct slub_cache {
    size_t object_size;
    size_t slot_size;
    list_entry_t slabs;
    size_t slab_count;
};

struct slub_direct_header {
    uint64_t magic;
    list_entry_t list_link;
    struct Page *base_page;
    size_t page_count;
};

static const size_t slub_cache_sizes[SLUB_CACHE_COUNT] = {
    8, 16, 32, 64, 128, 256, 512, 1024, 2048
};

static struct slub_cache slub_caches[SLUB_CACHE_COUNT];
static list_entry_t slub_direct_allocations;
static size_t slub_live_objects;
static size_t slub_live_slabs;
static bool slub_initialized;

static uintptr_t
slub_align_up(uintptr_t address) {
    return (address + SLUB_ALIGNMENT - 1) & ~(SLUB_ALIGNMENT - 1);
}

static void *
slub_page_address(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

static struct slub_cache *
slub_find_cache(size_t size) {
    size_t cache_index;

    for (cache_index = 0; cache_index < SLUB_CACHE_COUNT; cache_index++) {
        if (size <= slub_caches[cache_index].object_size) {
            return &slub_caches[cache_index];
        }
    }
    return NULL;
}

static struct slub_slab *
slub_create_slab(struct slub_cache *cache) {
    struct Page *page = alloc_pages(1);
    if (page == NULL) {
        return NULL;
    }

    struct slub_slab *slab = (struct slub_slab *)slub_page_address(page);
    uintptr_t object_start = slub_align_up((uintptr_t)slab + sizeof(*slab));
    size_t available = PGSIZE - (object_start - (uintptr_t)slab);
    size_t object_count = available / cache->slot_size;
    if (object_count == 0) {
        free_pages(page, 1);
        return NULL;
    }

    slab->magic = SLUB_SLAB_MAGIC;
    slab->cache = cache;
    list_init(&slab->list_link);
    slab->page = page;
    slab->total_objects = object_count;
    slab->free_objects = object_count;
    slab->free_head = NULL;

    size_t object_index;
    for (object_index = object_count; object_index > 0; object_index--) {
        uintptr_t slot_address = object_start +
                                 (object_index - 1) * cache->slot_size;
        struct slub_object_header *header =
            (struct slub_object_header *)slot_address;
        header->magic = SLUB_OBJECT_FREE_MAGIC;
        header->owner = slab;
        header->next = slab->free_head;
        slab->free_head = header;
    }

    list_add(&cache->slabs, &slab->list_link);
    cache->slab_count++;
    slub_live_slabs++;
    return slab;
}

static void
slub_release_slab(struct slub_slab *slab) {
    struct slub_cache *cache = slab->cache;

    list_del_init(&slab->list_link);
    cache->slab_count--;
    slub_live_slabs--;
    free_pages(slab->page, 1);
}

static void *
slub_alloc_cached(size_t size) {
    struct slub_cache *cache = slub_find_cache(size);
    if (cache == NULL) {
        return NULL;
    }

    struct slub_slab *slab = NULL;
    list_entry_t *entry = &cache->slabs;
    while ((entry = list_next(entry)) != &cache->slabs) {
        struct slub_slab *candidate =
            to_struct(entry, struct slub_slab, list_link);
        if (candidate->free_objects != 0) {
            slab = candidate;
            break;
        }
    }
    if (slab == NULL) {
        slab = slub_create_slab(cache);
        if (slab == NULL) {
            return NULL;
        }
    }

    struct slub_object_header *header = slab->free_head;
    assert(header != NULL);
    assert(header->magic == SLUB_OBJECT_FREE_MAGIC);
    slab->free_head = header->next;
    slab->free_objects--;
    header->magic = SLUB_OBJECT_ALLOC_MAGIC;
    header->next = NULL;
    slub_live_objects++;
    return (void *)(((struct slub_object_header *)header) + 1);
}

static void *
slub_alloc_direct(size_t size) {
    if (size > (size_t)-1 - sizeof(struct slub_direct_header) ||
        size + sizeof(struct slub_direct_header) >
            (size_t)-1 - (PGSIZE - 1)) {
        return NULL;
    }

    size_t total_bytes = size + sizeof(struct slub_direct_header);
    size_t page_count = (total_bytes + PGSIZE - 1) / PGSIZE;
    struct Page *page = alloc_pages(page_count);
    if (page == NULL) {
        return NULL;
    }

    struct slub_direct_header *header =
        (struct slub_direct_header *)slub_page_address(page);
    header->magic = SLUB_DIRECT_MAGIC;
    list_init(&header->list_link);
    header->base_page = page;
    header->page_count = page_count;
    list_add(&slub_direct_allocations, &header->list_link);
    slub_live_objects++;
    return (void *)((char *)header + sizeof(*header));
}

int
slub_init(void) {
    if (slub_initialized) {
        return SLUB_OK;
    }

    size_t cache_index;
    for (cache_index = 0; cache_index < SLUB_CACHE_COUNT; cache_index++) {
        struct slub_cache *cache = &slub_caches[cache_index];
        cache->object_size = slub_cache_sizes[cache_index];
        cache->slot_size = slub_align_up(sizeof(struct slub_object_header) +
                                         cache->object_size);
        cache->slab_count = 0;
        list_init(&cache->slabs);
    }
    list_init(&slub_direct_allocations);
    slub_live_objects = 0;
    slub_live_slabs = 0;
    slub_initialized = 1;
    return SLUB_OK;
}

void
slub_shutdown(void) {
    if (!slub_initialized) {
        return;
    }

    size_t cache_index;
    for (cache_index = 0; cache_index < SLUB_CACHE_COUNT; cache_index++) {
        struct slub_cache *cache = &slub_caches[cache_index];
        while (!list_empty(&cache->slabs)) {
            list_entry_t *entry = list_next(&cache->slabs);
            struct slub_slab *slab =
                to_struct(entry, struct slub_slab, list_link);
            list_del_init(&slab->list_link);
            cache->slab_count--;
            slub_live_slabs--;
            free_pages(slab->page, 1);
        }
    }

    while (!list_empty(&slub_direct_allocations)) {
        list_entry_t *entry = list_next(&slub_direct_allocations);
        struct slub_direct_header *header =
            to_struct(entry, struct slub_direct_header, list_link);
        list_del_init(&header->list_link);
        free_pages(header->base_page, header->page_count);
    }

    slub_live_objects = 0;
    slub_live_slabs = 0;
    slub_initialized = 0;
}

void *
slub_alloc(size_t size) {
    if (!slub_initialized || size == 0) {
        return NULL;
    }
    if (size <= SLUB_MAX_CACHED_SIZE) {
        return slub_alloc_cached(size);
    }
    return slub_alloc_direct(size);
}

int
slub_free(void *object) {
    if (object == NULL) {
        return SLUB_OK;
    }
    if (!slub_initialized) {
        return SLUB_ERR_UNINITIALIZED;
    }

    struct slub_object_header *header =
        ((struct slub_object_header *)object) - 1;
    if (header->magic == SLUB_OBJECT_FREE_MAGIC) {
        return SLUB_ERR_DOUBLE_FREE;
    }
    if (header->magic == SLUB_OBJECT_ALLOC_MAGIC && header->owner != NULL) {
        struct slub_slab *slab = (struct slub_slab *)header->owner;
        if (slab->magic != SLUB_SLAB_MAGIC || slab->cache == NULL ||
            slab->free_objects >= slab->total_objects) {
            return SLUB_ERR_INVALID;
        }

        header->magic = SLUB_OBJECT_FREE_MAGIC;
        header->next = slab->free_head;
        slab->free_head = header;
        slab->free_objects++;
        slub_live_objects--;
        if (slab->free_objects == slab->total_objects) {
            slub_release_slab(slab);
        }
        return SLUB_OK;
    }

    struct slub_direct_header *direct =
        (struct slub_direct_header *)((char *)object -
                                     sizeof(struct slub_direct_header));
    if (direct->magic != SLUB_DIRECT_MAGIC) {
        return SLUB_ERR_INVALID;
    }
    list_del_init(&direct->list_link);
    free_pages(direct->base_page, direct->page_count);
    slub_live_objects--;
    return SLUB_OK;
}

size_t
slub_nr_slabs(void) {
    return slub_live_slabs;
}

size_t
slub_nr_live_objects(void) {
    return slub_live_objects;
}
