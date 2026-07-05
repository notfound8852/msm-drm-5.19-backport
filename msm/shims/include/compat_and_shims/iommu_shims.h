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
#endif
#ifndef iommu_flush_iotlb_all
static inline void iommu_flush_iotlb_all(struct iommu_domain *domain) {
	if (domain->ops->flush_iotlb_all)
		domain->ops->flush_iotlb_all(domain);
}
#endif
#ifndef iommu_map_sgtable
static inline ssize_t iommu_map_sgtable(struct iommu_domain *domain,
                                        unsigned long iova,
                                        struct sg_table *sgt, int prot)
{
    return iommu_map_sg(domain, iova, sgt->sgl, sgt->orig_nents, prot);
}
#endif

#ifndef for_each_sgtable_sg
#define for_each_sgtable_sg(sgt, sg, i) \
	for_each_sg((sgt)->sgl, sg, (sgt)->orig_nents, i)
#endif
