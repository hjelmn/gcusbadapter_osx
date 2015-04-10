/* -*- Mode: C++; indent-tabs-mode:nil; c-basic-offset:4 -*- */
/*
 * driver for WUP-028 GameCube USB adapter CFPlugIn Bundle
 * Copyright © 2015 Nathan Hjelm <hjelmn@cs.unm.edu>
 *
 * This bundle is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This bundle is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <CoreFoundation/CoreFoundation.h>

#pragma GCC visibility push(hidden)

struct gcusbrumble_interface_t {
    IUnknownVTbl *vtbl;
    void *ctx;
};
typedef struct gcusbrumble_interface_t gcusbrumble_interface_t;

struct gcusbrumble_effect_t {
    /** identifier of effect */
    FFEffectDownloadID identifier;

    /** effect is downloaded */
    bool downloaded;

    /* current effect status */
    FFEffectStatusFlag status;

    /** timer for periodic effects */
    CFRunLoopTimerRef timer;

    /** affect iterations */
    int iterations;
};
typedef struct gcusbrumble_effect_t gcusbrumble_effect_t;

struct gcusbrumble_t {
    /** ForceFeedback Plugin interface */
    gcusbrumble_interface_t device_interface;

    /** CFPlugIn interface */
    gcusbrumble_interface_t plugin_interface;

    /** UUID used to create this plugin */
    CFUUIDRef factory_id;

    /** number of references outstanding. the structure will be freed
     * when this reaches 0 */
    UInt32 ref_cnt;

    /** force feedback effects (only support 1 at a time) */
    gcusbrumble_effect_t effects[1];

    /** number of effects downloaded (max 1 at this time) */
    unsigned int num_effects_downloaded;

    /** adapter port in use */
    IOHIDDeviceInterface121 **adapter_port;

    /** force feedback state */
    int state;

    /** controls debug messages */
    long debug;
};
typedef struct gcusbrumble_t gcusbrumble_t;

/** convenience macro for recovering the gcrumble structure */
#define GCRUMBLE(X) ((X) ? (gcusbrumble_t *) (((gcusbrumble_interface_t *)(X))->ctx) : NULL)

#pragma GCC visibility pop
