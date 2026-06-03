/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 */

#ifndef __MSM_DEBUGFS_H__
#define __MSM_DEBUGFS_H__

#ifdef CONFIG_DEBUG_FS
#if LINUX_VERSION_CODE > KERNEL_VERSION(5, 12, 0)
void msm_debugfs_init(struct drm_minor *minor);
#else
int msm_debugfs_init(struct drm_minor *minor);
#endif
#endif

#endif /* __MSM_DEBUGFS_H__ */
