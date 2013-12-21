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

#include <linux/module.h>
#include <linux/mmc/crypt.h>

void mmc_init_crypto_buffers(void)
{
}

void mmc_release_crypto_buffers(void)
{
}

bool
mmc_should_encrypt(const struct mmc_host *host, const struct mmc_request *mrq)
{
	return false;
}

bool
mmc_should_decrypt(const struct mmc_host *host, const struct mmc_request *mrq)
{
	return false;
}

bool mmc_should_encrypt_decrypt(const struct mmc_host *host,
						const struct request *req)
{
	return false;
}

void mmc_encrypt_req(struct mmc_host *host, struct mmc_request *mrq)
{
}

void mmc_decrypt_req(struct mmc_host *host, struct mmc_request *mrq)
{
}

