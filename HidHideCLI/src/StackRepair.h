// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// StackRepair.h
#pragma once

namespace HidHide
{
    // The driver stack filter attachment state of a device
    typedef enum
    {
        FilterAttachmentUnknown = 0, // The driver stack could not be evaluated (typically because the device is absent)
        FilterAttachmentMissing,     // The device is present but the filter driver isn't attached to its driver stack
        FilterAttachmentAttached     // The device is present and the filter driver is attached to its driver stack
    } FilterAttachmentState;

    // Determine the filter driver attachment state of the device with the given device instance path
    FilterAttachmentState FilterAttachment(_In_ std::wstring const& deviceInstancePath);

    // Rebuild the driver stack of the device with the given device instance path
    // This is the programmatic equivalent of a physical unplug/re-plug cycle: the device is disabled (which also forcibly releases any handles
    // held by applications) and enabled again, so that the class filter driver attachment is re-evaluated. Non-persistent disable semantics
    // are used so a failed or interrupted rebuild never survives a reboot. When the device itself doesn't regain the filter driver attachment
    // its parent is rebuilt as a fallback (a parent rebuild cascades into its children).
    // Returns the filter driver attachment state as observed after the rebuild settled
    FilterAttachmentState StackRebuild(_In_ std::wstring const& deviceInstancePath);
}
