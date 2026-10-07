// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// StackRepair.cpp
#include "stdafx.h"
#include "StackRepair.h"
#include "Utils.h"
#include "Logging.h"

namespace
{
    // The name of the filter driver as it appears on a device driver stack
    constexpr PCWSTR FilterDriverOnStack{ L"\\Driver\\HidHide" };

    // The delay allowed for a device stack to settle after a disable
    constexpr DWORD StackSettleDelayInMilliseconds{ 1500 };

    // The delay between subsequent device re-enumeration checks
    constexpr DWORD ReenumerationCheckDelayInMilliseconds{ 500 };

    // The maximum time allowed for a device to regain the filter driver attachment
    constexpr DWORD FilterAttachmentTimeoutInMilliseconds{ 24000 };

    // The number of subsequent checks returning a missing filter driver attachment before accepting it as the final result
    constexpr int StableFilterMissingChecks{ 4 };

    // The device instance path prefixes eligible for a parent rebuild
    constexpr PCWSTR ParentRebuildPrefixes[]{ L"USB\\", L"BTHENUM\\", L"BTHLE\\" };

    // Determine if the string starts with the given prefix
    bool StartsWith(_In_ std::wstring const& value, _In_ PCWSTR prefix)
    {
        return (0 == value.compare(0, ::wcslen(prefix), prefix));
    }

    // Read the driver stack of a present device
    // Returns false when the device isn't present or when its driver stack couldn't be read
    bool DriverStack(_In_ std::wstring const& deviceInstancePath, _Out_ std::vector<std::wstring>& drivers)
    {
        DEVINST devInst{};
        if (auto const result{ ::CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(deviceInstancePath.c_str()), CM_LOCATE_DEVNODE_NORMAL) }; (CR_SUCCESS != result)) return (false);

        DEVPROPTYPE devPropType{};
        ULONG needed{};
        if (auto const result{ ::CM_Get_DevNode_PropertyW(devInst, &DEVPKEY_Device_Stack, &devPropType, nullptr, &needed, 0) }; (CR_BUFFER_SMALL != result)) return (false);
        if (DEVPROP_TYPE_STRING_LIST != devPropType) return (false);

        std::vector<BYTE> buffer(needed);
        if (auto const result{ ::CM_Get_DevNode_PropertyW(devInst, &DEVPKEY_Device_Stack, &devPropType, buffer.data(), &needed, 0) }; (CR_SUCCESS != result)) return (false);

