"""Read Windows Bluetooth pairing and Sony service availability; no settings changed."""
import ctypes as C
import sys
import uuid
from ctypes import wintypes as W


class SystemTime(C.Structure):
    _fields_ = [("values", W.WORD * 8)]


class Address(C.Union):
    _fields_ = [("value", C.c_ulonglong), ("bytes", C.c_ubyte * 6)]


class Device(C.Structure):
    _fields_ = [
        ("size", W.DWORD), ("address", Address), ("device_class", W.ULONG),
        ("connected", W.BOOL), ("remembered", W.BOOL), ("authenticated", W.BOOL),
        ("last_seen", SystemTime), ("last_used", SystemTime), ("name", W.WCHAR * 248),
    ]


class Search(C.Structure):
    _fields_ = [
        ("size", W.DWORD), ("authenticated", W.BOOL), ("remembered", W.BOOL),
        ("unknown", W.BOOL), ("connected", W.BOOL), ("inquiry", W.BOOL),
        ("timeout", C.c_ubyte), ("radio", W.HANDLE),
    ]


class Guid(C.Structure):
    _fields_ = [("data1", W.DWORD), ("data2", W.WORD), ("data3", W.WORD),
                ("data4", C.c_ubyte * 8)]


def main():
    bt = C.WinDLL("bthprops.cpl", use_last_error=True)
    bt.BluetoothFindFirstDevice.argtypes = [C.POINTER(Search), C.POINTER(Device)]
    bt.BluetoothFindFirstDevice.restype = W.HANDLE
    bt.BluetoothFindNextDevice.argtypes = [W.HANDLE, C.POINTER(Device)]
    bt.BluetoothFindNextDevice.restype = W.BOOL
    bt.BluetoothFindDeviceClose.argtypes = [W.HANDLE]
    bt.BluetoothFindDeviceClose.restype = W.BOOL
    bt.BluetoothEnumerateInstalledServices.argtypes = [
        W.HANDLE, C.POINTER(Device), C.POINTER(W.DWORD), C.POINTER(Guid)]
    bt.BluetoothEnumerateInstalledServices.restype = W.DWORD
    search = Search()
    search.size = C.sizeof(search)
    search.authenticated = search.remembered = search.connected = 1
    device = Device()
    device.size = C.sizeof(device)
    handle = bt.BluetoothFindFirstDevice(C.byref(search), C.byref(device))
    if not handle:
        error = C.get_last_error()
        print(f"Bluetooth enumeration failed: {error}: {C.FormatError(error).strip()}")
        return 1
    found = False
    try:
        while True:
            if device.name == "WF-1000XM5":
                found = True
                print(f"Device: {device.name}; connected: {bool(device.connected)}; paired: {bool(device.authenticated)}")
                count = W.DWORD(64)
                services = (Guid * count.value)()
                error = bt.BluetoothEnumerateInstalledServices(None, C.byref(device), C.byref(count), services)
                if error:
                    print(f"Service enumeration failed: {error}")
                    return 1
                service_ids = {str(uuid.UUID(bytes_le=bytes(g))) for g in services[:count.value]}
                print("Sony v2 control service:", "956c7b26-d49a-4ba8-b03f-b17d393cb6e2" in service_ids)
            if not bt.BluetoothFindNextDevice(handle, C.byref(device)):
                error = C.get_last_error()
                if error != 259:  # ERROR_NO_MORE_ITEMS
                    print(f"Bluetooth iteration failed: {error}")
                    return 1
                break
    finally:
        bt.BluetoothFindDeviceClose(handle)
    if not found:
        print("WF-1000XM5 not found among remembered/connected devices.")
    return 0 if found else 2


if __name__ == "__main__":
    sys.exit(main())
