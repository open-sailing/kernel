/*
 *  linux/fs/9p/vfs_addr.c
 *
 * This file contians vfs address (mmap) ops for 9P2000.
 *
 *  Copyright (C) 2005 by Eric Van Hensbergen <ericvh@gmail.com>
 *  Copyright (C) 2002 by Ron Minnich <rminnich@lanl.gov>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2
 *  as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to:
 *  Free Software Foundation
 *  51 Franklin Street, Fifth Floor
 *  Boston, MA  02111-1301  USA
 *
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/inet.h>
#include <linux/pagemap.h>
#include <linux/idr.h>
#include <linux/sched.h>
#include <linux/uio.h>
#include <linux/slab.h>
#include <net/9p/9p.h>
#include <net/9p/client.h>
#include <trace/events/writeback.h>

#include "v9fs.h"
#include "v9fs_vfs.h"
#include "cache.h"
#include "fid.h"

/**
 * v9fs_fid_readpage - read an entire page in from 9P
 *
 * @fid: fid being read
 * @page: structure to page
 *
 */
static int v9fs_fid_readpage(struct p9_fid *fid, struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct bio_vec bvec = {.bv_page = page, .bv_len = PAGE_SIZE};
	struct iov_iter to;
	int retval, err;

	p9_debug(P9_DEBUG_VFS, "\n");

	BUG_ON(!PageLocked(page));

	retval = v9fs_readpage_from_fscache(inode, page);
	if (retval == 0)
		return retval;

	iov_iter_bvec(&to, ITER_BVEC | READ, &bvec, 1, PAGE_SIZE);

	retval = p9_client_read(fid, page_offset(page), &to, &err);
	if (err) {
		v9fs_uncache_page(inode, page);
		retval = err;
		goto done;
	}

	zero_user(page, retval, PAGE_SIZE - retval);
	flush_dcache_page(page);
	SetPageUptodate(page);

	v9fs_readpage_to_fscache(inode, page);
	retval = 0;

done:
	unlock_page(page);
	return retval;
}

/**
 * v9fs_vfs_readpage - read an entire page in from 9P
 *
 * @filp: file being read
 * @page: structure to page
 *
 */

static int v9fs_vfs_readpage(struct file *filp, struct page *page)
{
	return v9fs_fid_readpage(filp->private_data, page);
}

#if defined(CONFIG_KVM_GUEST_MICROVM)

/*
 * Context for "fast readpages"
 */
struct v9fs_readpages_ctx {
	struct file *filp;
	struct address_space *mapping;
	pgoff_t start_index; /* index of the first page with actual data */
	char *buf; /* buffer with actual data */
	int len; /* length of the actual data */
	int num_pages; /* maximal data chunk (in pages) that can be
			  passed per transmission */
};

static int init_readpages_ctx(struct v9fs_readpages_ctx *ctx,
			      struct file *filp,
			      struct address_space *mapping,
			      int num_pages)
{
	memset(ctx, 0, sizeof(*ctx));
	ctx->buf = kmalloc(num_pages << PAGE_CACHE_SHIFT, GFP_KERNEL);
	if (!ctx->buf)
		return -ENOMEM;
	ctx->filp = filp;
	ctx->mapping = mapping;
	ctx->num_pages = num_pages;
	return 0;
}

static void done_readpages_ctx(struct v9fs_readpages_ctx *ctx)
{
	kfree(ctx->buf);
}

static int receive_buffer(struct file *filp,
			  char *buf,
			  off_t offset, /* offset in the file */
			  int len,
			  int *err)
{
	struct kvec kvec;
	struct iov_iter iter;

	kvec.iov_base = buf;
	kvec.iov_len = len;
	iov_iter_kvec(&iter, READ | ITER_KVEC, &kvec, 1, len);

	return p9_client_read(filp->private_data, offset, &iter, err);
}

