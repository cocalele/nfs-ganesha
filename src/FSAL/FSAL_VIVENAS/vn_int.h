/* SPDX-License-Identifier: LGPL-3.0-or-later */
/*
 * vim:shiftwidth=8:tabstop=8:
 *
 * Copyright 2017-2019 Red Hat, Inc.
 * Author: Daniel Gryniewicz  dang@redhat.com
 *
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * -------------
 */

/* MEM methods for handles
*/
#define VIVENAS 1
#include "avltree.h"
#include "gsh_list.h"
#ifdef USE_LTTNG
#include "gsh_lttng/fsal_mem.h"
#endif
#ifdef VIVENAS
#include "vivenas.h"
#endif

struct fvn_fsal_obj_handle;

enum async_types {
	MEM_INLINE,
	MEM_RANDOM_OR_INLINE,
	MEM_RANDOM,
	MEM_FIXED,
};

/**
 * MEM internal export
 */
struct fvn_fsal_export {
	/** Export this wraps */
	struct fsal_export export;
	/** The path for this export */
	char *export_path;
	/** Root object for this export */
	struct fvn_fsal_obj_handle *root_handle;
	/** Entry into list of exports */
	struct glist_head export_entry;
	/** Lock protecting mfe_objs */
	pthread_rwlock_t mfe_exp_lock;
	/** List of all the objects in this export */
	struct glist_head mfe_objs;
	/** Async delay */
	uint32_t async_delay;
	/** Async Stall delay */
	uint32_t async_stall_delay;
	/** Type of async */
	uint32_t async_type;
	/** rocksdb ptah */
	char* db_path;
	struct ViveFsContext* mount_ctx;
};

fsal_status_t fvn_lookup_path(struct fsal_export *exp_hdl,
				const char *path,
				struct fsal_obj_handle **handle,
				struct fsal_attrlist *attrs_out);

fsal_status_t fvn_create_handle(struct fsal_export *exp_hdl,
				  struct gsh_buffdesc *hdl_desc,
				  struct fsal_obj_handle **handle,
				  struct fsal_attrlist *attrs_out);

/*
 * MEM internal object handle
 */

#define V4_FH_OPAQUE_SIZE 58 /* Size of state_obj digest */
struct vn_fd {
	struct state_t state;
	/** The open and share mode etc. This MUST be first in every
	 *  file descriptor structure.
	 */
	fsal_openflags_t openflags;
	struct ViveFile* vf;
};

struct fvn_fsal_obj_handle {
	struct fsal_obj_handle obj_handle;
	struct fsal_attrlist attrs;
	uint64_t inode;
	struct ViveInode* vninode;
	char handle[V4_FH_OPAQUE_SIZE];
	union {
		struct {
			struct fvn_fsal_obj_handle *parent;
			struct avltree avl_name;
			struct avltree avl_index;
			uint32_t numkids;
			pthread_mutex_t dir_lock;
		} mh_dir;
		struct {
			struct fsal_share share;
			struct vn_fd fd;
		} mh_file;
		struct {
			object_file_type_t nodetype;
			fsal_dev_t dev;
		} mh_node;
		struct {
			char *link_contents;
		} mh_symlink;
	};
	struct glist_head dirents; /**< List of dirents pointing to obj */
	struct glist_head mfo_exp_entry; /**< Link into mfs_objs */
	struct fvn_fsal_export *mfo_exp; /**< Export owning object */
	char *m_name;	/**< Base name of obj, for debugging */
	uint32_t datasize;
	bool is_export;
	uint32_t refcount; /**< We persist handles, so we need a refcount */
	char data[0]; /* Allocated data */
};

/**
 * @brief Dirent for FSAL_MEM
 */
struct fvn_dirent {
	struct fvn_fsal_obj_handle *hdl; /**< Handle dirent points to */
	struct fvn_fsal_obj_handle *dir; /**< Dir containing dirent */
	const char *d_name;		 /**< Name of dirent */
	uint64_t d_index;		 /**< index in dir */
#ifndef VIVENAS_IGNORE
	struct avltree_node avl_n;	 /**< Entry in dir's avl_name tree */
	struct avltree_node avl_i;	 /**< Entry in dir's avl_index tree */
	struct glist_head dlist;	 /**< Entry in hdl's dirents list */
#endif
};

