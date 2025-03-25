---
id: installation
title: Installation
---

## 📥 Installation

### From Cargo

```bash 
cargo install cforge 
```

### From Source

```bash 
git clone https://github.com/ChaseSunstrom/cforge.git
cd cforge
cargo build --release
cargo install --path . 
```

### Prerequisites
- Rust
- CMake (≥3.15)
- C/C++ Compiler (GCC, Clang, MSVC)
- Optional: Ninja, Make, or Visual Studio Build Tools