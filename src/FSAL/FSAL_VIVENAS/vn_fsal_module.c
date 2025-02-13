// SPDX-License-Identifier: LGPL-3.0-or-later
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * -------------
 */

/* main.c
 * Module core functions
 */

#include "config.h"

#include "fsal.h"
#include <libgen.h>		/* used for 'dirname' */
#include <pthread.h>
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include "FSAL/fsal_init.h"
#include "vn_int.h"
#include "fsal_convert.h"
#include "../fsal_private.h"
#include "FSAL/fsal_commonlib.h"


/* MEM FSAL module private storage
 */

/* defined the set of attributes supported with POSIX */
#define MEM_SUPPORTED_ATTRIBUTES (ATTRS_POSIX)

static const char memname[] = "VIVENAS";

void 	__PfAof_init();
static void enable_flush_on_exit();

/* my module private storage */
struct fvn_fsal_module VIVENAS_MODULE = {
	.fsal = {
		.fs_info = {
			.maxfilesize = INT64_MAX,
			.maxlink = 0,
			.maxnamelen = MAXNAMLEN,
			.maxpathlen = MAXPATHLEN,
			.no_trunc = true,
			.chown_restricted = true,
			.case_insensitive = false,
			.case_preserving = true,
			.link_support = true,
			.symlink_support = true,
			.lock_support = true,
			.lock_support_async_block = false,
			.named_attr = false,
			.unique_handles = true,
			.acl_support = 0,
			.cansettime = true,
			.homogenous = true,
			.supported_attrs = MEM_SUPPORTED_ATTRIBUTES,
			.maxread = FSAL_MAXIOSIZE,
			.maxwrite = FSAL_MAXIOSIZE,
			.umask = 0,
			.auth_exportpath_xdev = false,
			.link_supports_permission_checks = false,
			.readdir_plus = true,
			.expire_time_parent = -1,
		}
	}
};

static struct config_item fvn_items[] = {
	CONF_ITEM_UI32("Inode_Size", 0, 0x200000, 0,
		       fvn_fsal_module, inode_size),
	CONF_ITEM_UI32("Up_Test_Interval", 0, UINT32_MAX, 0,
		       fvn_fsal_module, up_interval),
	CONF_ITEM_UI32("Async_Threads", 0, 100, 0,
		       fvn_fsal_module, async_threads),
	CONF_ITEM_BOOL("Whence_is_name", false,
		       fvn_fsal_module, whence_is_name),
	CONFIG_EOL
};

static struct config_block fvn_block = {
	.dbus_interface_name = "org.ganesha.nfsd.config.fsal.vivenas",
	.blk_desc.name = "VIVENAS",
	.blk_desc.type = CONFIG_BLOCK,
	.blk_desc.u.blk.init = noop_conf_init,
	.blk_desc.u.blk.params = fvn_items,
	.blk_desc.u.blk.commit = noop_conf_commit
};

struct fridgethr *fvn_async_fridge;

/**
 * Initialize subsystem
 */
