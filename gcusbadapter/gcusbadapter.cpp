/* -*- Mode: C++; indent-tabs-mode:nil; c-basic-offset:4 -*- */
/*
 * driver for WUP-028 GameCube USB adapter
 * Copyright © 2015 Nathan Hjelm <hjelmn@cs.unm.edu>
 *
 * This driver is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This driver is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "gcusbadapter.h"

#define super IOUSBHIDDriver

OSDefineMetaClassAndStructors(GCUSBAdapter, super);

/* Report to inject for each WUP-028 port */
static uint8_t GCUSBAdapterInjectedDescriptor[] = {
    0x05, 0x01, /* USAGE_PAGE (Generic Desktop) */
    0x09, 0x05, /* USAGE (Game Pad) */

    /* Fake reports for Gamecube controllers */
    0xA1, 0x01,               /* COLLECTION (Application) */
        0xA1, 0x01,           /* COLLECTION (Application) */
            0x85, 0x50,       /* REPORT_ID (0x50) */
            0x05, 0x09,       /* USAGE_PAGE (Button) */
            0x19, 0x01,       /* USAGE_MINIMUM (Button 1) */
            0x29, 0x10,       /* USAGE_MAXIMUM (Button 16) */
            0x15, 0x00,       /* LOGICAL_MINIMUM (0) */
            0x25, 0x01,       /* LOGICAL_MAXIMUM (1) */
            0x75, 0x01,       /* REPORT_SIZE (1) */
            0x95, 0x10,       /* REPORT_COUNT (16) */
            0x81, 0x02,       /* INPUT (Data,Var,Abs) */
            0x05, 0x01,       /* USAGE_PAGE (Genertic Desktop) */
            0x09, 0x30,       /* USAGE (X) */
            0x09, 0x31,       /* USAGE (Y) */
            0x09, 0x33,       /* USAGE (Rx) */
            0x09, 0x34,       /* USAGE (Ry) */
            0x15, 0x9a,       /* LOGICAL_MINIMUM (-102) -- scaled from original */
            0x25, 0x66,       /* LOGICAL_MAXIMUM (102) -- scaled from original */
            0x75, 0x08,       /* REPORT_SIZE (8 bits) */
            0x95, 0x04,       /* REPORT_COUNT (4) */
            0x81, 0x02,       /* INPUT(Data,Var,Abs) */
            0x05, 0x01,       /* USAGE_PAGE (Genertic Desktop) */
            0x09, 0x32,       /* USAGE (Z) -- Left trigger */
            0x09, 0x35,       /* USAGE (Rz) -- Right trigger */
            0x15, 0x18,       /* LOGICAL_MINIMUM (0x18) -- Observed */
            0x26, 0xf0, 0x00, /* LOGICAL_MAXIMUM (0xf0) -- Observed */
            0x75, 0x08,       /* REPORT_SIZE (8 bits) */
            0x95, 0x02,       /* REPORT_COUNT (2) */
            0x81, 0x02,       /* INPUT(Data,Var,Abs) */
        0xC0,                 /* END_COLLECTION */
        0xA1, 0x01,           /* COLLECTION (Application) */
            0x85, 0x60,       /* REPORT_ID (0x50) */
            0x06, 0x00, 0xff, /* USAGE_PAGE (Vendor Defined) */
            0x09, 0x03,       /* USAGE (3) */
            0x75, 0x08,       /* REPORT_SIZE (8 bits) */
            0x95, 0x01,       /* REPORT_COUNT (1) */
            0x91, 0x02,       /* OUTPUT(Data,Var,Abs) */
        0xC0,                 /* END_COLLECTION */
    0xC0,                     /* END_COLLECTION */
};

