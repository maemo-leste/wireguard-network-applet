/*
 * Copyright (c) 2020-2021 Ivan J. <parazyd@dyne.org>
 *               2014-2019 Hiltjo Posthuma <hiltjo@codemadness.org>
 *
 * This file is part of wireguard-network-applet
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>

int pipe_rw(int fd_in, int fd_out, char *writestr, char **dest)
{
	char buf[PIPE_BUF];
	struct timeval tv;
	fd_set fdr, fdw;
	size_t total = 0;
	ssize_t r;
	int maxfd, status = -1, haswritten = 0;

	if (fd_out == -1 || writestr == NULL)
		haswritten = 1;

	while (1) {
		FD_ZERO(&fdr);
		FD_ZERO(&fdw);
		if (haswritten) {
			if (fd_in == -1)
				break;
			FD_SET(fd_in, &fdr);
			maxfd = fd_in;
		} else {
			FD_SET(fd_out, &fdw);
			maxfd = fd_out;
		}

		memset(&tv, 0, sizeof(tv));
		tv.tv_usec = 50000;	/* 50 ms */

		if ((r = select(maxfd + 1, haswritten ? &fdr : NULL,
				haswritten ? NULL : &fdw, NULL, &tv)) == -1) {
			if (errno != EINTR)
				goto fini;
		} else if (!r) {	/* timeout */
			continue;
		}

		if (fd_out != -1 && FD_ISSET(fd_out, &fdw)) {
			if (write(fd_out, writestr, strlen(writestr)) == -1) {
				if (errno == EWOULDBLOCK || errno == EAGAIN
				    || errno == EINTR)
					continue;
				else if (errno == EPIPE)
					goto fini;
				goto fini;
			}

			if (fd_out > 2)
				close(fd_out);	/* sends EOF */
			fd_out = -1;
			haswritten = 1;
		}

		if (haswritten && fd_in != -1 && FD_ISSET(fd_in, &fdr)) {
			r = read(fd_in, buf, sizeof(buf));
			if (r == -1) {
				if (errno == EWOULDBLOCK || errno == EAGAIN)
					continue;
				goto fini;
			}
			if (r > 0) {
				buf[r] = '\0';
				total += (size_t)r;
				*dest = malloc(total + 1);
				*dest = strncpy(*dest, buf, total + 1);
			} else if (!r) {
				status = 0;
				goto fini;
			}
		}
	}

 fini:
	if (fd_in != -1 && fd_in > 2)
		close(fd_in);
	if (fd_out != -1 && fd_out > 2)
		close(fd_out);
	return status;
}

int pipe_cmd(char *cmd[], char *writestr, char **dest)
{
	struct sigaction sa;
	pid_t pid;
	int pc[2], cp[2];

	if (pipe(pc) == -1 || pipe(cp) == -1) {
		g_critical("pipe: %s", g_strerror(errno));
		return -1;
	}

	pid = fork();
	if (pid == -1) {
		g_critical("fork: %s", g_strerror(errno));
		return -1;
	} else if (pid == 0) {
		/* child */
		close(cp[0]);
		close(pc[1]);

		if (dup2(pc[0], 0) == -1 || dup2(cp[1], 1) == -1) {
			g_critical("dup2: %s", g_strerror(errno));
			return -1;
		}

		if (execv(cmd[0], (char **)cmd) == -1) {
			g_critical("execv: %s", g_strerror(errno));
			_exit(1);	/* NOTE: must be _exit */
		}
		_exit(0);
	} else {
		/* parent */
		close(pc[0]);
		close(cp[1]);

		/* ignore SIGPIPE, we handle this for write(). */
		memset(&sa, 0, sizeof(sa));
		sa.sa_flags = SA_RESTART;
		sa.sa_handler = SIG_IGN;
		sigaction(SIGPIPE, &sa, NULL);

		if (pipe_rw(cp[0], pc[1], writestr, dest) == -1)
			return -1;
	}

	return 0;
}
