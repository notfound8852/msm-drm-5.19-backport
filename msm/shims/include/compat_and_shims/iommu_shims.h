#ifndef IO_PGTABLE_QUIRK_ARM_TTBR1
#define IO_PGTABLE_QUIRK_ARM_TTBR1 BIT(29) // avoid collision
#endif
#ifndef IO_PGTABLE_QUIRK_ARM_OUTER_WBWA
#define IO_PGTABLE_QUIRK_ARM_OUTER_WBWA BIT(21) // this just exists for the sake of existing.
#endif

#ifndef IO_PGTABLE_QUIRKS
#define IO_PGTABLE_QUIRKS
#endif

#ifndef iommu_set_pgtable_quirks
static inline int iommu_set_pgtable_quirks(struct iommu_domain *domain,
					   unsigned long quirks)
{
	int ret = 0;

	if (domain->type != IOMMU_DOMAIN_UNMANAGED)
		return -EINVAL;

	if (!(quirks & IO_PGTABLE_QUIRK_ARM_OUTER_WBWA))
		return ret;

#ifdef DOMAIN_ATTR_PAGE_TABLE_IS_COHERENT
	/* Make the page tables coherent (Snoop System Cache/LLC) */
	int coherent = 1;
	ret = iommu_domain_set_attr(domain, DOMAIN_ATTR_PAGE_TABLE_IS_COHERENT, &coherent);
	if (ret) {
		pr_err("%s: Failed to make io-pgtables coherent, ret=%d\n",
				__func__, ret);
		return ret;
	}
#else
	pr_warn_once("DOMAIN_ATTR_PAGE_TABLE_IS_COHERENT is not available!");
#endif

#ifdef DOMAIN_ATTR_USE_LLC_NWA
	/* Set to 0 to prefer standard WBWA if available and
	 * map allocation policies for Last Level Cache(LLC)
	 */
	int use_llc_nwa = 0;
	ret = iommu_domain_set_attr(domain, DOMAIN_ATTR_USE_LLC_NWA, &use_llc_nwa);
	if (ret) {
		pr_err("%s: Failed to set LLC quirk, ret=%d\n",
				__func__, ret);
		return ret;
	}
#else
	pr_warn_once("DOMAIN_ATTR_USE_LLC_NWA is not available!");
#endif

	return ret;
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