bool GCUSBAdapter::start(IOService *provider) {
    bool ret = super::start (provider);

    if (!ret) {
        return false;
    }

    do {
        IOLog ("Starting GC Adpater Driver for vid: 0x%04x, pid: 0x%04x, location: 0x%08x\n", _device->GetVendorID(),
               _device->GetProductID(), _device->GetLocationID());

        setProperty("Product", "GameCube USB Adapter WUP-028");

        /* start reports from the device */
        unsigned char _payload[1] = {0x13};
        IOBufferMemoryDescriptor *startReport = IOBufferMemoryDescriptor::withBytes(_payload, 1, kIODirectionOut);
        if (!startReport) {
            break;
        }

        setReport(startReport, kIOHIDReportTypeOutput, 0);
        startReport->release ();

        /* Allocate buffers for each controller report */
        for (int i = 0 ; i < 4 ; ++i) {
            unsigned char report_id = 0x50;
            _vreports[i] = IOBufferMemoryDescriptor::withCapacity(9, kIODirectionIn);
            if (nullptr == _vreports[i]) {
                ret = false;
                break;
            }

            _vreports[i]->writeBytes(0, &report_id, 1);
        }

        if (!ret) {
            break;
        }

        /* initialize the rumble report */
        /* report 0x11 is the rumble report */
        _rumble_data[0] = 0x11;
        for (int i = 0 ; i < 4 ; ++i) {
            _rumble_data[1 + i] = 0;
        }

        /* Allocate a buffer for rumble reports */
        _rumble_descriptor = IOBufferMemoryDescriptor::withCapacity(6, kIODirectionIn);
        if (nullptr == _rumble_descriptor) {
            break;
        }

        return true;
    } while (0);

    cleanup();

    return false;
}

void GCUSBAdapter::cleanup (void) {
    for (int i = 0 ; i < 4 ; ++i) {
        if (_vreports[i]) {
            _vreports[i]->release();
            _vreports[i] = nullptr;
        }

        if (_ports[i]) {
            _ports[i]->terminate();
            _ports[i]->release ();
            _ports[i] = nullptr;
        }
    }

    if (_rumble_descriptor) {
        _rumble_descriptor->release ();
        _rumble_descriptor = nullptr;
    }
}

void GCUSBAdapter::stop(IOService *provider) {
    super::stop (provider);
    cleanup();
}

IOReturn GCUSBAdapter::setRumble(int port, int data) {
    /* update internal state and set the real rumble report */
    _rumble_data[port + 1] = data;

    _rumble_descriptor->writeBytes(0, _rumble_data, 5);
    return super::setReport(_rumble_descriptor, kIOHIDReportTypeOutput, kIOHIDOptionsTypeNone);
}

IOReturn GCUSBAdapter::getReport (IOMemoryDescriptor *report, IOHIDReportType reportType,
                                  IOOptionBits options) {
    return super::getReport(report, reportType, options);
}

/**
 * @brief Intercept the incoming 0x21 report and break it down into individual controller reports
 *
 * This function is responsible for breaking the Nintendo reports into reports for individual
 * controllers. There are other possible ways to handle the 37 byte controller reports but this
 * one seems to be logical.
 */
IOReturn GCUSBAdapter::handleReportWithTime (AbsoluteTime timeStamp, IOMemoryDescriptor *report,
                                             IOHIDReportType reportType, IOOptionBits options)
{
    uint8_t report_id, report_data[9];

    report->readBytes(0, &report_id, 1);

    if (0x21 == report_id && 37 == report->getLength()) {
        for (int i = 0; i < 4; ++i) {
            report->readBytes(1 + i * 9, report_data, 9);
            if (report_data[0]) {
                if (!_ports[i]) {
                    GCUSBAdapterPort *newPort = GCUSBAdapterPort::withAdapter(this, i);
                    if (!newPort) {
                        IOLog ("Could not create GCUSBAdapterPort for port %d\n", i);
                        continue;
                    }

                    if (!newPort->attach(this)) {
                        continue;
                    }

                    if (!newPort->start(this)) {
                        newPort->detach(this);
                        continue;
                    }

                    newPort->registerService(kIOServiceAsynchronous);
                    _ports[i] = newPort;
                }

                /* rescale analog sticks to elimitate bias. other sticks may
                 * have different biases */
                report_data[3] = (uint8_t)((int8_t)report_data[3] - 122);
                report_data[4] = (uint8_t)((int8_t)report_data[4] - 144);
                report_data[5] = (uint8_t)((int8_t)report_data[5] - 133);
                report_data[6] = (uint8_t)((int8_t)report_data[6] - 133);

                _vreports[i]->writeBytes(1, report_data + 1, 8);
                int ret = _ports[i]->handleReport(_vreports[i]);
                if (kIOReturnSuccess != ret) {
                    return ret;
                }
            } else if (_ports[i]) {
                _ports[i]->terminate();
                _ports[i]->release();
                _ports[i] = nullptr;
            }
        }
    }

    return super::handleReportWithTime(timeStamp, report, reportType, options);
}