static fsal_status_t
fvn_async_pkginit(void)
{
	/* Return code from system calls */
	int code = 0;
	struct fridgethr_params frp;

	if (VIVENAS_MODULE.async_threads == 0) {
		/* Don't run async-threads */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	if (fvn_async_fridge) {
		/* Already initialized */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	memset(&frp, 0, sizeof(struct fridgethr_params));
	frp.thr_max = VIVENAS_MODULE.async_threads;
	frp.thr_min = 1;
	frp.flavor = fridgethr_flavor_worker;

	/* spawn MEM_ASYNC background thread */
	code = fridgethr_init(&fvn_async_fridge, "VIVENAS_ASYNC_fridge", &frp);
	if (code != 0) {
		LogMajor(COMPONENT_FSAL,
			 "Unable to initialize VIVENAS_ASYNC fridge, error code %d.",
			 code);
	}

	LogEvent(COMPONENT_FSAL,
		 "Initialized FSAL_VIVENAS async thread pool with %"
		 PRIu32" threads.",
		 VIVENAS_MODULE.async_threads);

	return posix2fsal_status(code);
}

/**
 * Shutdown subsystem
 *
 * @return FSAL status
 */
static fsal_status_t
fvn_async_pkgshutdown(void)
{
	if (!fvn_async_fridge) {
		/* Async wasn't configured */
		return fsalstat(ERR_FSAL_NO_ERROR, 0);
	}

	int rc = fridgethr_sync_command(fvn_async_fridge,
					fridgethr_comm_stop,
					120);

	if (rc == ETIMEDOUT) {
		LogMajor(COMPONENT_FSAL,
			 "Shutdown timed out, cancelling threads.");
		fridgethr_cancel(fvn_async_fridge);
	} else if (rc != 0) {
		LogMajor(COMPONENT_FSAL,
			 "Failed shutting down VIVENAS_ASYNC threads: %d", rc);
	}

	fridgethr_destroy(fvn_async_fridge);
	fvn_async_fridge = NULL;
	return posix2fsal_status(rc);
}

/* private helper for export object
 */

/* Initialize mem fs info */
static fsal_status_t fvn_init_config(struct fsal_module *fsal_hdl,
				 config_file_t config_struct,
				 struct config_error_type *err_type)
{
	struct fvn_fsal_module *fvn_me =
	    container_of(fsal_hdl, struct fvn_fsal_module, fsal);
	fsal_status_t status = {0, 0};

	LogFullDebug(COMPONENT_FSAL,
				 "Supported attributes default = 0x%" PRIx64,
				 fvn_me->fsal.fs_info.supported_attrs);

	/* if we have fsal specific params, do them here
	 * fsal_hdl->name is used to find the block containing the
	 * params.
	 */
	(void) load_config_from_parse(config_struct,
				      &fvn_block,
				      fvn_me,
				      true,
				      err_type);
	if (!config_error_is_harmless(err_type))
		return fsalstat(ERR_FSAL_INVAL, 0);

	//TODO: I don't know what's UP call
	/* Initialize UP calls */
	//status = fvn_up_pkginit();
	//if (FSAL_IS_ERROR(status)) {
	//	LogMajor(COMPONENT_FSAL,
	//		 "Failed to initialize FSAL_VIVENAS UP package %s",
	//		 fsal_err_txt(status));
	//	return status;
	//}

	/* Initialize ASYNC call back threads */
	status = fvn_async_pkginit();
	if (FSAL_IS_ERROR(status)) {
		LogMajor(COMPONENT_FSAL,
			 "Failed to initialize FSAL_VIVENAS ASYNC package %s",
			 fsal_err_txt(status));
		return status;
	}

	/* Set whence_is_name in fsinfo */
	fvn_me->fsal.fs_info.whence_is_name = fvn_me->whence_is_name;

	display_fsinfo(&fvn_me->fsal);
	LogFullDebug(COMPONENT_FSAL,
		     "Supported attributes constant = 0x%" PRIx64,
		     (uint64_t) MEM_SUPPORTED_ATTRIBUTES);
	LogDebug(COMPONENT_FSAL,
		 "FSAL INIT: Supported attributes mask = 0x%" PRIx64,
		 fvn_me->fsal.fs_info.supported_attrs);

	return fsalstat(ERR_FSAL_NO_ERROR, 0);
}

/* Module initialization.
 * Called by dlopen() to register the module
 * keep a private pointer to me in myself
 */

/* linkage to the exports and handle ops initializers
 */

/**
 * @brief Initialize and register the FSAL
 *
 * This function initializes the FSAL module handle.  It exists solely to
 * produce a properly constructed FSAL module handle.
 */

MODULE_INIT void init(void)
{
	int retval;
	struct fsal_module *myself = &VIVENAS_MODULE.fsal;
	__PfAof_init();

	retval = register_fsal(myself, memname, FSAL_MAJOR_VERSION,
			       FSAL_MINOR_VERSION, FSAL_ID_NO_PNFS);
	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"VIVENAS module failed to register.");
	}
	myself->m_ops.create_export = fvn_create_export;
	myself->m_ops.update_export = fvn_update_export;
	myself->m_ops.init_config = fvn_init_config;
	glist_init(&VIVENAS_MODULE.fvn_exports);
	VIVENAS_MODULE.next_inode = 0xc0ffee;

	/* Initialize the fsal_obj_handle ops for FSAL MEM */
	fvn_handle_ops_init(&VIVENAS_MODULE.handle_ops);
	enable_flush_on_exit();
}

