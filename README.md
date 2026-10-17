# AMD OpenGL: `glLinkProgram` crashes when "main" appears in a comment before the entry point in a shader using subroutines

**Tested on:** AMD Radeon RX 7900 GRE, driver 26.8.1.260810
(`4.6.0 Compatibility Profile Context`), Windows 11.

## Shader

```glsl
#version 430
// main <----
subroutine vec3 gradType(vec3 p);
subroutine uniform gradType grad;
subroutine(gradType) vec3 gradA(vec3 p) { return p; }
vec3 f(vec3 p) { return grad(p); }
void main() {}
```

- **Shader shown above:** `glCompileShader` succeeds, then `glLinkProgram` terminates
  the process with `0xC0000409` (STATUS_STACK_BUFFER_OVERRUN).
- **Without line 2 (`// main`):** the program links fine...

## What is required to trigger it

- **The standalone word `main` before the entry point:** in a `//` or `/* */`
  comment, or even inside an `#if 0` block. `Main`, `mainly` and `main_` don't
  trigger it, and neither does `main` after the entry point `void main() {}`.
- **A subroutine uniform called from a function other than `main()`:** calling
  it directly inside `main()`, or never calling it, does not crash.

## Build

From an x64 Visual Studio developer prompt:

```
cl /nologo /EHsc /O2 /MT amd_glsl_main_crash.cpp opengl32.lib user32.lib gdi32.lib
```

## Run

Start `amd_glsl_main_crash.exe` (no arguments). It compiles and links both
variants. The compilation is done in child processes to observe the error codes
returned. It prints:

```
Fragment shader without comment -> OK
Fragment shader with '// main'  -> CRASH 0xC0000409 (STATUS_STACK_BUFFER_OVERRUN) in glLinkProgram
```
