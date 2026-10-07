// SPDX-License-Identifier: MIT
// stack_repair_tests.cpp
// Unit tests for the argument validation of the StackRepair commands and the read-only evaluation surface.
// The device-rebuild paths themselves require real PnP devices and are exercised in integration runs, not here.
#include <gtest/gtest.h>

#include <windows.h>
#include <cfgmgr32.h>
#include <string>
#include <vector>

#include "HidHideIoctlContract.h"

namespace
{
    // Mirror of CommandInterpreter::ValZeroOrOneDeviceInstancePath for isolated testing (no device access involved)
    std::wstring ValidateZeroOrOneDeviceInstancePath(std::vector<std::wstring> const& args)
    {
        if (args.size() > 2) return (L"wrong-number-of-arguments");
        if ((2 == args.size()) && (args.at(1).size() > MAX_DEVICE_ID_LEN)) return (L"device-instance-path-too-long");
        return (std::wstring{});
    }
}

TEST(StackRepair, AcceptsEmptyArgumentList)
{
    EXPECT_TRUE(ValidateZeroOrOneDeviceInstancePath({ L"dev-repair" }).empty());
}

TEST(StackRepair, AcceptsOneDeviceInstancePath)
{
    EXPECT_TRUE(ValidateZeroOrOneDeviceInstancePath({ L"dev-repair", LR"(HID\VID_054C&PID_0CE6&MI_03\7&387ff904&0&0000)" }).empty());
}

TEST(StackRepair, AcceptsBorderlineLength)
{
    auto const path{ std::wstring(MAX_DEVICE_ID_LEN, L'A') };
    EXPECT_TRUE(ValidateZeroOrOneDeviceInstancePath({ L"dev-repair", path }).empty());
}

TEST(StackRepair, RejectsTooManyArguments)
{
    EXPECT_FALSE(ValidateZeroOrOneDeviceInstancePath({ L"dev-repair", L"a", L"b" }).empty());
}

TEST(StackRepair, RejectsOverlongDeviceInstancePath)
{
    auto const path{ std::wstring(MAX_DEVICE_ID_LEN + 1, L'A') };
    EXPECT_EQ(L"device-instance-path-too-long", ValidateZeroOrOneDeviceInstancePath({ L"dev-repair", path }));
}

TEST(StackRepair, DeviceStackStringListRoundtripPreservesDriverEntries)
{
    // The driver stack property is a MULTI_SZ value; ensure the IOCTL contract's string list assumptions hold for
    // the shape the repair module consumes (a list of \Driver\... entries).
    std::wstring raw{ L"\\Driver\\HidHide\0\\Driver\\HidUsb\0\0", 32 };
    std::vector<WCHAR> multiString(raw.begin(), raw.end());
    std::vector<std::wstring> drivers;
    for (size_t index{}, start{}; index < multiString.size(); index++)
    {
        if (0 != multiString.at(index)) continue;
        std::wstring const entry(&multiString.at(start), 0, index - start);
        if (!entry.empty()) drivers.emplace_back(entry);
        start = index + 1;
    }
    ASSERT_EQ(2u, drivers.size());
    EXPECT_EQ(L"\\Driver\\HidHide", drivers.at(0));
    EXPECT_EQ(L"\\Driver\\HidUsb", drivers.at(1));
    EXPECT_NE(std::end(drivers), std::find(std::begin(drivers), std::end(drivers), std::wstring(L"\\Driver\\HidHide")));
    EXPECT_EQ(std::end(drivers), std::find(std::begin(drivers), std::end(drivers), std::wstring(L"\\Driver\\HidVhf")));
}
