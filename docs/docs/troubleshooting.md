---
id: troubleshooting
title: Troubleshooting
---

## 🔧 Troubleshooting

### Common Issues

- **CMake not found**: Ensure it's installed and in PATH.
- **Dependency failures**: Run `cforge deps --update`.
- **Cross-compilation**: Check environment variables (e.g., `$ANDROID_NDK`).
- **Compiler errors**: Use `cforge build --verbosity verbose`.

CForge provides enhanced error diagnostics:

```bash
# Build error details:
ERROR[E0001]: undefined reference to 'math_lib::divide(int, int)'
 --> src/main.cpp:12:5
  12| math_lib::divide(10, 0);
     ^~~~~~~~~~~~~~~~

help: The function 'divide' is used but not defined. Check if the library is properly linked. 
```

### Useful Commands

```bash 
# List available configurations
cforge list configs

# List available build variants
cforge list variants

# List cross-compilation targets
cforge list targets

# List custom scripts
cforge list scripts 
```