/* ports */
#undef super
#define super IOHIDDevice

OSDefineMetaClassAndStructors(GCUSBAdapterPort, super);

GCUSBAdapterPort *GCUSBAdapterPort::withAdapter (GCUSBAdapter *adapter, int port) {
    GCUSBAdapterPort *newPort = new GCUSBAdapterPort;

    if (newPort && !newPort->init (adapter, port)) {
        newPort->release();
        return nullptr;
    }

    return newPort;
}

bool GCUSBAdapterPort::init (GCUSBAdapter *adapter, int port) {
    OSDictionary *plugin_types;

    if (!super::init()) {
        return false;
    }

    /* set properties */
    plugin_types = OSDictionary::withCapacity(1);
    if (!plugin_types) {
        return false;
    }

    /* add CFPlugIn for rumble support */
    plugin_types->setObject("f4545ce5-bf5b-11d6-a4bb-0003933e3e3e", OSString::withCString("gcusbadapter.kext/Contents/PlugIns/gcusbrumble.bundle"));
    setProperty("IOCFPlugInTypes", plugin_types);

    /* store the port in the registry entry */
    setProperty("Port", port, 32);

    _rumble = 0;
    _port = port;
    _adapter = adapter;

    return true;
}

IOReturn GCUSBAdapterPort::newReportDescriptor(IOMemoryDescriptor ** desc) const {
    *desc = IOBufferMemoryDescriptor::withBytes(GCUSBAdapterInjectedDescriptor, sizeof (GCUSBAdapterInjectedDescriptor), kIODirectionIn);

    return *desc ? kIOReturnSuccess : kIOReturnNoMemory;
}

IOReturn GCUSBAdapterPort::setReport (IOMemoryDescriptor *report, IOHIDReportType reportType,
                                      IOOptionBits options) {
    uint8_t report_data[2] = {0, 0};

    if (!_adapter) {
        return kIOReturnInvalid;
    }

    report->readBytes(0, report_data, 2);
    if (0x60 == report_data[0]) {
        _adapter->setRumble(_port, report_data[1]);
    }

    /* ignore all other input reports */
    return kIOReturnSuccess;
}

IOReturn GCUSBAdapterPort::getReport (IOMemoryDescriptor *report, IOHIDReportType reportType,
                                      IOOptionBits options) {
    /* pass through get reports */
    return _adapter ? _adapter->getReport(report, reportType, options) : kIOReturnInvalid;
}

OSString *GCUSBAdapterPort::newProductString() const {
    char product_name[64];
    snprintf (product_name, 64, "GameCube Controller %d", _port + 1);
    return OSString::withCString(product_name);
}

OSNumber *GCUSBAdapterPort::newLocationIDNumber() const {
    return _adapter ? _adapter->newLocationIDNumber() : nullptr;
}

OSString *GCUSBAdapterPort::newManufacturerString() const {
    return _adapter ? _adapter->newManufacturerString() : nullptr;
}

OSNumber *GCUSBAdapterPort::newProductIDNumber() const {
    return _adapter ? _adapter->newProductIDNumber() : nullptr;
}

OSString *GCUSBAdapterPort::newTransportString() const {
    return _adapter ? _adapter->newTransportString() : nullptr;
}

OSNumber *GCUSBAdapterPort::newReportIntervalNumber() const {
    return _adapter ? _adapter->newReportIntervalNumber() : nullptr;
}

OSString *GCUSBAdapterPort::newSerialNumberString() const {
    return _adapter ? _adapter->newSerialNumberString() : nullptr;
}

OSNumber *GCUSBAdapterPort::newVersionNumber() const {
    return _adapter ? _adapter->newVersionNumber() : nullptr;
}

OSNumber *GCUSBAdapterPort::newVendorIDNumber() const {
    return _adapter ? _adapter->newVendorIDNumber() : nullptr;
}

