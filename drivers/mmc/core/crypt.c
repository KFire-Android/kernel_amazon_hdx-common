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
#include <linux/crypto.h>
#include <linux/highmem.h>
#include <linux/blkdev.h>
#include <linux/mm.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/pagemap.h>
#include <crypto/scatterwalk.h>
#include <mach/qcrypto.h>

#include <linux/mmc/crypt.h>

#define MAX_ENCRYPTION_BUFFERS 2
#define SECTOR_SIZE 512
#define MMC_512_KB (512*1024)
/*
 * Maximum buffer size for crypto operation is 512KB
 * Each sg entry can point to a minimum of 4K virtual data
 * It means that we require max of 512k/4k = 128 sg entries
 */
#define MAX_SCATTER_LIST_ENTRIES 128

u8	*mmc_crypto_bufs[MAX_ENCRYPTION_BUFFERS];
struct scatterlist	*mmc_crypto_out_sg[MAX_ENCRYPTION_BUFFERS];
u8	mmc_crypto_buf_idx;

/* In future we may change this to accomodate multiple partitions encryption */
static sector_t mmc_start_sec, mmc_end_sec;
static bool mmc_crypto_info_parsed;

static bool
mmc_sector_in_crypto_range(struct mmc_card *card, sector_t sector,
							unsigned int length)
{
	bool ret = false;
	if (!mmc_crypto_info_parsed) {
		/* Verify that both parameters were received correctly */
		if (sscanf(card->crypto_info, "%lu - %lu",
					(unsigned long *)&mmc_start_sec,
					(unsigned long *)&mmc_end_sec) != 2)
			goto exit;
		pr_debug("Start sec = %lu End sec = %lu\n",
					(unsigned long)mmc_start_sec,
					(unsigned long)mmc_end_sec);
		mmc_crypto_info_parsed = true;
	}

	ret =  ((sector >= mmc_start_sec) && (sector < mmc_end_sec) &&
					((sector + length) < mmc_end_sec));
exit:
	return ret;
}

struct mmc_tcrypt_result {
	struct completion completion;
	int err;
};

void mmc_init_crypto_buffers(void)
{
	unsigned int buf_index;

	/* Setup encryption parameters */
	mmc_crypto_buf_idx = MAX_ENCRYPTION_BUFFERS;
	for (buf_index = 0; buf_index < MAX_ENCRYPTION_BUFFERS; buf_index++) {
		mmc_crypto_bufs[buf_index] = kzalloc(MMC_512_KB, GFP_KERNEL);
		if (!mmc_crypto_bufs[buf_index]) {
			pr_err("%s encryption buffer[%d] allocation failed\n",
							__func__, buf_index);
			break;
		}
		mmc_crypto_out_sg[buf_index] = kzalloc(
			sizeof(struct scatterlist) * MAX_SCATTER_LIST_ENTRIES,
			GFP_KERNEL);
		if (!mmc_crypto_out_sg[buf_index]) {
			kfree(mmc_crypto_bufs[buf_index]);
			mmc_crypto_bufs[buf_index] = NULL;
			pr_err("%s out_sg[%d] allocation failed\n",
							__func__, buf_index);
			break;
		}
		mmc_crypto_buf_idx--;
	}

	if (buf_index != MAX_ENCRYPTION_BUFFERS) {
		mmc_crypto_buf_idx = MAX_ENCRYPTION_BUFFERS;
		for ( ; buf_index; buf_index--) {
			kfree(mmc_crypto_bufs[buf_index-1]);
			mmc_crypto_bufs[buf_index-1] = NULL;
			kfree(mmc_crypto_out_sg[buf_index-1]);
			mmc_crypto_out_sg[buf_index-1] = NULL;
		}
	}
}

void mmc_release_crypto_buffers(void)
{
	unsigned int buf_index;
	for (buf_index = 0; buf_index < MAX_ENCRYPTION_BUFFERS; buf_index++) {
		kfree(mmc_crypto_bufs[buf_index]);
		kfree(mmc_crypto_out_sg[buf_index]);
	}

	mmc_crypto_buf_idx = MAX_ENCRYPTION_BUFFERS;
}

bool
mmc_should_encrypt(const struct mmc_host *host, const struct mmc_request *mrq)
{
	bool ret = false;

	if (!host || !mrq)
		goto out;

	if ((host->card->encryption_state == MMC_DISK_ENCRYPTION_IN_PROGRESS) ||
		(host->card->encryption_state == MMC_DISK_ENCRYPTED)) {
		if (mmc_sector_in_crypto_range(host->card, mrq->data->sector,
			(mrq->data->blksz * mrq->data->blocks)/SECTOR_SIZE)) {
			/*
			 * mmc_crypto_buf_idx would be MAX_ENCRYPTION_BUFFERS
			 * only if memory allocation for encryption has failed
			 */
			if (mmc_crypto_buf_idx == MAX_ENCRYPTION_BUFFERS)
				goto out;

			if ((mrq->cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK) ||
				(mrq->cmd->opcode == MMC_WRITE_BLOCK))
				ret = true;
		}
	}
out:
	return ret;
}

