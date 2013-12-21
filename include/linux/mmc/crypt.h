/*
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef LINUX_MMC_CRYPT_H
#define LINUX_MMC_CRYPT_H

#include <linux/mmc/core.h>
#include <linux/types.h>

enum mmc_disk_encryption_state {
	MMC_NO_ENCRYPTION = 0,
	MMC_DISK_ENCRYPTION_IN_PROGRESS,
	MMC_DISK_ENCRYPTED,
	MMC_MAX_DISK_ENCRYPTION_STATE
};

extern void mmc_encrypt_req(struct mmc_host*, struct mmc_request*);

extern void mmc_decrypt_req(struct mmc_host*, struct mmc_request*);

extern bool mmc_should_encrypt(const struct mmc_host*,
						const struct mmc_request*);

extern bool mmc_should_decrypt(const struct mmc_host*,
						const struct mmc_request*);

extern bool mmc_should_encrypt_decrypt(const struct mmc_host*,
						const struct request*);

extern void mmc_init_crypto_buffers(void);

extern void mmc_release_crypto_buffers(void);

#endif /* LINUX_MMC_CRYPT_H */