static inline bool fvn_unopenable_type(object_file_type_t type)
{
	if ((type == SOCKET_FILE) || (type == CHARACTER_FILE)
	    || (type == BLOCK_FILE)) {
		return true;
	} else {
		return false;
	}
}

void fvn_handle_ops_init(struct fsal_obj_ops *ops);

/* Internal MEM method linkage to export object
*/

fsal_status_t fvn_create_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				const struct fsal_up_vector *up_ops);

fsal_status_t fvn_update_export(struct fsal_module *fsal_hdl,
				void *parse_node,
				struct config_error_type *err_type,
				struct fsal_export *original,
				struct fsal_module *updated_super);

const char *str_async_type(uint32_t async_type);

#define fvn_free_handle(h) _fvn_free_handle(h, __func__, __LINE__)
/**
 * @brief Free a MEM handle
 *
 * @note mfe_exp_lock MUST be held for write
 * @param[in] hdl	Handle to free
 */
static inline void _fvn_free_handle(struct fvn_fsal_obj_handle *hdl,
				    const char *func, int line)
{
#ifdef USE_LTTNG
	tracepoint(fsalmem, fvn_free, func, line, hdl, hdl->m_name);
#endif

	glist_del(&hdl->mfo_exp_entry);
	hdl->mfo_exp = NULL;

	if (hdl->m_name != NULL) {
		gsh_free(hdl->m_name);
		hdl->m_name = NULL;
	}

	gsh_free(hdl);
}

void fvn_clean_export(struct fvn_fsal_obj_handle *root);
void fvn_clean_all_dirents(struct fvn_fsal_obj_handle *parent);

/**
 * @brief FSAL Module wrapper for MEM
 */
struct fvn_fsal_module {
	/** Module we're wrapping */
	struct fsal_module fsal;
	/** fsal_obj_handle ops vector */
	struct fsal_obj_ops handle_ops;
	/** List of MEM exports. TODO Locking when we care */
	struct glist_head fvn_exports;
	/** Config - size of data in inode */
	uint32_t inode_size;
	/** Config - Interval for UP call thread */
	uint32_t up_interval;
	/** Next unused inode */
	uint64_t next_inode;
	/** Config - number of async threads */
	uint32_t async_threads;
	/** Config - whether so use whence-is-name */
	bool whence_is_name;
};

/* ASYNC testing */
extern struct fridgethr *fvn_async_fridge;

/* UP testing */
fsal_status_t fvn_up_pkginit(void);
fsal_status_t fvn_up_pkgshutdown(void);

extern struct fvn_fsal_module MEM;
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KBLU  "\x1B[34m"
#define KMAG  "\x1B[35m"
#define KCYN  "\x1B[36m"
#define KWHT  "\x1B[37m"






void vns5log(int level, const char* format, ...);

#define S5LOG_LEVEL_FATAL 0
#define S5LOG_LEVEL_ERROR 1
#define S5LOG_LEVEL_WARN 2
#define S5LOG_LEVEL_INFO 3
#define S5LOG_LEVEL_DEBUG 4
/**
 * log the fatal type information.
 */
#define S5LOG_FATAL(fmt,args...)							\
vns5log(S5LOG_LEVEL_FATAL,  fmt "(%s:%d:%s) " , ##args, __FILE__ , __LINE__ , __FUNCTION__ )

 /**
  * log the error type information.
  */
#define S5LOG_ERROR(fmt,args...)							\
vns5log(S5LOG_LEVEL_ERROR,  fmt "(%s:%d:%s) " ,  ##args, __FILE__ , __LINE__ , __FUNCTION__ )

  /**
   * log the warn type information.
   */
#define S5LOG_WARN(fmt,args...)							\
vns5log(S5LOG_LEVEL_WARN,  fmt "(%s:%d:%s) " ,  ##args, __FILE__ , __LINE__ , __FUNCTION__ )


   /**
	* log the info type information.
	*/
#define S5LOG_INFO(fmt,args...)							\
vns5log(S5LOG_LEVEL_INFO,  fmt "(%s:%d:%s) " ,  ##args, __FILE__ , __LINE__ , __FUNCTION__ )


	/**
	 * log the debug type information.
	 */
#define S5LOG_DEBUG(fmt,args...)							\
vns5log(S5LOG_LEVEL_DEBUG,  fmt "(%s:%d:%s) " ,  ##args, __FILE__ , __LINE__ , __FUNCTION__ )
