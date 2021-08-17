/*
 * Copyright (c) 2021 Ivan J. <parazyd@dyne.org>
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
#ifndef __CONFIGURATION_H__
#define __CONFIGURATION_H__

/*
#define str(x) #x
#define xstr(x) str(x)
*/

#define GC_WIREGUARD "/system/maemo/wireguard"
#define GC_WIREGUARD_ACTIVE GC_WIREGUARD"/active_config"

#define GC_CFG_TUNNELENABLED "systemtunnel-enabled"
#define GC_CFG_PRIVATEKEY    "PrivateKey"
#define GC_CFG_ADDRESS       "Address"
#define GC_CFG_LISTENPORT    "ListenPort"

#define GC_PEER_PUBLICKEY    "PublicKey"
#define GC_PEER_ALLOWEDIPS   "AllowedIPs"
#define GC_PEER_ENDPOINT     "EndPoint"

#endif
