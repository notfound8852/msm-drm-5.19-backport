#ifndef IO_PGTABLE_QUIRK_ARM_TTBR1
#define IO_PGTABLE_QUIRK_ARM_TTBR1 0
#endif
#ifndef IO_PGTABLE_QUIRK_ARM_OUTER_WBWA
#define IO_PGTABLE_QUIRK_ARM_OUTER_WBWA 0
#endif

#ifndef IO_PGTABLE_QUIRKS
#define IO_PGTABLE_QUIRKS
static inline int iommu_set_pgtable_quirks(struct iommu_domain *domain, unsigned long quirks) {
    return 0;
}

static inline void iommu_flush_iotlb_all(struct iommu_domain *domain) {}
#endif
