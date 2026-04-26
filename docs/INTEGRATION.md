# xrt-sync — Host Integration Guide

This document shows how to embed xrt-sync into different host environments through its C ABI.

---

## 1. Native C++ (CMake)

```cmake
add_subdirectory(third_party/xrt-sync)
target_link_libraries(my_application PRIVATE xrtsync::xrtsync)
```

```cpp
#include <xrtsync/xrtsync.h>

xrtsync::SessionConfig cfg;
cfg.local_endpoint  = {"0.0.0.0", 7800};
cfg.remote_endpoint = {"10.0.0.5", 7800};
xrtsync::Session session(cfg);
session.start();
```

## 2. Unity (C# via P/Invoke)

Build xrt-sync as a shared library (`libxrtsync.dylib` / `xrtsync.dll` / `libxrtsync.so`) and place under `Assets/Plugins/<platform>/`.

```csharp
using System;
using System.Runtime.InteropServices;

public static class XrtSync {
    [DllImport("xrtsync")]
    public static extern IntPtr xrtsync_session_create(ref Config cfg);

    [DllImport("xrtsync")]
    public static extern int xrtsync_session_start(IntPtr session);

    [DllImport("xrtsync")]
    public static extern int xrtsync_session_send(IntPtr session, byte[] data, UIntPtr len);

    [DllImport("xrtsync")]
    public static extern int xrtsync_session_poll(IntPtr session, byte[] outBuf, ref UIntPtr len);

    [DllImport("xrtsync")]
    public static extern void xrtsync_session_close(IntPtr session);

    [StructLayout(LayoutKind.Sequential)]
    public struct Config {
        [MarshalAs(UnmanagedType.LPStr)] public string localHost;
        public ushort localPort;
        [MarshalAs(UnmanagedType.LPStr)] public string remoteHost;
        public ushort remotePort;
        public uint   targetLatencyMs;
    }
}
```

## 3. Unreal Engine (Third-Party Module)

Create `Source/ThirdParty/XrtSync/XrtSync.Build.cs`:

```csharp
using UnrealBuildTool;
using System.IO;

public class XrtSync : ModuleRules {
    public XrtSync(ReadOnlyTargetRules Target) : base(Target) {
        Type = ModuleType.External;
        string Root = Path.GetFullPath(Path.Combine(ModuleDirectory, "."));
        PublicIncludePaths.Add(Path.Combine(Root, "include"));
        if (Target.Platform == UnrealTargetPlatform.Win64) {
            PublicAdditionalLibraries.Add(Path.Combine(Root, "lib/win64/xrtsync.lib"));
            RuntimeDependencies.Add(Path.Combine(Root, "bin/win64/xrtsync.dll"));
        } else if (Target.Platform == UnrealTargetPlatform.Mac) {
            PublicAdditionalLibraries.Add(Path.Combine(Root, "lib/mac/libxrtsync.dylib"));
        }
    }
}
```

Then use the C ABI from any Unreal module that depends on `XrtSync`.

## 4. Python (ctypes)

```python
import ctypes, ctypes.util

lib = ctypes.CDLL(ctypes.util.find_library("xrtsync") or "./libxrtsync.so")

class Config(ctypes.Structure):
    _fields_ = [
        ("local_host",    ctypes.c_char_p),
        ("local_port",    ctypes.c_uint16),
        ("remote_host",   ctypes.c_char_p),
        ("remote_port",   ctypes.c_uint16),
        ("target_latency_ms", ctypes.c_uint32),
    ]

lib.xrtsync_session_create.restype  = ctypes.c_void_p
lib.xrtsync_session_create.argtypes = [ctypes.POINTER(Config)]
lib.xrtsync_session_start.argtypes  = [ctypes.c_void_p]

cfg = Config(b"0.0.0.0", 7800, b"127.0.0.1", 7800, 25)
session = lib.xrtsync_session_create(ctypes.byref(cfg))
lib.xrtsync_session_start(session)
```

## 5. Rust (bindgen)

```toml
# Cargo.toml
[build-dependencies]
bindgen = "0.69"
cc = "1.0"
```

```rust
// build.rs
fn main() {
    bindgen::Builder::default()
        .header("third_party/xrt-sync/include/xrtsync/c_abi.h")
        .generate()
        .unwrap()
        .write_to_file("src/bindings.rs")
        .unwrap();
    println!("cargo:rustc-link-lib=xrtsync");
}
```

---

## Threading Notes

The C ABI is **thread-safe per session**: any thread may call `_send`, `_poll`, or `_close`, but two threads MUST NOT call `_close` concurrently. The library spawns one I/O thread per session internally.

## Lifetime Notes

`xrtsync_session_create` allocates; `xrtsync_session_close` deallocates. A closed handle MUST NOT be reused. There is no reference counting.
