// AMD OpenGL driver crash repro: the word "main" in a comment before the
// entry point + a subroutine called from a helper function makes
// glLinkProgram crash with STATUS_STACK_BUFFER_OVERRUN (0xC0000409).
//
// Run without arguments. Each test shader is compiled and linked in a child
// process (so the crash cannot take down the tester) and the result printed.
//
// Build: cl /nologo /EHsc /O2 /MT amd_glsl_main_crash.cpp opengl32.lib user32.lib gdi32.lib

#include <windows.h>
#include <GL/gl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// The only difference between the two shaders is line 2.
static const char* SHADER_OK =
    "#version 430\n"
    "subroutine vec3 gradType(vec3 p);\n"
    "subroutine uniform gradType grad;\n"
    "subroutine(gradType) vec3 gradA(vec3 p) { return p; }\n"
    "vec3 f(vec3 p) { return grad(p); }\n"
    "void main() {}\n";

static const char* SHADER_CRASH =
    "#version 430\n"
    "// main\n"
    "subroutine vec3 gradType(vec3 p);\n"
    "subroutine uniform gradType grad;\n"
    "subroutine(gradType) vec3 gradA(vec3 p) { return p; }\n"
    "vec3 f(vec3 p) { return grad(p); }\n"
    "void main() {}\n";

struct TestCase { const char* name; const char* src; };
static const TestCase CASES[] = {
    { "without comment", SHADER_OK },
    { "with '// main' ", SHADER_CRASH },
};
static const int NUM_CASES = sizeof(CASES) / sizeof(CASES[0]);

// ---------------------------------------------------------------- child --

typedef char GLchar;
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS  0x8B81
#define GL_LINK_STATUS     0x8B82

typedef GLuint (APIENTRY *PFNCREATESHADER)(GLenum);
typedef void   (APIENTRY *PFNSHADERSOURCE)(GLuint, GLsizei, const GLchar* const*, const GLint*);
typedef void   (APIENTRY *PFNCOMPILESHADER)(GLuint);
typedef void   (APIENTRY *PFNGETSHADERIV)(GLuint, GLenum, GLint*);
typedef void   (APIENTRY *PFNGETINFOLOG)(GLuint, GLsizei, GLsizei*, GLchar*);
typedef GLuint (APIENTRY *PFNCREATEPROGRAM)();
typedef void   (APIENTRY *PFNATTACHSHADER)(GLuint, GLuint);
typedef void   (APIENTRY *PFNLINKPROGRAM)(GLuint);

// Exit codes of the child process.
enum { RES_OK = 0, RES_COMPILE_FAILED = 1, RES_LINK_FAILED = 2, RES_NO_CONTEXT = 3 };

static int runCase(int idx, bool printInfo)
{
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "amd_glsl_main_crash";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA(wc.lpszClassName, "", WS_OVERLAPPEDWINDOW,
                              0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    HDC dc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    SetPixelFormat(dc, ChoosePixelFormat(dc, &pfd), &pfd);
    HGLRC rc = wglCreateContext(dc);
    if (!rc || !wglMakeCurrent(dc, rc)) return RES_NO_CONTEXT;

    if (printInfo)
        printf("GPU:    %s\nDriver: %s\n\n",
               (const char*)glGetString(GL_RENDERER), (const char*)glGetString(GL_VERSION));

    auto createShader  = (PFNCREATESHADER)wglGetProcAddress("glCreateShader");
    auto shaderSource  = (PFNSHADERSOURCE)wglGetProcAddress("glShaderSource");
    auto compileShader = (PFNCOMPILESHADER)wglGetProcAddress("glCompileShader");
    auto getShaderiv   = (PFNGETSHADERIV)wglGetProcAddress("glGetShaderiv");
    auto getShaderLog  = (PFNGETINFOLOG)wglGetProcAddress("glGetShaderInfoLog");
    auto createProgram = (PFNCREATEPROGRAM)wglGetProcAddress("glCreateProgram");
    auto attachShader  = (PFNATTACHSHADER)wglGetProcAddress("glAttachShader");
    auto linkProgram   = (PFNLINKPROGRAM)wglGetProcAddress("glLinkProgram");
    auto getProgramiv  = (PFNGETSHADERIV)wglGetProcAddress("glGetProgramiv");
    auto getProgramLog = (PFNGETINFOLOG)wglGetProcAddress("glGetProgramInfoLog");
    if (!createShader || !linkProgram) return RES_NO_CONTEXT;

    char log[4096] = {};
    GLuint sh = createShader(GL_FRAGMENT_SHADER);
    shaderSource(sh, 1, &CASES[idx].src, nullptr);
    compileShader(sh);
    GLint ok = 0;
    getShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { getShaderLog(sh, sizeof(log), nullptr, log); fprintf(stderr, "%s\n", log); return RES_COMPILE_FAILED; }

    GLuint prog = createProgram();
    attachShader(prog, sh);
    linkProgram(prog);  // <-- crashes here
    getProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { getProgramLog(prog, sizeof(log), nullptr, log); fprintf(stderr, "%s\n", log); return RES_LINK_FAILED; }
    return RES_OK;
}

// --------------------------------------------------------------- parent --

static DWORD spawnCase(int idx)
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char cmd[MAX_PATH + 32];
    snprintf(cmd, sizeof(cmd), "\"%s\" --case %d", exe, idx);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(nullptr, cmd, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
        return (DWORD)-1;
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code;
}

static const char* describe(DWORD code)
{
    switch (code)
    {
    case RES_OK:             return "OK";
    case RES_COMPILE_FAILED: return "compile error (see log above)";
    case RES_LINK_FAILED:    return "link error (see log above)";
    case RES_NO_CONTEXT:     return "could not create OpenGL context";
    case 0xC0000409:         return "CRASH 0xC0000409 (STATUS_STACK_BUFFER_OVERRUN) in glLinkProgram";
    case 0xC0000005:         return "CRASH 0xC0000005 (access violation)";
    default:                 return "unexpected exit code";
    }
}

int main(int argc, char** argv)
{
    if (argc == 3 && strcmp(argv[1], "--case") == 0)
        return runCase(atoi(argv[2]), false);

    printf("AMD OpenGL driver crash repro: \"main\" in a comment + subroutine call\n\n");
    if (runCase(0, true) != RES_OK)  // also prints GPU / driver
        printf("(note: direct test of the first shader did not succeed)\n\n");

    for (int i = 0; i < NUM_CASES; ++i)
    {
        DWORD code = spawnCase(i);
        printf("Fragment shader %s -> %s (exit 0x%08lX)\n", CASES[i].name, describe(code), code);
    }

    printf("\nThe two shaders differ only in line 2:\n\n%s\n", SHADER_CRASH);
    printf("Press Enter to exit...");
    getchar();
    return 0;
}
