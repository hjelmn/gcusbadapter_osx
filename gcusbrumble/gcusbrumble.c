/* -*- Mode: C; indent-tabs-mode:nil; c-basic-offset:4 -*- */
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

#include <ForceFeedback/IOForceFeedbackLib.h>
#include <IOKit/IOCFPlugin.h>
#include <IOKit/hid/IOHIDLib.h>

#include "gcusbrumble.h"
#include "gcusbrumblePriv.h"

#define gcusbrumble_major   1
#define gcusbrumble_minor   0
#define gcusbrumble_stage   0
#define gcusbrumble_rev     0

#define GCRumbleDebug(device, format, ...) \
    do { \
        if (device->debug) { \
            fprintf (stderr, format, ##__VA_ARGS__); \
        } \
    } while (0)


#define gcusbrumbleUUID CFUUIDGetConstantUUIDWithBytes(kCFAllocatorSystemDefault, \
0x6b, 0x8b, 0x24, 0x7d, 0xa6, 0x37, 0x4e, 0x36, 0xb5, 0x8d, 0x4a, 0x2e, 0x57, 0x3c, 0xf9, 0xf4);

static HRESULT gcusbrumble_stop_effect (void *self, UInt32 downloadID);

static void gcusb_set_rumble (IOHIDDeviceInterface121 **object, int value) {
    uint8_t report[2] = {0x60, (uint8_t) value};

    (*object)->setReport (object, kIOHIDReportTypeOutput, 0x60, report, 2, 0, NULL, NULL, NULL);
}

static void gcusbrumble_destroy_timer (gcusbrumble_effect_t *effect) {
    if (effect->timer) {
        CFRunLoopTimerInvalidate(effect->timer);
        CFRelease(effect->timer);
        effect->timer = NULL;
    }
}

static void gcusbrumble_free (gcusbrumble_t **rumble) {
    if (*rumble) {
        gcusbrumble_destroy_timer ((*rumble)->effects);
        if ((*rumble)->factory_id) {
            CFPlugInRemoveInstanceForFactory((*rumble)->factory_id);
            CFRelease((*rumble)->factory_id);
        }

        free (*rumble);
        *rumble = NULL;
    }
}

static HRESULT gcusbrumble_get_version (void *self, ForceFeedbackVersion *version) {
    version->apiVersion.majorRev = kFFPlugInAPIMajorRev;
    version->apiVersion.minorAndBugRev = kFFPlugInAPIMinorAndBugRev;
    version->apiVersion.stage = kFFPlugInAPIStage;
    version->apiVersion.nonRelRev = kFFPlugInAPINonRelRev;
    version->plugInVersion.majorRev = gcusbrumble_major;
    version->plugInVersion.minorAndBugRev = gcusbrumble_minor;
    version->plugInVersion.stage = gcusbrumble_stage;
    version->plugInVersion.nonRelRev = gcusbrumble_rev;

    return S_OK;
}

static HRESULT gcusbrumble_query (void *self, REFIID iid, LPVOID *ppv) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    CFUUIDRef interface;

    GCRumbleDebug(rumble, "Query called for rumble %p\n", rumble);

    interface = CFUUIDCreateFromUUIDBytes(kCFAllocatorDefault, iid);

    if (CFEqual (interface, kIOForceFeedbackDeviceInterfaceID)) {
        *ppv = &rumble->device_interface;
    } else if (CFEqual(interface, IUnknownUUID) || CFEqual(interface, kIOCFPlugInInterfaceID)) {
        *ppv = &rumble->plugin_interface;
    } else {
        *ppv = NULL;
    }

    CFRelease(interface);

    if (*ppv == NULL) {
        return E_NOINTERFACE;
    }

    ++rumble->ref_cnt;

    return S_OK;
}

static ULONG gcusbrumble_add_ref (void *self) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    GCRumbleDebug(rumble, "Add ref called for rumble %p\n", rumble);
    return ++rumble->ref_cnt;
}

static ULONG gcusbrumble_release (void *self) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    GCRumbleDebug(rumble, "Release called for rumble %p\n", rumble);

    if (!--rumble->ref_cnt) {
        gcusbrumble_free (&rumble);
        return 0;
    }

    return rumble->ref_cnt;
}

