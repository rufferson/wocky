/*
 * wocky-jingle-info-internal.h - internal types for WockyJingleInfo
 * Copyright © 2012 Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */
#if !defined (WOCKY_COMPILATION)
# error "This is an internal header."
#endif

#ifndef WOCKY_JINGLE_INFO_INTERNAL_H
#define WOCKY_JINGLE_INFO_INTERNAL_H

typedef enum {
    WOCKY_STUN_SERVER_USER_SPECIFIED,
    WOCKY_STUN_SERVER_DISCOVERED,
    WOCKY_STUN_SERVER_FALLBACK
} WockyStunServerSource;

#endif /* WOCKY_JINGLE_INFO_INTERNAL_H */
