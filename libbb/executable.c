/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2006 Gabriel Somlo <somlo at cmu.edu>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"
#include "busybox.h" /* for APPLET_IS_NOEXEC */

/* check if path points to an executable file;
 * return 1 if found;
 * return 0 otherwise;
 */
int FAST_FUNC file_is_executable(const char *name)
{
	struct stat s;
	return (!access(name, X_OK) && !stat(name, &s) && S_ISREG(s.st_mode));
}

/* search (*PATHp) for an executable file;
 * return allocated string containing full path if found;
 *  PATHp points to the component after the one where it was found
 *  (or NULL if found in last component),
 *  you may call find_executable again with this PATHp to continue
 * return NULL otherwise (PATHp is undefined)
 */
char* FAST_FUNC find_executable(const char *name, const char **PATHp)
{
	/* About empty components in $PATH:
	 * http://pubs.opengroup.org/onlinepubs/009695399/basedefs/xbd_chap08.html
	 * 8.3 Other Environment Variables - PATH
	 * A zero-length prefix is a legacy feature that indicates the current
	 * working directory. It appears as two adjacent colons ( "::" ), as an
	 * initial colon preceding the rest of the list, or as a trailing colon
	 * following the rest of the list.
	 */
	char *p = (char*) *PATHp;

	if (!p)
		return NULL;
	while (1) {
		const char *end = strchrnul(p, ':');
		int sz = end - p;

		if (sz != 0) {
			p = xasprintf("%.*s/%s", sz, p, name);
		} else {
			/* We have xxx::yyy in $PATH,
			 * it means "use current dir" */
			p = xstrdup(name);
// A bit of discrepancy wrt the path used if file is found here.
// bash 5.2.15 "type" returns "./NAME".
// GNU which v2.21 returns "/CUR/DIR/NAME".
// With -a, both skip over all colons: xxx::::yyy is the same as xxx::yyy,
// current dir is not tried the second time.
		}
		if (file_is_executable(p)) {
			*PATHp = (*end ? end+1 : NULL);
			return p;
		}
		free(p);
		if (*end == '\0')
			return NULL;
		p = (char *) end + 1;
	}
}

/* search $PATH for an executable file;
 * return 1 if found;
 * return 0 otherwise;
 */
int FAST_FUNC executable_exists(const char *name)
{
	const char *path = getenv("PATH");
	char *ret = find_executable(name, &path);
	free(ret);
	return ret != NULL;
}

int FAST_FUNC applet_execve(const char *name, char *const argv[], char *const envp[])
{
#if ENABLE_FEATURE_PREFER_APPLETS
	int applet = find_applet_by_name(name);
	if (applet >= 0) {
		/* NOMMU targets only support vfork(). 
		 * since vfork() requires the child to exec() or _exit() for the
		 * parent to resume, running applets with NOEXEC and vfork()
		 * may result in deadlocks, as exec() will never be called. */
		if (BB_MMU && (ENABLE_FEATURE_ALWAYS_NOEXEC || APPLET_IS_NOEXEC(applet))) {
			/* since run_noexec_applet_and_exit takes char **argv,
			 * we need to copy argv to a new heap-allocated array. */
			char **copied_argv = clone_string_array(argv);
			
			/* since exec will not be called, we need to manually
			 * reset some signal handlers. */
			reset_all_signals();

			/* since exec will not be called, we then need to close
			 * all FDs with FD_CLOEXEC manually. */
			close_cloexec_fds();

			/* if non-default environ was passed, replace environ */
			if (envp != environ) {
				clearenv();

				/* envp is NULL terminated. */
				while (*envp)
					putenv(*envp++);
			}

			/* this should never return. */
			run_noexec_applet_and_exit(applet, name, copied_argv);

			/* if this is reached, error out */
			errno = ENOEXEC;
			return -1;
		} else {
			/* applet has to be executed using an exec syscall */
			return execve(bb_busybox_exec_path, argv, envp);
		}
	}

	/* no matching applet was found */
	errno = ENOENT;
	return -1;
#else
	/* applets are not prefered */
	return -1;
#endif
}

int FAST_FUNC applet_execvpe(const char *name, char *const argv[], char *const envp[])
{
	/* we try calling bb_applet_execve with the given name. */
	int error = applet_execve(name, argv, envp);

	/* since this is function is supposed to emulate a PATH
	 * search, we try executing with the basename too. */
	if (error < 0)
		error = applet_execve(bb_basename(name), argv, envp);

	return error;
}

/* just like the real execve, but we might try to launch an applet named 'pathname' first */
int FAST_FUNC bb_execve(const char *pathname, char *const argv[], char *const envp[])
{
	if (applet_execve(pathname, argv, envp) < 0) {
#if ENABLE_FEATURE_FORCE_APPLETS
		/* no external programs are allowed, error out */
		errno = ENOENT;
		return -1;
#endif
	}

	/* fall back to execve */
	return execve(pathname, argv, envp);
}

/* just like bb_execve, but we keep the existing environment by passing environ */
int FAST_FUNC bb_execv(const char *pathname, char *const argv[])
{
	return bb_execve(pathname, argv, environ);
}


/* just like the real execvpe, but we might try to launch an applet named 'file' first */
int FAST_FUNC bb_execvpe(const char *file, char *const argv[], char *const envp[])
{
	if (applet_execvpe(file, argv, envp) < 0) {
#if ENABLE_FEATURE_FORCE_APPLETS
		/* no external programs are allowed, error out */
		errno = ENOENT;
		return -1;
#endif
	}

	/* fall back to execvpe */
	return execvpe(file, argv, envp);
}

/* just like bb_execvpe, but we keep the existing environment by passing environ */
int FAST_FUNC bb_execvp(const char *file, char *const argv[])
{
	return bb_execvpe(file, argv, environ);
}