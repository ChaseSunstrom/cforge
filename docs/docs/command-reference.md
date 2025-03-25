---
id: command-reference
title: Command Reference
---

## 🛠️ Command Reference

| Command      | Description                         | Example                             |
|--------------|-------------------------------------|-------------------------------------|
| `init`       | Create new project/workspace        | `cforge init --template lib`        |
| `build`      | Build the project                   | `cforge build --config Release`     |
| `clean`      | Clean build artifacts               | `cforge clean`                      |
| `run`        | Run built executable                | `cforge run -- arg1 arg2`           |
| `test`       | Execute tests (CTest integration)   | `cforge test --filter MyTest`       |
| `install`    | Install project binaries            | `cforge install --prefix /usr/local`|
| `deps`       | Manage dependencies                 | `cforge deps --update`              |
| `script`     | Execute custom scripts              | `cforge script format`              |
| `startup`    | Manage workspace startup project    | `cforge startup my_app`             |
| `ide`        | Generate IDE project files          | `cforge ide vscode`                 |
| `package`    | Package project binaries            | `cforge package --type zip`         |
| `list`       | List variants, configs, or targets  | `cforge list variants`              |