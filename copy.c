#define USE_THE_REPOSITORY_VARIABLE

#include "git-compat-util.h"
#include "copy.h"
#include "path.h"
#include "gettext.h"
#include "strbuf.h"
#include "trace2.h"
#include "abspath.h"

#if defined(__linux__)
#include <sys/ioctl.h>
#include <linux/fs.h>
#elif defined(__APPLE__)
#include <sys/clonefile.h>
#endif

int copy_fd(int ifd, int ofd)
{
	while (1) {
		char buffer[8192];
		ssize_t len = xread(ifd, buffer, sizeof(buffer));
		if (!len)
			break;
		if (len < 0)
			return COPY_READ_ERROR;
		if (write_in_full(ofd, buffer, len) < 0)
			return COPY_WRITE_ERROR;
	}
	return 0;
}

static int copy_times(const char *dst, const char *src)
{
	struct stat st;
	struct utimbuf times;
	if (stat(src, &st) < 0)
		return -1;
	times.actime = st.st_atime;
	times.modtime = st.st_mtime;
	if (utime(dst, &times) < 0)
		return -1;
	return 0;
}

int copy_file(const char *dst, const char *src, int mode)
{
	int fdi, fdo, status;

	mode = (mode & 0111) ? 0777 : 0666;
	if ((fdi = open(src, O_RDONLY)) < 0)
		return fdi;
	if ((fdo = open(dst, O_WRONLY | O_CREAT | O_EXCL, mode)) < 0) {
		close(fdi);
		return fdo;
	}
	status = copy_fd(fdi, fdo);
	switch (status) {
	case COPY_READ_ERROR:
		error_errno("copy-fd: read returned");
		break;
	case COPY_WRITE_ERROR:
		error_errno("copy-fd: write returned");
		break;
	}
	close(fdi);
	if (close(fdo) != 0)
		return error_errno("%s: close error", dst);

	if (!status && adjust_shared_perm(the_repository, dst))
		return -1;

	return status;
}

int copy_file_with_time(const char *dst, const char *src, int mode)
{
	int status = copy_file(dst, src, mode);
	if (!status)
		return copy_times(dst, src);
	return status;
}

int copy_file_cow(const char *dst, const char *src)
{
#if defined(__linux__)
	int fdi, fdo;
	int ret = -1;

	if ((fdi = open(src, O_RDONLY)) < 0)
		return -1;
	/* Create file; if successful, perform COW */
	if ((fdo = open(dst, O_WRONLY | O_CREAT | O_EXCL, 0666)) < 0) {
		close(fdi);
		return -1;
	}

#ifndef FICLONE
#define FICLONE _IOW(0x94, 9, int)
#endif

	if (ioctl(fdo, FICLONE, fdi) == 0) {
		trace2_data_string("cow", NULL, "status", "success");
		ret = 0;
	} else {
		/* If FICLONE fails, we must remove the created file */
		unlink(dst);
	}

	close(fdi);
	close(fdo);
	return ret;

#elif defined(__APPLE__)
	if (!clonefile(src, dst, 0)) {
		trace2_data_string("cow", NULL, "status", "success");
		return 0;
	}
	return -1;

#else
	return -1;
#endif
}
