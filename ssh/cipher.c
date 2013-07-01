/* $OpenBSD: cipher.c,v 1.88 2013/04/19 01:06:50 djm Exp $ */
/*
 * Author: Tatu Ylonen <ylo@cs.hut.fi>
 * Copyright (c) 1995 Tatu Ylonen <ylo@cs.hut.fi>, Espoo, Finland
 *                    All rights reserved
 *
 * As far as I am concerned, the code I have written for this software
 * can be used freely for any purpose.  Any derived versions of this
 * software must be clearly marked as such, and if the derived work is
 * incompatible with the protocol description in the RFC file, it must be
 * called by a name other than "ssh" or "Secure Shell".
 *
 *
 * Copyright (c) 1999 Niels Provos.  All rights reserved.
 * Copyright (c) 1999, 2000 Markus Friedl.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>

#include <openssl/md5.h>

#include <string.h>
#include <stdarg.h>

#include "err.h"
#include "cipher.h"
#include "misc.h"

extern const EVP_CIPHER *evp_ssh1_bf(void);
extern const EVP_CIPHER *evp_ssh1_3des(void);
extern int ssh1_3des_iv(EVP_CIPHER_CTX *, int, u_char *, int);

struct sshcipher {
	char	*name;
	int	number;		/* for ssh1 only */
	u_int	block_size;
	u_int	key_len;
	u_int	iv_len;		/* defaults to block_size */
	u_int	auth_len;
	u_int	discard_len;
	u_int	cbc_mode;
	const EVP_CIPHER	*(*evptype)(void);
};

static const struct sshcipher ciphers[] = {
	{ "none",	SSH_CIPHER_NONE, 8, 0, 0, 0, 0, 0, EVP_enc_null },
	{ "des",	SSH_CIPHER_DES, 8, 8, 0, 0, 0, 1, EVP_des_cbc },
	{ "3des",	SSH_CIPHER_3DES, 8, 16, 0, 0, 0, 1, evp_ssh1_3des },
	{ "blowfish",	SSH_CIPHER_BLOWFISH, 8, 32, 0, 0, 0, 1, evp_ssh1_bf },

	{ "3des-cbc",	SSH_CIPHER_SSH2, 8, 24, 0, 0, 0, 1, EVP_des_ede3_cbc },
	{ "blowfish-cbc",
			SSH_CIPHER_SSH2, 8, 16, 0, 0, 0, 1, EVP_bf_cbc },
	{ "cast128-cbc",
			SSH_CIPHER_SSH2, 8, 16, 0, 0, 0, 1, EVP_cast5_cbc },
	{ "arcfour",	SSH_CIPHER_SSH2, 8, 16, 0, 0, 0, 0, EVP_rc4 },
	{ "arcfour128",	SSH_CIPHER_SSH2, 8, 16, 0, 0, 1536, 0, EVP_rc4 },
	{ "arcfour256",	SSH_CIPHER_SSH2, 8, 32, 0, 0, 1536, 0, EVP_rc4 },
	{ "aes128-cbc",	SSH_CIPHER_SSH2, 16, 16, 0, 0, 0, 1, EVP_aes_128_cbc },
	{ "aes192-cbc",	SSH_CIPHER_SSH2, 16, 24, 0, 0, 0, 1, EVP_aes_192_cbc },
	{ "aes256-cbc",	SSH_CIPHER_SSH2, 16, 32, 0, 0, 0, 1, EVP_aes_256_cbc },
	{ "rijndael-cbc@lysator.liu.se",
			SSH_CIPHER_SSH2, 16, 32, 0, 0, 0, 1, EVP_aes_256_cbc },
	{ "aes128-ctr",	SSH_CIPHER_SSH2, 16, 16, 0, 0, 0, 0, EVP_aes_128_ctr },
	{ "aes192-ctr",	SSH_CIPHER_SSH2, 16, 24, 0, 0, 0, 0, EVP_aes_192_ctr },
	{ "aes256-ctr",	SSH_CIPHER_SSH2, 16, 32, 0, 0, 0, 0, EVP_aes_256_ctr },
	{ "aes128-gcm@openssh.com",
			SSH_CIPHER_SSH2, 16, 16, 12, 16, 0, 0, EVP_aes_128_gcm },
	{ "aes256-gcm@openssh.com",
			SSH_CIPHER_SSH2, 16, 32, 12, 16, 0, 0, EVP_aes_256_gcm },

	{ NULL,		SSH_CIPHER_INVALID, 0, 0, 0, 0, 0, 0, NULL }
};

/*--*/

