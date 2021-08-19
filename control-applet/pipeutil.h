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
#ifndef __PIPEUTIL_H__
#define __PIPEUTIL_H__

int pipe_cmd(char *cmd[], char *writestr, char **dest);
int pipe_rw(int fd_in, int fd_out, char *writestr, char **dest);

#endif