static int fast_filler(struct v9fs_readpages_ctx *ctx, struct page *page)
{
	int err;
	int ret = 0;
	char *kdata;
	int to_page;
	off_t off_in_buf;
	struct inode *inode = page->mapping->host;

	BUG_ON(!PageLocked(page));
	/*
	 * first, validate the buffer
	 */
	if (page->index < ctx->start_index ||
	    ctx->start_index + ctx->num_pages < page->index) {
		/*
		 * No actual data in the buffer,
		 * so actualize it
		 */
		ret = receive_buffer(ctx->filp,
				     ctx->buf,
				     page_offset(page),
				     ctx->num_pages << PAGE_CACHE_SHIFT,
				     &err);
		if (err) {
			pr_warn("failed to receive buffer off=%llu (%d)\n",
					(unsigned long long)page_offset(page),
			       err);
			ret = err;
			goto done;
		}
		ctx->start_index = page->index;
		ctx->len = ret;
		ret = 0;
	}
	/*
	 * fill the page with buffer's data
	 */
	off_in_buf = (page->index - ctx->start_index) << PAGE_CACHE_SHIFT;
	if (off_in_buf >= ctx->len) {
		/*
		 * No actual data to fill the page with
		 */
		ret = -1;
		goto done;
	}
	to_page = ctx->len - off_in_buf;
	if (to_page >= PAGE_CACHE_SIZE)
		to_page = PAGE_CACHE_SIZE;

	kdata = kmap_atomic(page);
	memcpy(kdata, ctx->buf + off_in_buf, to_page);
	memset(kdata + to_page, 0, PAGE_CACHE_SIZE - to_page);
	kunmap_atomic(kdata);

	flush_dcache_page(page);
	SetPageUptodate(page);
	v9fs_readpage_to_fscache(inode, page);
 done:
	unlock_page(page);
	return ret;
}

/**
 * Try to read pages by groups. For every such group we issue only one
 * read request to the server.
 * @num_pages: maximal chunk of data (in pages) that can be passed per
 * such request
 */
static int v9fs_readpages_tryfast(struct file *filp,
				  struct address_space *mapping,
				  struct list_head *pages,
				  int num_pages)
{
	int ret;
	struct v9fs_readpages_ctx ctx;

	ret = init_readpages_ctx(&ctx, filp, mapping, num_pages);
	if (ret)
		/*
		 * Can not allocate resources for the fast path,
		 * so do it by slow way
		 */
		return read_cache_pages(mapping, pages,
					(void *)v9fs_vfs_readpage, filp);

	else
		ret = read_cache_pages(mapping, pages,
				       (void *)fast_filler, &ctx);
	done_readpages_ctx(&ctx);
	return ret;
}

/**
 * v9fs_vfs_readpages - read a set of pages from 9P
 *
 * @filp: file being read
 * @mapping: the address space
 * @pages: list of pages to read
 * @nr_pages: count of pages to read
 *
 */

static int v9fs_vfs_readpages(struct file *filp, struct address_space *mapping,
			     struct list_head *pages, unsigned nr_pages)
{
	int ret = 0;
	struct inode *inode;
	struct v9fs_flush_set *fset;

	inode = mapping->host;
	p9_debug(P9_DEBUG_VFS, "inode: %p file: %p\n", inode, filp);

	ret = v9fs_readpages_from_fscache(inode, mapping, pages, &nr_pages);
	if (ret == 0)
		return ret;

	fset = v9fs_inode2v9ses(mapping->host)->flush;
	if (!fset)
		/*
		 * Do it by slow way
		 */
		ret = read_cache_pages(mapping, pages,
				       (void *)v9fs_vfs_readpage, filp);
	else
		ret = v9fs_readpages_tryfast(filp, mapping,
					     pages, fset->num_pages);

	p9_debug(P9_DEBUG_VFS, "  = %d\n", ret);
	return ret;
}

#else
static int v9fs_vfs_readpages(struct file *filp, struct address_space *mapping,
			     struct list_head *pages, unsigned nr_pages)
{
	int ret = 0;
	struct inode *inode;

	inode = mapping->host;
	p9_debug(P9_DEBUG_VFS, "inode: %p file: %p\n", inode, filp);

	ret = v9fs_readpages_from_fscache(inode, mapping, pages, &nr_pages);
	if (ret == 0)
		return ret;

	ret = read_cache_pages(mapping, pages, (void *)v9fs_vfs_readpage, filp);
	p9_debug(P9_DEBUG_VFS, "  = %d\n", ret);
	return ret;
}

#endif /* CONFIG_KVM_GUEST_MICROVM */

/**
 * v9fs_release_page - release the private state associated with a page
 *
 * Returns 1 if the page can be released, false otherwise.
 */