/* Returns a comma-separated list of supported ciphers. */
char *
cipher_alg_list(void)
{
	char *ret = NULL;
	size_t nlen, rlen = 0;
	const struct sshcipher *c;

	for (c = ciphers; c->name != NULL; c++) {
		if (c->number != SSH_CIPHER_SSH2)
			continue;
		if (ret != NULL)
			ret[rlen++] = '\n';
		nlen = strlen(c->name);
		if (reallocn((void **)&ret, 1, rlen + nlen + 2) != 0)
			return NULL;
		memcpy(ret + rlen, c->name, nlen + 1);
		rlen += nlen;
	}
	return ret;
}

u_int
cipher_blocksize(const struct sshcipher *c)
{
	return (c->block_size);
}

u_int
cipher_keylen(const struct sshcipher *c)
{
	return (c->key_len);
}

u_int
cipher_authlen(const struct sshcipher *c)
{
	return (c->auth_len);
}

u_int
cipher_ivlen(const struct sshcipher *c)
{
	return (c->iv_len ? c->iv_len : c->block_size);
}

u_int
cipher_get_number(const struct sshcipher *c)
{
	return (c->number);
}

u_int
cipher_is_cbc(const struct sshcipher *c)
{
	return (c->cbc_mode);
}

u_int
cipher_mask_ssh1(int client)
{
	u_int mask = 0;
	mask |= 1 << SSH_CIPHER_3DES;		/* Mandatory */
	mask |= 1 << SSH_CIPHER_BLOWFISH;
	if (client) {
		mask |= 1 << SSH_CIPHER_DES;
	}
	return mask;
}

const struct sshcipher *
cipher_by_name(const char *name)
{
	const struct sshcipher *c;
	for (c = ciphers; c->name != NULL; c++)
		if (strcmp(c->name, name) == 0)
			return c;
	return NULL;
}

const struct sshcipher *
cipher_by_number(int id)
{
	const struct sshcipher *c;
	for (c = ciphers; c->name != NULL; c++)
		if (c->number == id)
			return c;
	return NULL;
}

#define	CIPHER_SEP	","
int
ciphers_valid(const char *names)
{
	const struct sshcipher *c;
	char *cipher_list, *cp;
	char *p;

	if (names == NULL || strcmp(names, "") == 0)
		return 0;
	if ((cipher_list = cp = strdup(names)) == NULL)
		return 0;
	for ((p = strsep(&cp, CIPHER_SEP)); p && *p != '\0';
	    (p = strsep(&cp, CIPHER_SEP))) {
		c = cipher_by_name(p);
		if (c == NULL || c->number != SSH_CIPHER_SSH2) {
			free(cipher_list);
			return 0;
		}
	}
	free(cipher_list);
	return 1;
}

/*
 * Parses the name of the cipher.  Returns the number of the corresponding
 * cipher, or -1 on error.
 */

int
cipher_number(const char *name)
{
	const struct sshcipher *c;
	if (name == NULL)
		return -1;
	for (c = ciphers; c->name != NULL; c++)
		if (strcasecmp(c->name, name) == 0)
			return c->number;
	return -1;
}

char *
cipher_name(int id)
{
	const struct sshcipher *c = cipher_by_number(id);
	return (c==NULL) ? "<unknown>" : c->name;
}

const char *
cipher_warning_message(const struct sshcipher_ctx *cc)
{
	if (cc == NULL || cc->cipher == NULL)
		return NULL;
	if (cc->cipher->number == SSH_CIPHER_DES)
		return "use of DES is strongly discouraged due to "
		    "cryptographic weaknesses";
	return NULL;
}

