# GLAD (vendored)

OpenGL 3.3 core loader generated with [glad2](https://github.com/Dav1dde/glad)
2.0.8. No extensions are included — core 3.3 is all the renderer needs.

Files are checked in, so building the project does not require Python or glad.

## Regenerating

```sh
python3 -m venv /tmp/opencode/gladvenv
/tmp/opencode/gladvenv/bin/pip install glad2
/tmp/opencode/gladvenv/bin/glad --quiet --reproducible --api gl:core=3.3 --extensions "" --out-path external/glad c
```

Generated files (the only ones to keep):

```
external/glad/include/glad/gl.h
external/glad/include/KHR/khrplatform.h
external/glad/src/gl.c
```

## Notes

- The loader is compiled into the static library target `glad`, with warnings
  disabled: it is generated code.
- `gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)` is called from
  `src/platform/platform.c` after the GL context is current, which is why the
  project never links against libGL directly.
- `--reproducible` is supported by glad 2.0.8 and is used above: it generates
  from the specification bundled with the package instead of fetching the latest
  one from Khronos, and leaves the generation timestamp out of the header.
  `--extensions ""` keeps the loader free of extension entry points.