bool
mmc_should_decrypt(const struct mmc_host *host, const struct mmc_request *mrq)
{
	bool ret = false;

	if (!host || !mrq)
		goto out;

	if (host->card->encryption_state == MMC_DISK_ENCRYPTED) {
		if (mmc_sector_in_crypto_range(host->card, mrq->data->sector,
			(mrq->data->blksz * mrq->data->blocks)/SECTOR_SIZE)) {
			if ((mrq->cmd->opcode == MMC_READ_MULTIPLE_BLOCK) ||
				(mrq->cmd->opcode == MMC_READ_SINGLE_BLOCK))
				ret = true;
		}
	}
out:
	return ret;
}

bool mmc_should_encrypt_decrypt(const struct mmc_host *host,
						const struct request *req)
{
	bool ret = false;

	if (host && req &&
		((host->card->encryption_state > MMC_NO_ENCRYPTION) &&
		 (host->card->encryption_state < MMC_MAX_DISK_ENCRYPTION_STATE))
		&& mmc_sector_in_crypto_range(host->card, req->__sector,
							blk_rq_sectors(req)))
		ret = true;

	return ret;
}

static
void mmc_crypto_cipher_complete(struct crypto_async_request *req, int err)
{
	struct mmc_tcrypt_result *res = req->data;

	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
}

#define MMC_KEY_SIZE_XTS 32
#define MMC_AES_XTS_IV_LEN 16

static int mmc_count_sg(struct scatterlist *sg, unsigned long nbytes)
{
	int i;

	for (i = 0; nbytes > 0 && sg; i++, sg = scatterwalk_sg_next(sg))
		nbytes -= sg->length;
	return i;
}

static int mmc_copy_sglist(struct scatterlist *in_sg, int entries,
				struct scatterlist *out_sg, u8 *buf)
{
	/* initialize out_sg with number of entries present in the in_sg
	 * iterate over in_sg to get the length of each entry and copy
	 * the same amount of buffer into out_sg
	 */
	int i = 0;

	if (out_sg && (entries > 0))
		sg_init_table(out_sg, entries);
	else {
		pr_err("Either in_sg is empty or out_sg is NULL\n");
		goto exit;
	}

	while (in_sg && entries > 0) {
		if (&out_sg[i]) {
			sg_set_buf(&out_sg[i], buf, in_sg->length);
			buf += in_sg->length;
			i++;
			in_sg = scatterwalk_sg_next(in_sg);
			entries--;
		} else {
			pr_err("in_sg is bigger than out_sg\n");
			i = 0;
			goto exit;
		}
	}
exit:
	return i;
}