int
cipher_init(struct sshcipher_ctx *cc, const struct sshcipher *cipher,
    const u_char *key, u_int keylen, const u_char *iv, u_int ivlen,
    int do_encrypt)
{
	int ret = SSH_ERR_INTERNAL_ERROR;
	const EVP_CIPHER *type;
	int klen;
	u_char *junk, *discard;

	if (cipher->number == SSH_CIPHER_DES) {
		if (keylen > 8)
			keylen = 8;
	}
	cc->plaintext = (cipher->number == SSH_CIPHER_NONE);
	cc->encrypt = do_encrypt;

	if (keylen < cipher->key_len ||
	    (iv != NULL && ivlen < cipher->iv_len))
		return SSH_ERR_INVALID_ARGUMENT;

	cc->cipher = cipher;
	type = (*cipher->evptype)();
	EVP_CIPHER_CTX_init(&cc->evp);
	if (EVP_CipherInit(&cc->evp, type, NULL, (u_char *)iv,
	    (do_encrypt == CIPHER_ENCRYPT)) == 0) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
 bad:
		EVP_CIPHER_CTX_cleanup(&cc->evp);
		return ret;
	}
	if (cipher_authlen(cipher) &&
	    !EVP_CIPHER_CTX_ctrl(&cc->evp, EVP_CTRL_GCM_SET_IV_FIXED,
	    -1, (u_char *)iv)) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto bad;
	}
	klen = EVP_CIPHER_CTX_key_length(&cc->evp);
	if (klen > 0 && keylen != (u_int)klen) {
		if (EVP_CIPHER_CTX_set_key_length(&cc->evp, keylen) == 0) {
			ret = SSH_ERR_LIBCRYPTO_ERROR;
			goto bad;
		}
	}
	if (EVP_CipherInit(&cc->evp, NULL, (u_char *)key, NULL, -1) == 0) {
		ret = SSH_ERR_LIBCRYPTO_ERROR;
		goto bad;
	}

	if (cipher->discard_len > 0) {
		if ((junk = malloc(cipher->discard_len)) == NULL ||
		    (discard = malloc(cipher->discard_len)) == NULL) {
			if (junk != NULL)
				free(junk);
			ret = SSH_ERR_ALLOC_FAIL;
			goto bad;
		}
		ret = EVP_Cipher(&cc->evp, discard, junk, cipher->discard_len);
		bzero(discard, cipher->discard_len);
		free(junk);
		free(discard);
		if (ret != 1) {
			ret = SSH_ERR_LIBCRYPTO_ERROR;
			goto bad;
		}
	}
	return 0;
}

/*
 * cipher_crypt() operates as following:
 * Copy 'aadlen' bytes (without en/decryption) from 'src' to 'dest'.
 * Theses bytes are treated as additional authenticated data for
 * authenticated encryption modes.
 * En/Decrypt 'len' bytes at offset 'aadlen' from 'src' to 'dest'.
 * Use 'authlen' bytes at offset 'len'+'aadlen' as the authentication tag.
 * This tag is written on encryption and verified on decryption.
 * Both 'aadlen' and 'authlen' can be set to 0.
 */
int
cipher_crypt(struct sshcipher_ctx *cc, u_char *dest, const u_char *src,
    u_int len, u_int aadlen, u_int authlen)
{
	if (authlen) {
		u_char lastiv[1];

		if (authlen != cipher_authlen(cc->cipher))
			return SSH_ERR_INVALID_ARGUMENT;
		/* increment IV */
		if (!EVP_CIPHER_CTX_ctrl(&cc->evp, EVP_CTRL_GCM_IV_GEN,
		    1, lastiv))
			return SSH_ERR_LIBCRYPTO_ERROR;
		/* set tag on decyption */
		if (!cc->encrypt &&
		    !EVP_CIPHER_CTX_ctrl(&cc->evp, EVP_CTRL_GCM_SET_TAG,
		    authlen, (u_char *)src + aadlen + len))
			return SSH_ERR_LIBCRYPTO_ERROR;
	}
	if (aadlen) {
		if (authlen &&
		    EVP_Cipher(&cc->evp, NULL, (u_char *)src, aadlen) < 0)
			return SSH_ERR_LIBCRYPTO_ERROR;
		memcpy(dest, src, aadlen);
	}
	if (len % cc->cipher->block_size)
		return SSH_ERR_INVALID_ARGUMENT;
	if (EVP_Cipher(&cc->evp, dest + aadlen, (u_char *)src + aadlen,
	    len) < 0)
		return SSH_ERR_LIBCRYPTO_ERROR;
	if (authlen) {
		/* compute tag (on encrypt) or verify tag (on decrypt) */
		if (EVP_Cipher(&cc->evp, NULL, NULL, 0) < 0)
			return cc->encrypt ?
			    SSH_ERR_LIBCRYPTO_ERROR : SSH_ERR_MAC_INVALID;
		if (cc->encrypt &&
		    !EVP_CIPHER_CTX_ctrl(&cc->evp, EVP_CTRL_GCM_GET_TAG,
		    authlen, dest + aadlen + len))
			return SSH_ERR_LIBCRYPTO_ERROR;
	}
	return 0;
}

int
cipher_cleanup(struct sshcipher_ctx *cc)
{
	if (EVP_CIPHER_CTX_cleanup(&cc->evp) == 0)
		return SSH_ERR_LIBCRYPTO_ERROR;
	return 0;
}

/*
 * Selects the cipher, and keys if by computing the MD5 checksum of the
 * passphrase and using the resulting 16 bytes as the key.
 */