static int v9fs_release_page(struct page *page, gfp_t gfp)
{
	if (PagePrivate(page))
		return 0;
	return v9fs_fscache_release_page(page, gfp);
}

/**
 * v9fs_invalidate_page - Invalidate a page completely or partially
 *
 * @page: structure to page
 * @offset: offset in the page
 */

static void v9fs_invalidate_page(struct page *page, unsigned int offset,
				 unsigned int length)
{
	/*
	 * If called with zero offset, we should release
	 * the private state assocated with the page
	 */
	if (offset == 0 && length == PAGE_CACHE_SIZE)
		v9fs_fscache_invalidate_page(page);
}

static int v9fs_vfs_writepage_locked(struct page *page)
{
	struct inode *inode = page->mapping->host;
	struct v9fs_inode *v9inode = V9FS_I(inode);
	loff_t size = i_size_read(inode);
	struct iov_iter from;
	struct bio_vec bvec;
	int err, len;

	if (page->index == size >> PAGE_CACHE_SHIFT)
		len = size & ~PAGE_CACHE_MASK;
	else
		len = PAGE_CACHE_SIZE;

	bvec.bv_page = page;
	bvec.bv_offset = 0;
	bvec.bv_len = len;
	iov_iter_bvec(&from, ITER_BVEC | WRITE, &bvec, 1, len);

	/* We should have writeback_fid always set */
	BUG_ON(!v9inode->writeback_fid);

	set_page_writeback(page);

	p9_client_write(v9inode->writeback_fid, page_offset(page), &from, &err);

	end_page_writeback(page);
	return err;
}

static int v9fs_vfs_writepage(struct page *page, struct writeback_control *wbc)
{
	int retval;

	p9_debug(P9_DEBUG_VFS, "page %p\n", page);

	retval = v9fs_vfs_writepage_locked(page);
	if (retval < 0) {
		if (retval == -EAGAIN) {
			redirty_page_for_writepage(wbc, page);
			retval = 0;
		} else {
			SetPageError(page);
			mapping_set_error(page->mapping, retval);
		}
	} else
		retval = 0;

	unlock_page(page);
	return retval;
}

#if defined(CONFIG_KVM_GUEST_MICROVM)
static void redirty_pages_for_writeback(struct page **pages, int nr,
					struct writeback_control *wbc)
{
	int i;

	for (i = 0; i < nr; i++) {
		lock_page(pages[i]);
		redirty_page_for_writepage(wbc, pages[i]);
		unlock_page(pages[i]);
	}
}

static void set_pages_error(struct page **pages, int nr, int error)
{
	int i;

	for (i = 0; i < nr; i++) {
		lock_page(pages[i]);
		SetPageError(pages[i]);
		mapping_set_error(pages[i]->mapping, error);
		unlock_page(pages[i]);
	}
}

#define V9FS_WRITEPAGES_DEBUG   (0)

struct flush_context {
	struct writeback_control *wbc;
	struct address_space *mapping;
	struct v9fs_flush_set *fset;
	pgoff_t done_index;
	pgoff_t writeback_index;
	pgoff_t index;
	pgoff_t end; /* Inclusive */
	const char *msg;
	int cycled;
	int range_whole;
	int done;
};

/**
 * Copy a page with file's data to buffer.
 * Handle races with truncate, etc.
 * Return number of copied bytes
 *
 * @page: page to copy data from;
 * @page_nr: serial number of the page
 */