//this function only called if exit() called explicitly.
//   e.g. SIGINT handler called exit() will trigger this function. if SIGINT handler not installed, this function also not called
MODULE_FINI void finish(void)
{
	int retval;

	S5LOG_INFO("VIVENAS module finishing...");
	struct glist_head* hi = NULL;
	struct glist_head* hn = NULL;

	glist_for_each_safe(hi, hn, &VIVENAS_MODULE.fsal.exports) {
		struct fsal_export* exp_hdl = container_of(hi, struct fsal_export, exports);
		struct fvn_fsal_export* myself;
		myself = container_of(exp_hdl, struct fvn_fsal_export, export);
		fsal_detach_export(exp_hdl->fsal, &exp_hdl->exports);
		//vn_umount(myself->mount_ctx);
		myself->mount_ctx = NULL;
		free_export_ops(exp_hdl);
	}


	/* Shutdown UP calls */
	fvn_up_pkgshutdown();

	/* Shutdown ASYNC threads */
	fvn_async_pkgshutdown();

	retval = unregister_fsal(&VIVENAS_MODULE.fsal);
	if (retval != 0) {
		LogCrit(COMPONENT_FSAL,
			"Unable to unload VIVENAS FSAL.  Dying with extreme prejudice.");
		//abort(); //where has changed fsal_module.refcount ?
	}
}



void vns5log(int level, const char* format, ...)
{
	static const char* stderr_log[] = { KRED "FATA" KNRM, KRED "ERRO" KNRM, KYEL "WARN" KNRM, KBLU "INFO" KNRM, KGRN "DEBU" KNRM };
	static const char** log_level_str = stderr_log;

	static __thread char buffer[2048];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);
	char time_buf[100];
	struct timespec tp;
	clock_gettime(CLOCK_REALTIME, &tp);

	strftime(time_buf, 100, "%Y-%m-%d %H:%M:%S", localtime(&tp.tv_sec));
	snprintf(&time_buf[strlen(time_buf)], 100, ".%03d", (int)(tp.tv_nsec / 1000000L));
	fprintf(stderr, "[%s %s]%s\n", log_level_str[level], time_buf, buffer);
	if (level == S5LOG_LEVEL_FATAL)
		exit(-1);
}



static void flush_all_fs()
{
	S5LOG_INFO("Flushing all FS... ");

	struct glist_head* hi = NULL;
	struct glist_head* hn = NULL;

	glist_for_each_safe(hi, hn, &VIVENAS_MODULE.fsal.exports) {
		struct fsal_export* exp_hdl = container_of(hi, struct fsal_export, exports);
		struct fvn_fsal_export* myself;
		myself = container_of(exp_hdl, struct fvn_fsal_export, export);
		struct ViveFsContext* ctx = myself->mount_ctx;
		vn_flush_fs(ctx);
	}





	//vn_umount(g_fs_ctx);
	//delete g_fs_ctx;
	//g_fs_ctx = NULL;
}
//Run ganesha.nfsd -h, will get following:
//----------------- Signals ----------------
//SIGHUP: Reload LOGand EXPORT config
//SIGTERM : Cleanly terminate the program


static sighandler_t old_int_handle = NULL;
static sighandler_t old_term_handle = NULL;

static void sigroutine(int signo)
{
	switch (signo)
	{
	case SIGTERM://ganesha exit on SIGTERM
		S5LOG_INFO("Receive signal SIGTERM.");
		flush_all_fs();
		if (old_term_handle)old_term_handle(signo);
		exit(1);

	case SIGINT:
		S5LOG_INFO("Receive signal SIGINT.");
		flush_all_fs();
		if (old_int_handle)old_int_handle(signo);
		exit(0);
	}
	return;
}
static void enable_flush_on_exit()
{
	static int inited = 0;
	if (!inited) {
		S5LOG_DEBUG("install signal handler for SIGINT");
		old_int_handle = signal(SIGINT, sigroutine);
		//old_term_handle = signal(SIGTERM, sigroutine);
		inited = 1;
	}
}