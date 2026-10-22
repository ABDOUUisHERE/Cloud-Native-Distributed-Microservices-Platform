# Vendored health contract

`v1/health.proto` is copied without modification from [gRPC v1.81.1](https://github.com/grpc/grpc/blob/v1.81.1/src/proto/grpc/health/v1/health.proto), matching the optional pinned vcpkg version. Its standard Check/Watch messages are wire-compatible with the default Ubuntu gRPC 1.51.1 profile. Each build generates bindings with its own matching protoc and plugin.

SHA-256: `8d44f54645557c1e10ba0da377883fd4d24ad994aff4f2139d61b7e9f0ece511`.

The original copyright and Apache-2.0 header are preserved. The complete Apache-2.0 license text is in [LICENSES/Apache-2.0.txt](../../../LICENSES/Apache-2.0.txt). This attribution applies to the vendored contract; no license for original project code is selected by this file.

CMake generates its C++ client bindings locally. Generated code is not committed. The phase 1 worker uses gRPC's built-in health server and advertises only `taskrelay.foundation.worker`; it does not advertise a working task processor.