static int flush_page(struct page *page, int page_nr, struct flush_context *ctx)
{
	char *kdata;
	loff_t isize;
	int copied = 0;
	struct writeback_control *wbc = ctx->wbc;
	/*
	 * At this point, the page may be truncated or
	 * invalidated (changing page->mapping to NULL), or
	 * even swizzled back from swapper_space to tmpfs file
	 * mapping. However, page->index will not change
	 * because we have a reference on the page.
	 */
	if (page->index > ctx->end) {
		/*
		 * can't be range_cyclic (1st pass) because
		 * end == -1 in that case.
		 */
		ctx->done = 1;
		ctx->msg = "page out of range";
		goto exit;
	}
	ctx->done_index = page->index;
	lock_page(page);
	/*
	 * Page truncated or invalidated. We can freely skip it
	 * then, even for data integrity operations: the page
	 * has disappeared concurrently, so there could be no
	 * real expectation of this data interity operation
	 * even if there is now a new, dirty page at the same
	 * pagecache address.
	 */
	if (unlikely(page->mapping != ctx->mapping)) {
		unlock_page(page);
		ctx->msg = "page truncated or invalidated";
		goto exit;
	}
	if (!PageDirty(page)) {
		/*
		 * someone wrote it for us
		 */
		unlock_page(page);
		ctx->msg = "page not dirty";
		goto exit;
	}
	if (PageWriteback(page)) {
		if (wbc->sync_mode != WB_SYNC_NONE)
			wait_on_page_writeback(page);
		else {
			unlock_page(page);
			ctx->msg = "page is writeback";
			goto exit;
		}
	}
	BUG_ON(PageWriteback(page));
	if (!clear_page_dirty_for_io(page)) {
		unlock_page(page);
		ctx->msg = "failed to clear page dirty";
		goto exit;
	}
	trace_wbc_writepage(wbc, inode_to_bdi(ctx->mapping->host));

	set_page_writeback(page);
	isize = i_size_read(ctx->mapping->host);
	if (page->index == isize >> PAGE_CACHE_SHIFT)
		copied = isize & ~PAGE_CACHE_MASK;
	else
		copied = PAGE_CACHE_SIZE;
	kdata = kmap_atomic(page);
	memcpy(ctx->fset->buf + (page_nr << PAGE_CACHE_SHIFT), kdata, copied);
	kunmap_atomic(kdata);
	end_page_writeback(page);

	unlock_page(page);
	/*
	 * We stop writing back only if we are not doing
	 * integrity sync. In case of integrity sync we have to
	 * keep going until we have written all the pages
	 * we tagged for writeback prior to entering this loop.
	 */
	if (--wbc->nr_to_write <= 0 && wbc->sync_mode == WB_SYNC_NONE)
		ctx->done = 1;
 exit:
	return copied;
}

static int send_buffer(off_t offset, int len, struct flush_context *ctx)
{
	int ret = 0;
	struct kvec kvec;
	struct iov_iter iter;
	struct v9fs_inode *v9inode = V9FS_I(ctx->mapping->host);

	kvec.iov_base = ctx->fset->buf;
	kvec.iov_len = len;
	iov_iter_kvec(&iter, WRITE | ITER_KVEC, &kvec, 1, len);
	BUG_ON(!v9inode->writeback_fid);

	p9_client_write(v9inode->writeback_fid, offset, &iter, &ret);
	return ret;
}

/**
 * Helper function for managing 9pFS write requests.
 * The main purpose of this function is to provide support for
 * the coalescing of several pages into a single 9p message.
 * This is similarly to NFS's pagelist.
 *
 * Copy pages with adjusent indices to a buffer and send it to
 * the server.
 *
 * @pages: array of pages with ascending indices;
 * @nr_pages: number of pages in the array;
 */
static int flush_pages(struct page **pages, int nr_pages,
		       struct flush_context *ctx)
{
	int ret;
	int pos = 0;
	int iter_pos;
	int iter_nrpages;
	pgoff_t iter_page_idx;

	while (pos < nr_pages) {

		int i;
		int iter_len = 0;
		struct page *page;

		iter_pos = pos;
		iter_nrpages = 0;
		iter_page_idx = pages[pos]->index;

		for (i = 0; pos < nr_pages; i++) {
			int from_page;

			page = pages[pos];
			if (page->index != iter_page_idx + i) {
				/*
				 * Hole in the indices,
				 * further coalesce impossible.
				 * Try to send what we have accumulated.
				 * This page will be processed in the next
				 * iteration
				 */
				goto iter_send;
			}
			from_page = flush_page(page, i, ctx);

			iter_len += from_page;
			iter_nrpages++;
			pos++;

			if (from_page != PAGE_CACHE_SIZE) {
				/*
				 * Not full page was flushed,
				 * further coalesce impossible.
				 * Try to send what we have accumulated.
				 */
#if V9FS_WRITEPAGES_DEBUG
				if (from_page == 0)
					pr_info("9p: page %lu is not flushed (%s)\n",
							page->index, ctx->msg);
#endif
				goto iter_send;
			}
		}
iter_send:
		if (iter_len == 0)
			/*
			 * Nothing to send
			 */
			goto next_iter;

		ret = send_buffer(iter_page_idx << PAGE_CACHE_SHIFT,
				  iter_len, ctx);
		if (ret == -EAGAIN) {
			redirty_pages_for_writeback(pages + iter_pos,
						    iter_nrpages, ctx->wbc);
		} else if (ret < 0) {
			/*
			 * Something bad happened.
			 * done_index is set past this chunk,
			 * so media errors will not choke
			 * background writeout for the entire
			 * file.
			 */
			pr_warn("9p: send_buffer failed (%d)\n", ret);

			set_pages_error(pages + iter_pos, iter_nrpages, ret);
			ctx->done_index =
				pages[iter_pos + iter_nrpages - 1]->index + 1;
			ctx->done = 1;
			return ret;
		}

		ret = 0;

next_iter:
		if (ctx->done)
			return ret;
	}
	return 0;
}