HRESULT	gcusbrumble_initialize_terminate (void *self, NumVersion forceFeedbackAPIVersion, io_object_t hidDevice, boolean_t begin) {
    IOCFPlugInInterface **plugInInterface = NULL;
    IOReturn kresult;
    SInt32 score = 0;

    gcusbrumble_t *rumble = GCRUMBLE(self);
    GCRumbleDebug(rumble, "Initialize called for rumble %p, hidDevice %p, begin %d\n", rumble, (void *)(intptr_t)hidDevice, begin);

    if (!begin) {
        if (rumble->adapter_port) {
            (*rumble->adapter_port)->close (rumble->adapter_port);
            (*rumble->adapter_port)->Release (rumble->adapter_port);
            rumble->adapter_port = NULL;
        }
        return FF_OK;
    }

    if (!IOObjectConformsTo(hidDevice, "GCUSBAdapterPort")) {
        return FFERR_INVALIDPARAM;
    }

    kresult = IOCreatePlugInInterfaceForService (hidDevice, kIOHIDDeviceUserClientTypeID, kIOCFPlugInInterfaceID, &plugInInterface, &score);
    if (kresult != kIOReturnSuccess) {
        return FFERR_NOINTERFACE;
    }

    kresult = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOHIDDeviceInterfaceID121), (LPVOID) &rumble->adapter_port);
    (*plugInInterface)->Release(plugInInterface);

    if (kresult != kIOReturnSuccess) {
        return FFERR_NOINTERFACE;
    }

    (*rumble->adapter_port)->open(rumble->adapter_port, 0);

    GCRumbleDebug(rumble, "Opening adapter port %p\n", rumble->adapter_port);

    return FF_OK;
}

static HRESULT gcusbrumble_destroy_effect (void *self, FFEffectDownloadID downloadID) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    GCRumbleDebug(rumble, "Destroy Effect called for rumble %p, downloadID = %u\n", rumble, downloadID);

    if (1 != downloadID || ! rumble->effects[0].downloaded) {
        return FFERR_INVALIDPARAM;
    }

    rumble->effects[0].downloaded = 0;
    --rumble->num_effects_downloaded;

    gcusbrumble_destroy_timer (rumble->effects);

    return FF_OK;
}

static void gcusbrumble_timer (CFRunLoopTimerRef timer, void *info) {
    gcusbrumble_effect_t *effect = (gcusbrumble_effect_t *) info;
    gcusbrumble_t *rumble = (gcusbrumble_t *)((intptr_t) effect - offsetof (gcusbrumble_t, effects) - sizeof (*effect) * (effect->identifier - 1));
    fprintf (stderr, "Timer fired. left = %d\n", effect->iterations - 1);
    if (0 == --effect->iterations) {
        gcusbrumble_stop_effect(&rumble->device_interface.vtbl, 1);
    }
}

