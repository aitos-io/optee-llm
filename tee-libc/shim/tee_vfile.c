// SPDX-License-Identifier: BSD-2-Clause
/*
 * Read-only files that live in TA memory, for libraries that insist on opening a path: the TA
 * registers a buffer under a name (tee_vfile_add), and open/read/pread/lseek/fstat/mmap on that
 * name behave as on a regular file. mmap returns the buffer itself, so a model mapped this way
 * exists once in TA memory; munmap of it is a no-op (the TA owns the buffer).
 */
#include <stdint.h>
#include <string.h>
#include <tee_internal_api.h>

#include <tee_libc.h>

#define LX_SYS_fcntl		25
#define LX_SYS_openat		56
#define LX_SYS_close		57
#define LX_SYS_lseek		62
#define LX_SYS_read		63
#define LX_SYS_readv		65
#define LX_SYS_pread64		67
#define LX_SYS_newfstatat	79
#define LX_SYS_fstat		80
#define LX_SYS_fadvise64	223
#define LX_SYS_mmap		222
#define LX_SYS_munmap		215
#define LX_SYS_madvise		233

#define LX_ENOENT	2
#define LX_EBADF	9
#define LX_EINVAL	22
#define LX_EMFILE	24

#define LX_AT_EMPTY_PATH	0x1000
#define LX_S_IFREG		0100000

#define MAX_FILES	4
#define MAX_FDS		8
#define FD_BASE		3	/* after stdin, stdout, stderr */

struct lx_iovec {
	void *base;
	size_t len;
};

/* struct stat on aarch64 Linux (asm-generic layout), as musl passes it to the kernel. */
struct lx_stat {
	uint64_t dev;
	uint64_t ino;
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint64_t rdev;
	uint64_t pad1;
	int64_t size;
	int32_t blksize;
	int32_t pad2;
	int64_t blocks;
	int64_t times[6];
	uint32_t unused[2];
};

struct vfile {
	char path[64];
	const uint8_t *data;
	size_t size;
};

struct vfd {
	struct vfile *file;
	size_t pos;
};

static struct vfile files[MAX_FILES];
static struct vfd fds[MAX_FDS];

int tee_vfile_add(const char *path, const void *data, size_t size)
{
	size_t i = 0;

	if (strlen(path) >= sizeof(files[0].path))
		return -1;
	for (i = 0; i < MAX_FILES; i++) {
		if (!files[i].data || !strcmp(files[i].path, path)) {
			strcpy(files[i].path, path);
			files[i].data = data;
			files[i].size = size;
			return 0;
		}
	}
	return -1;
}

static struct vfile *lookup(const char *path)
{
	size_t i = 0;

	for (i = 0; i < MAX_FILES; i++)
		if (files[i].data && !strcmp(files[i].path, path))
			return files + i;
	return NULL;
}

static struct vfd *get_fd(long fd)
{
	if (fd < FD_BASE || fd >= FD_BASE + MAX_FDS || !fds[fd - FD_BASE].file)
		return NULL;
	return fds + (fd - FD_BASE);
}

static long do_open(const char *path)
{
	struct vfile *f = lookup(path);
	size_t i = 0;

	if (!f)
		return -LX_ENOENT;
	for (i = 0; i < MAX_FDS; i++) {
		if (!fds[i].file) {
			fds[i].file = f;
			fds[i].pos = 0;
			return FD_BASE + i;
		}
	}
	return -LX_EMFILE;
}

static size_t copy_out(struct vfd *v, size_t pos, void *buf, size_t len)
{
	size_t n = 0;

	if (pos >= v->file->size)
		return 0;
	n = v->file->size - pos;
	if (n > len)
		n = len;
	memcpy(buf, v->file->data + pos, n);
	return n;
}

static void fill_stat(struct vfile *f, struct lx_stat *st)
{
	memset(st, 0, sizeof(*st));
	st->mode = LX_S_IFREG | 0444;
	st->nlink = 1;
	st->size = f->size;
	st->blksize = 4096;
	st->blocks = (f->size + 511) / 512;
}

long tee_vfile_syscall(long n, long a, long b, long c, long d, long e, long f, int *handled)
{
	struct vfd *v = NULL;

	*handled = 1;
	switch (n) {
	case LX_SYS_openat:
		if (lookup((const char *)b))
			return do_open((const char *)b);
		break;
	case LX_SYS_newfstatat:
		if ((d & LX_AT_EMPTY_PATH) && !*(const char *)b) {
			v = get_fd(a);
			if (!v)
				break;
			fill_stat(v->file, (struct lx_stat *)c);
			return 0;
		}
		if (lookup((const char *)b)) {
			fill_stat(lookup((const char *)b), (struct lx_stat *)c);
			return 0;
		}
		break;
	case LX_SYS_fstat:
		v = get_fd(a);
		if (!v)
			break;
		fill_stat(v->file, (struct lx_stat *)b);
		return 0;
	case LX_SYS_read:
		v = get_fd(a);
		if (!v)
			break;
		c = copy_out(v, v->pos, (void *)b, c);
		v->pos += c;
		return c;
	case LX_SYS_readv: {
		const struct lx_iovec *iov = (const struct lx_iovec *)b;
		long total = 0;
		long i = 0;

		v = get_fd(a);
		if (!v)
			break;
		for (i = 0; i < c; i++) {
			size_t got = copy_out(v, v->pos, iov[i].base, iov[i].len);

			v->pos += got;
			total += got;
			if (got < iov[i].len)
				break;
		}
		return total;
	}
	case LX_SYS_pread64:
		v = get_fd(a);
		if (!v)
			break;
		if (d < 0)
			return -LX_EINVAL;
		return copy_out(v, d, (void *)b, c);
	case LX_SYS_lseek: {
		int64_t base = 0;

		v = get_fd(a);
		if (!v)
			break;
		if (c == 0)
			base = 0;
		else if (c == 1)
			base = v->pos;
		else if (c == 2)
			base = v->file->size;
		else
			return -LX_EINVAL;
		if (base + b < 0)
			return -LX_EINVAL;
		v->pos = base + b;
		return v->pos;
	}
	case LX_SYS_close:
		v = get_fd(a);
		if (!v)
			break;
		v->file = NULL;
		return 0;
	case LX_SYS_fcntl:
	case LX_SYS_fadvise64:
		if (get_fd(a))
			return 0;
		break;
	case LX_SYS_mmap:
		/* mmap(addr, len, prot, flags, fd, off): read-only views of a registered file */
		v = get_fd(e);
		if (!v)
			break;
		if (f < 0 || (size_t)f > v->file->size || (f & 4095))
			return -LX_EINVAL;
		return (long)(v->file->data + f);
	case LX_SYS_munmap:
	case LX_SYS_madvise: {
		size_t i = 0;

		for (i = 0; i < MAX_FILES; i++)
			if (files[i].data && (const uint8_t *)a >= files[i].data &&
			    (const uint8_t *)a < files[i].data + files[i].size)
				return 0;
		break;
	}
	default:
		break;
	}
	*handled = 0;
	return 0;
}
