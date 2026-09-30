# Third-party dependency ledger

Arssyut does not vendor third-party runtime libraries in P0.

Before adding a dependency, record:

- package/project name;
- exact version/commit;
- upstream URL;
- license/SPDX identifier;
- whether source or binaries are redistributed;
- build/runtime role;
- expected binary-size impact;
- security/update owner;
- why the dependency is preferable to platform/project-native code.

Media-stack dependencies are intentionally deferred to the measured P2A
FFmpeg/libav vs Media Foundation decision.