static HRESULT gcusbrumble_download_effect (void *self, CFUUIDRef effectType, FFEffectDownloadID *pDownloadID,
                                     FFEFFECT *pEffect, FFEffectParameterFlag flags) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    int effect_index = 0;

    if (rumble->debug) {
        CFUUIDBytes GCRUMBLE = CFUUIDGetUUIDBytes(effectType);

        GCRumbleDebug(rumble, "Download Effect called for rumble %p, pDownloadID = %u, flags = 0x%x\n", rumble, *pDownloadID, flags);
        GCRumbleDebug(rumble, "Effect flags: %x\n", pEffect->dwFlags);
        GCRumbleDebug(rumble, "Effect gain: %x\n", pEffect->dwGain);
        GCRumbleDebug(rumble, "Effect duration: %x\n", pEffect->dwDuration);
        GCRumbleDebug(rumble, "Effect sample period: %x\n", pEffect->dwSamplePeriod);
        GCRumbleDebug(rumble, "Effect trigger: %x\n", pEffect->dwTriggerButton);
        GCRumbleDebug(rumble, "Effect type: %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x\n",
                      GCRUMBLE.byte0, GCRUMBLE.byte1, GCRUMBLE.byte2, GCRUMBLE.byte3, GCRUMBLE.byte4, GCRUMBLE.byte5, GCRUMBLE.byte6, GCRUMBLE.byte7,
                      GCRUMBLE.byte8, GCRUMBLE.byte9,GCRUMBLE.byte10, GCRUMBLE.byte11, GCRUMBLE.byte12, GCRUMBLE.byte13, GCRUMBLE.byte14, GCRUMBLE.byte15);
    }

    if (!CFEqual(effectType, kFFEffectType_ConstantForce_ID) &&
        !CFEqual(effectType, kFFEffectType_Sine_ID) &&
        !CFEqual(effectType, kFFEffectType_Square_ID)) {
        return FFERR_UNSUPPORTED;
    }

    if (FFSFFC_PAUSE == rumble->state) {
        return FFERR_DEVICEPAUSED;
    }

    /* no need to allow more than one effect be downloaded at a time */
    if (rumble->effects[effect_index].downloaded && 0 == *pDownloadID) {
        return FFERR_OUTOFMEMORY;
    }

    *pDownloadID = 1;

    if (FF_INFINITE != pEffect->dwDuration) {
        /* duration is in us */
        CFAbsoluteTime interval = (float) pEffect->dwDuration * 0.000001f;
        CFRunLoopTimerContext timer_context = {.version = 0, .info = rumble->effects, .retain = NULL,
                                               .release = NULL, .copyDescription = NULL};

        rumble->effects[effect_index].timer = CFRunLoopTimerCreate(kCFAllocatorDefault, interval, interval, 0, 0,
                                                                   gcusbrumble_timer, &timer_context);
    }

    if (flags & FFEP_START) {
        gcusb_set_rumble (rumble->adapter_port, 1);
        rumble->effects[effect_index].status = FFEGES_PLAYING;
    }

    rumble->effects[effect_index].downloaded = 1;
    ++rumble->num_effects_downloaded;

    return FF_OK;
}

static HRESULT gcusbrumble_escape (void *self, FFEffectDownloadID downloadID, FFEFFESCAPE *pEscape) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    GCRumbleDebug(rumble, "Escape Effect called for rumble %p, pDownloadID = %u\n", rumble, downloadID);

    if (downloadID > 1) {
        return FFERR_INVALIDDOWNLOADID;
    }

    return FFERR_UNSUPPORTED;
}

static HRESULT gcusbrumble_get_effect_status (void *self, FFEffectDownloadID downloadID, FFEffectStatusFlag *pStatusCode) {
    gcusbrumble_t *rumble = GCRUMBLE(self);

    GCRumbleDebug(rumble, "Escape Status called for rumble %p, pDownloadID = %u\n", rumble, downloadID);

    if (1 != downloadID || !rumble->effects[downloadID - 1].downloaded) {
        return FFERR_INVALIDDOWNLOADID;
    }

    *pStatusCode = rumble->effects[downloadID - 1].status;

    return FF_OK;
}

static HRESULT gcusbrumble_get_force_feedback_capabilities (void * self, FFCAPABILITIES * pCapabilities) {
    gcusbrumble_t *rumble = GCRUMBLE(self);

    GCRumbleDebug(rumble, "Force feedback cap called for rumble %p\n", rumble);

    pCapabilities->ffSpecVer.majorRev = kFFPlugInAPIMajorRev;
    pCapabilities->ffSpecVer.minorAndBugRev = kFFPlugInAPIMinorAndBugRev;
    pCapabilities->ffSpecVer.stage = kFFPlugInAPIStage;
    pCapabilities->ffSpecVer.nonRelRev = kFFPlugInAPINonRelRev;
    /* probably can add more "supported" types */
    pCapabilities->supportedEffects = FFCAP_ET_CONSTANTFORCE | FFCAP_ET_SINE;
    pCapabilities->emulatedEffects = 0;
    pCapabilities->subType = FFCAP_ST_VIBRATION;
    pCapabilities->numFfAxes = 1;
    memset (pCapabilities->ffAxes, 0, sizeof (pCapabilities->ffAxes));
    pCapabilities->ffAxes[0] = FFJOFS_X;
    pCapabilities->storageCapacity = 1;
    pCapabilities->playbackCapacity = 1;
    pCapabilities->driverVer.majorRev = gcusbrumble_major;
    pCapabilities->driverVer.minorAndBugRev = gcusbrumble_minor;
    pCapabilities->driverVer.stage = gcusbrumble_stage;
    pCapabilities->driverVer.nonRelRev = gcusbrumble_rev;
    memset (&pCapabilities->hardwareVer, 0, sizeof(pCapabilities->hardwareVer));
    pCapabilities->hardwareVer.majorRev = 1;
    memset (&pCapabilities->firmwareVer, 0, sizeof(pCapabilities->hardwareVer));
    pCapabilities->firmwareVer.majorRev = 1;

    return FF_OK;
}