int
cipher_set_key_string(struct sshcipher_ctx *cc, const struct sshcipher *cipher,
    const char *pphrase, int do_encrypt)
{
	MD5_CTX md;
	u_char digest[16];
	int ret = SSH_ERR_LIBCRYPTO_ERROR;

	if (MD5_Init(&md) != 1 ||
	    MD5_Update(&md, (const u_char *)pphrase, strlen(pphrase)) != 1 ||
	    MD5_Final(digest, &md) != 1)
		goto out;

	ret = cipher_init(cc, cipher, digest, 16, NULL, 0, do_encrypt);

 out:
	memset(digest, 0, sizeof(digest));
	memset(&md, 0, sizeof(md));
	return ret;
}

/*
 * Exports an IV from the sshcipher_ctx required to export the key
 * state back from the unprivileged child to the privileged parent
 * process.
 */
int
cipher_get_keyiv_len(const struct sshcipher_ctx *cc)
{
	const struct sshcipher *c = cc->cipher;
	int ivlen;

	if (c->number == SSH_CIPHER_3DES)
		ivlen = 24;
	else
		ivlen = EVP_CIPHER_CTX_iv_length(&cc->evp);
	return (ivlen);
}

int
cipher_get_keyiv(struct sshcipher_ctx *cc, u_char *iv, u_int len)
{
	const struct sshcipher *c = cc->cipher;
	int evplen;

	switch (c->number) {
	case SSH_CIPHER_SSH2:
	case SSH_CIPHER_DES:
	case SSH_CIPHER_BLOWFISH:
		evplen = EVP_CIPHER_CTX_iv_length(&cc->evp);
		if (evplen == 0)
			return 0;
		else if (evplen < 0)
			return SSH_ERR_LIBCRYPTO_ERROR;
		if ((u_int)evplen != len)
			return SSH_ERR_INVALID_ARGUMENT;
		if (cipher_authlen(c)) {
			if (!EVP_CIPHER_CTX_ctrl(&cc->evp, EVP_CTRL_GCM_IV_GEN,
			   len, iv))
			       return SSH_ERR_LIBCRYPTO_ERROR;
		} else
			memcpy(iv, cc->evp.iv, len);
		break;
	case SSH_CIPHER_3DES:
		return ssh1_3des_iv(&cc->evp, 0, iv, 24);
	default:
		return SSH_ERR_INVALID_ARGUMENT;
	}
	return 0;
}

int
cipher_set_keyiv(struct sshcipher_ctx *cc, const u_char *iv)
{
	const struct sshcipher *c = cc->cipher;
	int evplen = 0;

	switch (c->number) {
	case SSH_CIPHER_SSH2:
	case SSH_CIPHER_DES:
	case SSH_CIPHER_BLOWFISH:
		evplen = EVP_CIPHER_CTX_iv_length(&cc->evp);
		if (evplen <= 0)
			return SSH_ERR_LIBCRYPTO_ERROR;
		if (cipher_authlen(c)) {
			/* XXX iv arg is const, but EVP_CIPHER_CTX_ctrl isn't */
			if (!EVP_CIPHER_CTX_ctrl(&cc->evp,
			    EVP_CTRL_GCM_SET_IV_FIXED, -1, (void *)iv))
				return SSH_ERR_LIBCRYPTO_ERROR;
		} else
			memcpy(cc->evp.iv, iv, evplen);
		break;
	case SSH_CIPHER_3DES:
		return ssh1_3des_iv(&cc->evp, 1, (u_char *)iv, 24);
	default:
		return SSH_ERR_INVALID_ARGUMENT;
	}
	return 0;
}

#define EVP_X_STATE(evp)	(evp).cipher_data
#define EVP_X_STATE_LEN(evp)	(evp).cipher->ctx_size

int
cipher_get_keycontext(const struct sshcipher_ctx *cc, u_char *dat)
{
	const struct sshcipher *c = cc->cipher;
	int plen = 0;

	if (c->evptype == EVP_rc4) {
		plen = EVP_X_STATE_LEN(cc->evp);
		if (dat == NULL)
			return (plen);
		memcpy(dat, EVP_X_STATE(cc->evp), plen);
	}
	return (plen);
}

void
cipher_set_keycontext(struct sshcipher_ctx *cc, const u_char *dat)
{
	const struct sshcipher *c = cc->cipher;
	int plen;

	if (c->evptype == EVP_rc4) {
		plen = EVP_X_STATE_LEN(cc->evp);
		memcpy(EVP_X_STATE(cc->evp), dat, plen);
	}
}
