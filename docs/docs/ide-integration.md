---
id: ide-integration
title: IDE Integration
---

## 🖥️ IDE Integration

Generate IDE-specific project files:

```bash # VS Code
cforge ide vscode

# CLion
cforge ide clion

# Xcode (macOS only)
cforge ide xcode

# Visual Studio (Windows only)
cforge ide vs2022
cforge ide vs:x64  # With architecture specification 
```

Example VS Code launch configuration (generated automatically):
```json {
  "version": "0.2.0",
  "configurations": [
    {
      "name": "Debug",
      "type": "cppdbg",
      "request": "launch",
      "program": "${workspaceFolder}/build/bin/${workspaceFolderBasename}",
      "args": [],
      "stopAtEntry": false,
      "cwd": "${workspaceFolder}",
      "environment": [],
      "externalConsole": false,
      "MIMode": "gdb",
      "setupCommands": [
        {
          "description": "Enable pretty-printing for gdb",
          "text": "-enable-pretty-printing",
          "ignoreFailures": true
        }
      ],
      "preLaunchTask": "build"
    }
  ]
} 
```