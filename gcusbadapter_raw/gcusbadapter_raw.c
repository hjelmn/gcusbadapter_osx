//
//  gcusbadapter_raw.c
//  gcusbadapter_raw
//
//  Created by Nathan Hjelm on 4/10/15.
//  Copyright (c) 2015 None. All rights reserved.
//

#include <mach/mach_types.h>

kern_return_t gcusbadapter_raw_start(kmod_info_t * ki, void *d);
kern_return_t gcusbadapter_raw_stop(kmod_info_t *ki, void *d);

kern_return_t gcusbadapter_raw_start(kmod_info_t * ki, void *d)
{
    return KERN_SUCCESS;
}

kern_return_t gcusbadapter_raw_stop(kmod_info_t *ki, void *d)
{
    return KERN_SUCCESS;
}
