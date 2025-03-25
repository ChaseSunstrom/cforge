---
id: troubleshooting
title: Troubleshooting
---

## 🔧 Troubleshooting

- **CMake not found**: Ensure it's installed and in PATH.
- **Dependency failures**: Run `cforge deps --update`.
- **Cross-compilation**: Check environment variables (e.g., `$ANDROID_NDK`).
- **Compiler errors**: Use `cforge build --verbosity verbose`.