static HRESULT gcusbrumble_get_force_feedback_state (void *self, ForceFeedbackDeviceState *pDeviceState) {
    gcusbrumble_t *rumble = GCRUMBLE(self);

    pDeviceState->dwState = rumble->state;
    pDeviceState->dwLoad = rumble->num_effects_downloaded * 100;

    return FF_OK;
}

static HRESULT gcusbrumble_send_force_feedback_command (void *self, FFCommandFlag state) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    int rumble_state = 0;

    GCRumbleDebug(rumble, "Send command called for rumble %p, state %d\n", rumble, state);

    switch (state) {
    case FFSFFC_RESET:
            rumble->effects[0].downloaded = 0;
            rumble->state = FFGFFS_EMPTY;
            rumble->effects[0].status = FFEGES_NOTPLAYING;
            gcusbrumble_destroy_timer (rumble->effects);
            break;
    case FFSFFC_STOPALL:
            rumble->state = FFGFFS_STOPPED;
            rumble->effects[0].status = FFEGES_NOTPLAYING;
            break;
    case FFSFFC_SETACTUATORSOFF:
            rumble->state |= FFGFFS_ACTUATORSOFF;
            break;
    case FFSFFC_PAUSE:
            rumble->state = FFGFFS_PAUSED;
            break;
    case FFSFFC_SETACTUATORSON:
            rumble->state &= ~FFGFFS_ACTUATORSOFF;
            rumble->state |= FFGFFS_ACTUATORSON;
            if (FFEGES_PLAYING == rumble->effects[0].status) {
                rumble_state = 1;
            }
            break;
    case FFSFFC_CONTINUE:
            rumble->state &= ~FFGFFS_PAUSED;
            if (FFEGES_PLAYING == rumble->effects[0].status) {
                rumble_state = 1;
            }
            break;
    }

    gcusb_set_rumble (rumble->adapter_port, rumble_state);

    return FF_OK;
}

static HRESULT gcusbrumble_set_property (void *self, FFProperty property, void *pValue) {
    gcusbrumble_t *rumble = GCRUMBLE(self);

    GCRumbleDebug(rumble, "Set property called for rumble %p, property %d\n", rumble, property);

#pragma unused(pValue)
    return FFERR_UNSUPPORTED;
}

static HRESULT gcusbrumble_start_effect (void *self, FFEffectDownloadID downloadID, FFEffectStartFlag mode, UInt32 iterations) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    int rumble_index = downloadID - 1;

    GCRumbleDebug(rumble, "Start effect called for rumble %p, downloadID %d, mode %d, iterations %d\n", rumble, downloadID, mode, iterations);

    if (!(FFGFFS_PAUSED & rumble->state)) {
        gcusb_set_rumble (rumble->adapter_port, 1);
        rumble->effects[rumble_index].status = FFEGES_PLAYING;
        if (FF_INFINITE != iterations && rumble->effects[rumble_index].timer) {
            rumble->effects[rumble_index].iterations = iterations;
            CFRunLoopAddTimer(CFRunLoopGetCurrent(), rumble->effects[rumble_index].timer, kCFRunLoopCommonModes);
        }
    }

    return FF_OK;
}

static HRESULT gcusbrumble_stop_effect (void *self, UInt32 downloadID) {
    gcusbrumble_t *rumble = GCRUMBLE(self);
    int rumble_index = downloadID - 1;

    GCRumbleDebug(rumble, "Stop effect called for rumble %p, downloadID %d\n", rumble, downloadID);

    gcusb_set_rumble (rumble->adapter_port, 0);
    rumble->effects[rumble_index].status = FFEGES_NOTPLAYING;
    if (rumble->effects[rumble_index].timer) {
        CFRunLoopTimerInvalidate(rumble->effects[rumble_index].timer);
    }

    return FF_OK;
}

