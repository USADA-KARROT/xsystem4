/* Exercise the real allocator/free list, including repeated last-owner drops. */
static void test_heap_reuse(void)
{
    const size_t live = live_slots(), capacity = heap_size;
    for (int i = 0; i < 1000; i++) {
        int a = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0));
        uint32_t generation = heap[a].seq;
        heap_ref(a); heap_unref(a);
        assert(HEAP_REF(a) == 1);
        heap_unref(a);
        assert(HEAP_REF(a) == 0);
        heap_unref(a); /* no duplicate free-list insertion */
        int b = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0));
        assert(b == a && heap[b].seq != generation && HEAP_REF(b) == 1);
        int c = heap_alloc_page(alloc_page(ARRAY_PAGE, AIN_ARRAY_INT, 0));
        assert(c != b && HEAP_REF(b) == 1 && HEAP_REF(c) == 1);
        heap_unref(c); heap_unref(b);
    }
    int child = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
    struct page *array = alloc_page(ARRAY_PAGE, AIN_ARRAY_STRUCT, 1);
    array->array.rank = 1;
    array->values[0].i = child;
    int owner = heap_alloc_page(array);
    exit_unref(owner); /* destructor-free cleanup must recycle pages too */
    assert(HEAP_REF(child) == 0 && HEAP_REF(owner) == 0);
    int reuse = heap_alloc_page(alloc_page(DELEGATE_PAGE, 0, 0));
    assert(reuse == owner);
    heap_unref(reuse);
    assert(live_slots() == live && heap_size == capacity);
    puts("HEAP PASS 1000 page-slot reuse/generation/duplicate-unref rounds; exit_unref recycling; stable capacity and live=0");
}
