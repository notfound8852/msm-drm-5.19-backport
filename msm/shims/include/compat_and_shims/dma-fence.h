struct dma_fence *dma_fence_get_stub(void);
struct dma_fence *dma_fence_allocate_private_stub(void);
extern const struct dma_fence_ops dma_fence_chain_ops;
/**
 * dma_fence_is_chain - check if a fence is from the chain subclass
 * @fence: the fence to test
 *
 * Return true if it is a dma_fence_chain and false otherwise.
 */
static inline bool dma_fence_is_chain(struct dma_fence *fence)
{
    return fence->ops == &dma_fence_chain_ops;
}