static IOForceFeedbackDeviceInterface gcusbrumble_device_interface = {
    /* IUNKNOWNCGUTS */
    .QueryInterface = gcusbrumble_query,
    .AddRef = gcusbrumble_add_ref,
    .Release = gcusbrumble_release,
    /* IOForceFeedbackDeviceInterface */
    .ForceFeedbackGetVersion = gcusbrumble_get_version,
    .InitializeTerminate = gcusbrumble_initialize_terminate,
    .DestroyEffect = gcusbrumble_destroy_effect,
    .DownloadEffect = gcusbrumble_download_effect,
    .Escape = gcusbrumble_escape,
    .GetEffectStatus = gcusbrumble_get_effect_status,
    .GetForceFeedbackCapabilities = gcusbrumble_get_force_feedback_capabilities,
    .GetForceFeedbackState = gcusbrumble_get_force_feedback_state,
    .SendForceFeedbackCommand = gcusbrumble_send_force_feedback_command,
    .SetProperty = gcusbrumble_set_property,
    .StartEffect = gcusbrumble_start_effect,
    .StopEffect = gcusbrumble_stop_effect,
};

static IOReturn gcusbrumble_probe (void *self, CFDictionaryRef propertyTable, io_service_t service, SInt32 * order) {
    if (0 != service && IOObjectConformsTo(service, "GCUSBAdapterPort")) {
        return S_OK;
    }

    return kIOReturnBadArgument;
}

static IOReturn gcusbrumble_start (void *self, CFDictionaryRef propertyTable, io_service_t service) {
    return S_OK;
}

static IOReturn gcusbrumble_stop (void *self) {
    return S_OK;
}

static IOCFPlugInInterface gcusbrumble_plugin_interface = {
    /* IUNKNOWNCGUTS */
    .QueryInterface = gcusbrumble_query,
    .AddRef = gcusbrumble_add_ref,
    .Release = gcusbrumble_release,
    .version = 1,
    .revision = 0,
    /* IOCFPlugInInterface */
    .Probe = gcusbrumble_probe,
    .Start = gcusbrumble_start,
    .Stop = gcusbrumble_stop,
};

static IOCFPlugInInterface **gcusbrumble_alloc (CFUUIDRef uuid) {
    CFBundleRef my_bundle = CFBundleGetMainBundle();
    CFNumberRef debug_level;
    gcusbrumble_t *new_plugin;
    char *tmp;

    new_plugin = (gcusbrumble_t *) calloc (1, sizeof (*new_plugin));

    new_plugin->device_interface.vtbl = (IUnknownVTbl *) &gcusbrumble_device_interface;
    new_plugin->device_interface.ctx = new_plugin;
    new_plugin->plugin_interface.vtbl = (IUnknownVTbl *) &gcusbrumble_plugin_interface;
    new_plugin->plugin_interface.ctx = new_plugin;
    new_plugin->factory_id = (CFUUIDRef) CFRetain(uuid);
    new_plugin->ref_cnt = 1;
    new_plugin->effects[0].identifier = 1;

    debug_level = CFBundleGetValueForInfoDictionaryKey(my_bundle, CFSTR("Debug"));
    if (debug_level) {
        CFNumberGetValue(debug_level, kCFNumberLongType, &new_plugin->debug);
    } else if (NULL != (tmp = getenv ("GCUSBRUMBLE_DEBUG"))) {
        new_plugin->debug = strtol (tmp, NULL, 0);
    }

    CFPlugInAddInstanceForFactory(uuid);

    GCRumbleDebug(new_plugin, "Setting debug level to %ld\n", new_plugin->debug);
    GCRumbleDebug(new_plugin, "New plugin allocated. returning %p\n", &new_plugin->plugin_interface.vtbl);

    return (IOCFPlugInInterface **) &new_plugin->plugin_interface.vtbl;
}

void *gcusbrumble_factory (CFAllocatorRef allocator, CFUUIDRef typeID) {
#pragma unused (allocator)
    if (CFEqual (typeID, kIOForceFeedbackLibTypeID)) {
        return gcusbrumble_alloc (typeID);
    }

    return NULL;
}