        auto const values{ reinterpret_cast<PCWSTR>(buffer.data()) };
        drivers = HidHide::MultiStringToStringList(std::vector<WCHAR>(values, values + (needed / sizeof(WCHAR))));
        return (true);
    }

    // Determine if the filter driver is attached
    bool FilterDriverAttached(_In_ std::vector<std::wstring> const& drivers)
    {
        return (std::end(drivers) != std::find(std::begin(drivers), std::end(drivers), FilterDriverOnStack));
    }

    // Get the device instance path of the parent of a present device
    // Returns an empty string when the parent couldn't be determined
    std::wstring ParentDeviceInstancePath(_In_ std::wstring const& deviceInstancePath)
    {
        DEVINST devInst{};
        if (auto const result{ ::CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(deviceInstancePath.c_str()), CM_LOCATE_DEVNODE_NORMAL) }; (CR_SUCCESS != result)) return (std::wstring{});

        DEVINST parent{};
        if (auto const result{ ::CM_Get_Parent(&parent, devInst, 0) }; (CR_SUCCESS != result)) return (std::wstring{});

        std::vector<WCHAR> buffer(MAX_DEVICE_ID_LEN + 1);
        if (auto const result{ ::CM_Get_Device_IDW(parent, buffer.data(), static_cast<ULONG>(buffer.size()), 0) }; (CR_SUCCESS != result)) return (std::wstring{});

        return (buffer.data());
    }

    // Determine if the device instance path provided is eligible for a parent rebuild
    bool ParentRebuildable(_In_ std::wstring const& parentDeviceInstancePath)
    {
        if (parentDeviceInstancePath.empty()) return (false);
        if (std::wstring::npos != parentDeviceInstancePath.find(L"ROOT_HUB")) return (false);
        for (auto const prefix : ParentRebuildPrefixes)
        {
            if (StartsWith(parentDeviceInstancePath, prefix)) return (true);
        }
        return (false);
    }

    // Cycle (disable and enable) a present device while forcing the teardown to not survive a reboot
    void CycleDevice(_In_ std::wstring const& deviceInstancePath)
    {
        DEVINST devInst{};
        if (auto const result{ ::CM_Locate_DevNodeW(&devInst, const_cast<DEVINSTID_W>(deviceInstancePath.c_str()), CM_LOCATE_DEVNODE_NORMAL) }; (CR_SUCCESS != result)) THROW_CONFIGRET(result);

        // Locate can resolve a malformed or empty identifier to an unrelated node, so confirm identity before touching anything
        std::vector<WCHAR> resolved(MAX_DEVICE_ID_LEN + 1);
        if (auto const result{ ::CM_Get_Device_IDW(devInst, resolved.data(), static_cast<ULONG>(resolved.size()), 0) }; (CR_SUCCESS != result)) THROW_CONFIGRET(result);
        if (0 != ::_wcsicmp(resolved.data(), deviceInstancePath.c_str())) THROW_CONFIGRET(CR_NO_SUCH_DEVNODE);

        // Disable; this tears down the device stack and therefore also forcibly releases handles held by consuming applications
        if (auto const result{ ::CM_Disable_DevNode(devInst, (CM_DISABLE_ABSOLUTE | CM_DISABLE_UI_NOT_OK)) }; (CR_SUCCESS != result)) THROW_CONFIGRET(result);

        // Allow the stack teardown to settle before the device is enabled again
        ::Sleep(StackSettleDelayInMilliseconds);

        // Enable again; the device re-enumerates and its filter driver attachment is re-evaluated
        if (auto const result{ ::CM_Enable_DevNode(devInst, 0) }; (CR_SUCCESS != result)) THROW_CONFIGRET(result);
    }

    // Wait for the filter driver attachment state of a present device to settle
    HidHide::FilterAttachmentState SettledFilterAttachment(_In_ std::wstring const& deviceInstancePath)
    {
        auto stableFilterMissing{ 0 };
        for (DWORD elapsed{}; (elapsed < FilterAttachmentTimeoutInMilliseconds); elapsed += ReenumerationCheckDelayInMilliseconds)
        {
            ::Sleep(ReenumerationCheckDelayInMilliseconds);
            auto const state{ HidHide::FilterAttachment(deviceInstancePath) };
            if (HidHide::FilterAttachmentAttached == state) return (state);
            if (HidHide::FilterAttachmentMissing == state)
            {
                if (++stableFilterMissing >= StableFilterMissingChecks) return (state);
            }
            else
            {
                stableFilterMissing = 0;
            }
        }
        return (HidHide::FilterAttachment(deviceInstancePath));
    }
}

namespace HidHide
{
    _Use_decl_annotations_
    FilterAttachmentState FilterAttachment(std::wstring const& deviceInstancePath)
    {
        TRACE_ALWAYS(L"");
        std::vector<std::wstring> drivers;
        if (!DriverStack(deviceInstancePath, drivers)) return (FilterAttachmentUnknown);
        return (FilterDriverAttached(drivers) ? FilterAttachmentAttached : FilterAttachmentMissing);
    }

    _Use_decl_annotations_
    FilterAttachmentState StackRebuild(std::wstring const& deviceInstancePath)
    {
        TRACE_ALWAYS(L"");

        // An empty or overlong identifier can never name a device
        if (deviceInstancePath.empty() || (deviceInstancePath.size() > MAX_DEVICE_ID_LEN)) return (FilterAttachmentUnknown);

        // Only present devices can be rebuilt
        if (FilterAttachmentUnknown == FilterAttachment(deviceInstancePath)) return (FilterAttachmentUnknown);

        // Rebuild the device itself first
        CycleDevice(deviceInstancePath);
        if (auto const state{ SettledFilterAttachment(deviceInstancePath) }; (FilterAttachmentAttached == state)) return (state);

        // The device regained its stack but not the filter driver attachment; try the parent as a parent rebuild cascades into its children
        auto const parentDeviceInstancePath{ ParentDeviceInstancePath(deviceInstancePath) };
        if (!ParentRebuildable(parentDeviceInstancePath)) return (FilterAttachment(deviceInstancePath));

        CycleDevice(parentDeviceInstancePath);
        return (SettledFilterAttachment(deviceInstancePath));
    }
}