void mmc_encrypt_req(struct mmc_host *host, struct mmc_request *mrq)
{
	struct crypto_ablkcipher *tfm;
	struct ablkcipher_request *req;
	struct mmc_tcrypt_result result;
	struct scatterlist *in_sg = mrq->data->sg;
	struct scatterlist *out_sg = NULL;
	u8 *dst_data = NULL;
	unsigned long data_len = 0;
	uint32_t bytes = 0;
	int rc = 0;
	u8 IV[MMC_AES_XTS_IV_LEN];
	sector_t sector = mrq->data->sector;

	tfm = crypto_alloc_ablkcipher("xts(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("%s:%s ablkcipher tfm allocation failed : error = %lu\n",
				mmc_hostname(host), __func__, PTR_ERR(tfm));
		return;
	}

	req = ablkcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s:%s ablkcipher request allocation failed\n",
				mmc_hostname(host), __func__);
		goto ablkcipher_req_alloc_failure;
	}

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					mmc_crypto_cipher_complete, &result);

	init_completion(&result.completion);
	qcrypto_cipher_set_flag(req,
		QCRYPTO_CTX_USE_PIPE_KEY | QCRYPTO_CTX_XTS_DU_SIZE_512B);
	crypto_ablkcipher_clear_flags(tfm, ~0);
	crypto_ablkcipher_setkey(tfm, NULL, MMC_KEY_SIZE_XTS);

	data_len = mrq->data->blksz * mrq->data->blocks;
	if (data_len > MMC_512_KB) {
		pr_err("%s:%s Encryption operation aborted: req size > 512K\n",
				mmc_hostname(host), __func__);
		goto crypto_operation_failure;
	}

	if (mmc_crypto_buf_idx != MAX_ENCRYPTION_BUFFERS) {
		dst_data = mmc_crypto_bufs[mmc_crypto_buf_idx];
		memset(dst_data, 0, MMC_512_KB);
		out_sg = mmc_crypto_out_sg[mmc_crypto_buf_idx];
		memset(out_sg, 0, sizeof(struct scatterlist) *
					MAX_SCATTER_LIST_ENTRIES);
		mmc_crypto_buf_idx = 1-mmc_crypto_buf_idx;
	} else {
		pr_err("%s:%s encryption buffers not available\n",
				mmc_hostname(host), __func__);
		goto crypto_operation_failure;
	}

	bytes = sg_copy_to_buffer(in_sg, mrq->data->sg_len, dst_data, data_len);
	if (bytes != data_len) {
		pr_err("%s:%s error in copying data from sglist to buffer\n",
				 mmc_hostname(host), __func__);
		goto crypto_operation_failure;
	}

	if (!mmc_copy_sglist(in_sg, mrq->data->sg_len, out_sg, dst_data)) {
		pr_err("%s:%s could not create dst sglist from in sglist\n",
				mmc_hostname(host), __func__);
		goto crypto_operation_failure;
	}

	memset(IV, 0, MMC_AES_XTS_IV_LEN);
	memcpy(IV, &sector, sizeof(sector_t));

	ablkcipher_request_set_crypt(req, in_sg, out_sg, data_len,
					(void *) IV);

	rc = crypto_ablkcipher_encrypt(req);

	switch (rc) {
	case 0:
		break;

	case -EBUSY:
		/*
		 * Lets make this synchronous request by waiting on
		 * in progress as well
		 */
	case -EINPROGRESS:
		wait_for_completion_interruptible(&result.completion);
		if (result.err)
			pr_err("%s:%s error = %d encrypting the request\n",
				mmc_hostname(host), __func__, result.err);
		else {
			mrq->data->sg = out_sg;
			mrq->data->sg_len = mmc_count_sg(out_sg, data_len);
			mrq->data->orig_sg = in_sg;
		}
		break;

	default:
		goto crypto_operation_failure;
	}

crypto_operation_failure:
	ablkcipher_request_free(req);

ablkcipher_req_alloc_failure:
	crypto_free_ablkcipher(tfm);

	return;
}
EXPORT_SYMBOL(mmc_encrypt_req);

void mmc_decrypt_req(struct mmc_host *host, struct mmc_request *mrq)
{
	struct crypto_ablkcipher *tfm;
	struct ablkcipher_request *req;
	struct mmc_tcrypt_result result;
	struct scatterlist *in_sg = mrq->data->sg;
	int rc = 0;
	u8 IV[MMC_AES_XTS_IV_LEN];
	sector_t sector = mrq->data->sector;

	tfm = crypto_alloc_ablkcipher("xts(aes)", 0, 0);
	if (IS_ERR(tfm)) {
		pr_err("%s:%s ablkcipher tfm allocation failed : error = %lu\n",
				mmc_hostname(host), __func__, PTR_ERR(tfm));
		return;
	}

	req = ablkcipher_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		pr_err("%s:%s ablkcipher request allocation failed\n",
				mmc_hostname(host), __func__);
		goto ablkcipher_req_alloc_failure;
	}

	ablkcipher_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					mmc_crypto_cipher_complete, &result);

	init_completion(&result.completion);
	qcrypto_cipher_set_flag(req,
		QCRYPTO_CTX_USE_PIPE_KEY | QCRYPTO_CTX_XTS_DU_SIZE_512B);
	crypto_ablkcipher_clear_flags(tfm, ~0);
	crypto_ablkcipher_setkey(tfm, NULL, MMC_KEY_SIZE_XTS);

	memset(IV, 0, MMC_AES_XTS_IV_LEN);
	memcpy(IV, &sector, sizeof(sector_t));

	ablkcipher_request_set_crypt(req, in_sg, in_sg,
		mrq->data->blksz * mrq->data->blocks, (void *) IV);

	rc = crypto_ablkcipher_decrypt(req);

	switch (rc) {
	case 0:
		break;

	case -EBUSY:
		/*
		 * Lets make this synchronous request by waiting on
		 * in progress as well
		 */
	case -EINPROGRESS:
		wait_for_completion_interruptible(&result.completion);
		if (result.err)
			pr_err("%s:%s error = %d decrypting the request\n",
				mmc_hostname(host), __func__, result.err);
		break;

	default:
		goto crypto_operation_failure;
	}

crypto_operation_failure:
	ablkcipher_request_free(req);

ablkcipher_req_alloc_failure:
	crypto_free_ablkcipher(tfm);

	return;
}
EXPORT_SYMBOL(mmc_decrypt_req);