static void init_flush_context(struct flush_context *ctx,
			       struct address_space *mapping,
			       struct writeback_control *wbc,
			       struct v9fs_flush_set *fset)
{
	ctx->wbc = wbc;
	ctx->mapping = mapping;
	ctx->fset = fset;
	ctx->done = 0;
	ctx->range_whole = 0;

	if (wbc->range_cyclic) {
		ctx->writeback_index = mapping->writeback_index;
		ctx->index = ctx->writeback_index;
		if (ctx->index == 0)
			ctx->cycled = 1;
		else
			ctx->cycled = 0;
		ctx->end = -1;
	} else {
		ctx->index = wbc->range_start >> PAGE_CACHE_SHIFT;
		ctx->end = wbc->range_end >> PAGE_CACHE_SHIFT;
		if (wbc->range_start == 0 && wbc->range_end == LLONG_MAX)
			ctx->range_whole = 1;
		ctx->cycled = 1; /* ignore range_cyclic tests */
	}
}

/**
 * Pre-condition: flush set is locked
 */
static int v9fs_writepages_fastpath(struct address_space *mapping,
				    struct writeback_control *wbc,
				    struct v9fs_flush_set *fset)
{
	int ret = 0;
	int tag;
	int nr_pages;
	struct page **pages = fset->pages;
	struct flush_context ctx;

	init_flush_context(&ctx, mapping, wbc, fset);

	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag = PAGECACHE_TAG_TOWRITE;
	else
		tag = PAGECACHE_TAG_DIRTY;
retry:
	if (wbc->sync_mode == WB_SYNC_ALL || wbc->tagged_writepages)
		tag_pages_for_writeback(mapping, ctx.index, ctx.end);

	ctx.done_index = ctx.index;

	while (!ctx.done && (ctx.index <= ctx.end)) {
		int i;

		nr_pages = find_get_pages_tag(mapping, &ctx.index, tag,
					      1 + min(ctx.end - ctx.index,
					      (pgoff_t)(fset->num_pages - 1)),
					      pages);
		if (nr_pages == 0)
			break;

		ret = flush_pages(pages, nr_pages, &ctx);
		/*
		 * unpin pages
		 */
		for (i = 0; i < nr_pages; i++)
			page_cache_release(pages[i]);
		if (ret < 0)
			break;
		cond_resched();
	}
	if (!ctx.cycled && !ctx.done) {
		/*
		 * range_cyclic:
		 * We hit the last page and there is more work
		 * to be done: wrap back to the start of the file
		 */
		ctx.cycled = 1;
		ctx.index = 0;
		ctx.end = ctx.writeback_index - 1;
		goto retry;
	}
	if (wbc->range_cyclic || (ctx.range_whole && wbc->nr_to_write > 0))
		mapping->writeback_index = ctx.done_index;
	return ret;
}

static int v9fs_writepages(struct address_space *mapping,
			   struct writeback_control *wbc)
{
	int ret;
	struct v9fs_flush_set *fset;

	fset = v9fs_inode2v9ses(mapping->host)->flush;
	if (!fset || !spin_trylock_flush_set(fset))
		/*
		 * do it by slow way
		 */
		return generic_writepages(mapping, wbc);

	ret = v9fs_writepages_fastpath(mapping, wbc, fset);
	spin_unlock_flush_set(fset);
	return ret;
}
#endif /* CONFIG_KVM_GUEST_MICROVM */

/**
 * v9fs_launder_page - Writeback a dirty page
 * Returns 0 on success.
 */

