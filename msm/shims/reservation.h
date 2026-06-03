/*
 * 4.19 doesn't have this.
*/
static void reservation_object_describe(struct reservation_object *robj,
                                        struct seq_file *m)
{
    struct dma_fence *fence;
    struct reservation_object_list *fobj;
    unsigned int i;

    seq_puts(m, "fences:\n");

    fence = robj->fence_excl;
    if (fence)
        seq_printf(m, "\texcl: %s %llu\n",
                   fence->ops->get_driver_name(fence),
                   fence->context);

    fobj = robj->fence;
    if (fobj) {
        for (i = 0; i < fobj->shared_count; i++) {
            fence = rcu_dereference_protected(
                fobj->shared[i], 1);
            if (fence)
                seq_printf(m, "\tshared: %s %llu\n",
                           fence->ops->get_driver_name(fence),
                           fence->context);
        }
    }
}
