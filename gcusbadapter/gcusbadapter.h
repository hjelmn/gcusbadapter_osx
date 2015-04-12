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

#if !defined(GCUSB_H)
#define GCUSB_H

#include <mach/mach_types.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/usb/IOUSBHIDDriver.h>

class GCUSBAdapterPort;

class GCUSBAdapter : public IOUSBHIDDriver {
    OSDeclareDefaultStructors(GCUSBAdapter);
public:
    virtual bool start (IOService *provider);
    virtual void stop(IOService *provider);
    virtual IOReturn handleReportWithTime (AbsoluteTime timeStamp, IOMemoryDescriptor *report,
                                           IOHIDReportType reportType, IOOptionBits options);
    virtual IOReturn getReport (IOMemoryDescriptor *report, IOHIDReportType reportType,
                                IOOptionBits options);

    IOReturn setRumble (int port, int data);
private:
    void cleanup (void);
    /* report 0x11 is the rumble report */
    uint8_t _rumble_data[5] = {0x11, 0x00, 0x00, 0x00, 0x00};
    IOBufferMemoryDescriptor *_vreports[4] = {nullptr, nullptr, nullptr, nullptr};
    IOBufferMemoryDescriptor *_rumble_descriptor = nullptr;
    GCUSBAdapterPort *_ports[4] = {nullptr, nullptr, nullptr, nullptr};
};

class GCUSBAdapterPort : public IOHIDDevice {
    OSDeclareDefaultStructors(GCUSBAdapterPort);
public:
    static GCUSBAdapterPort *withAdapter (GCUSBAdapter *adapter, int port);
    bool init (GCUSBAdapter *adapter, int port);

    virtual IOReturn newReportDescriptor(IOMemoryDescriptor ** desc) const;
    virtual IOReturn getReport (IOMemoryDescriptor *report, IOHIDReportType reportType,
                                IOOptionBits options);
    virtual IOReturn setReport (IOMemoryDescriptor *report, IOHIDReportType reportType,
                                IOOptionBits options);

    virtual OSString * 	newTransportString() const;
    virtual OSNumber * 	newVendorIDNumber() const;
    virtual OSNumber * 	newProductIDNumber() const;
    virtual OSNumber * 	newVersionNumber() const;
    virtual OSString * 	newManufacturerString() const;
    virtual OSString * 	newProductString() const;
    virtual OSString * 	newSerialNumberString() const;
    virtual OSNumber * 	newLocationIDNumber() const;
    virtual OSNumber *	newReportIntervalNumber() const;

private:
    GCUSBAdapter *_adapter;
    int _port, _rumble;
};


#endif