static int v9fs_launder_page(struct page *page)
{
	int retval;
	struct inode *inode = page->mapping->host;

	v9fs_fscache_wait_on_page_write(inode, page);
	if (clear_page_dirty_for_io(page)) {
		retval = v9fs_vfs_writepage_locked(page);
		if (retval)
			return retval;
	}
	return 0;
}

/**
 * v9fs_direct_IO - 9P address space operation for direct I/O
 * @iocb: target I/O control block
 * @pos: offset in file to begin the operation
 *
 * The presence of v9fs_direct_IO() in the address space ops vector
 * allowes open() O_DIRECT flags which would have failed otherwise.
 *
 * In the non-cached mode, we shunt off direct read and write requests before
 * the VFS gets them, so this method should never be called.
 *
 * Direct IO is not 'yet' supported in the cached mode. Hence when
 * this routine is called through generic_file_aio_read(), the read/write fails
 * with an error.
 *
 */
static ssize_t
v9fs_direct_IO(struct kiocb *iocb, struct iov_iter *iter, loff_t pos)
{
	struct file *file = iocb->ki_filp;
	ssize_t n;
	int err = 0;
	if (iov_iter_rw(iter) == WRITE) {
		n = p9_client_write(file->private_data, pos, iter, &err);
		if (n) {
			struct inode *inode = file_inode(file);
			loff_t i_size = i_size_read(inode);
			if (pos + n > i_size)
				inode_add_bytes(inode, pos + n - i_size);
		}
	} else {
		n = p9_client_read(file->private_data, pos, iter, &err);
	}
	return n ? n : err;
}

static int v9fs_write_begin(struct file *filp, struct address_space *mapping,
			    loff_t pos, unsigned len, unsigned flags,
			    struct page **pagep, void **fsdata)
{
	int retval = 0;
	struct page *page;
	struct v9fs_inode *v9inode;
	pgoff_t index = pos >> PAGE_CACHE_SHIFT;
	struct inode *inode = mapping->host;


	p9_debug(P9_DEBUG_VFS, "filp %p, mapping %p\n", filp, mapping);

	v9inode = V9FS_I(inode);
start:
	page = grab_cache_page_write_begin(mapping, index, flags);
	if (!page) {
		retval = -ENOMEM;
		goto out;
	}
	BUG_ON(!v9inode->writeback_fid);
	if (PageUptodate(page))
		goto out;

	if (len == PAGE_CACHE_SIZE)
		goto out;

	retval = v9fs_fid_readpage(v9inode->writeback_fid, page);
	page_cache_release(page);
	if (!retval)
		goto start;
out:
	*pagep = page;
	return retval;
}

static int v9fs_write_end(struct file *filp, struct address_space *mapping,
			  loff_t pos, unsigned len, unsigned copied,
			  struct page *page, void *fsdata)
{
	loff_t last_pos = pos + copied;
	struct inode *inode = page->mapping->host;

	p9_debug(P9_DEBUG_VFS, "filp %p, mapping %p\n", filp, mapping);

	if (unlikely(copied < len)) {
		/*
		 * zero out the rest of the area
		 */
		unsigned from = pos & (PAGE_CACHE_SIZE - 1);

		zero_user(page, from + copied, len - copied);
		flush_dcache_page(page);
	}

	if (!PageUptodate(page))
		SetPageUptodate(page);
	/*
	 * No need to use i_size_read() here, the i_size
	 * cannot change under us because we hold the i_mutex.
	 */
	if (last_pos > inode->i_size) {
		inode_add_bytes(inode, last_pos - inode->i_size);
		i_size_write(inode, last_pos);
	}
	set_page_dirty(page);
	unlock_page(page);
	page_cache_release(page);

	return copied;
}


const struct address_space_operations v9fs_addr_operations = {
	.readpage = v9fs_vfs_readpage,
	.readpages = v9fs_vfs_readpages,
	.set_page_dirty = __set_page_dirty_nobuffers,
	.writepage = v9fs_vfs_writepage,
#if defined(CONFIG_KVM_GUEST_MICROVM)
	.writepages = v9fs_writepages,
#endif /* CONFIG_KVM_GUEST_MICROVM */
	.write_begin = v9fs_write_begin,
	.write_end = v9fs_write_end,
	.releasepage = v9fs_release_page,
	.invalidatepage = v9fs_invalidate_page,
	.launder_page = v9fs_launder_page,
	.direct_IO = v9fs_direct_IO,
};
