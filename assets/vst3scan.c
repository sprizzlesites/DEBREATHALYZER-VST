// Minimal VST3 loader: load the DLL, walk the factory, count classes,
// instantiate the Audio Module component, and confirm it succeeds.
// A plugin that can't load here won't load in a DAW.
#include <windows.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char cid[16];
    int  cardinality;
    char category[32];
    char name[64];
} PClassInfo;

typedef struct IPluginFactoryVtbl IPluginFactoryVtbl;
typedef struct { IPluginFactoryVtbl* v; } IPluginFactory;

struct IPluginFactoryVtbl {
    long (__stdcall *queryInterface)(void*, const char*, void**);
    unsigned long (__stdcall *addRef)(void*);
    unsigned long (__stdcall *release)(void*);
    long (__stdcall *getFactoryInfo)(void*, void*);
    int  (__stdcall *countClasses)(void*);
    long (__stdcall *getClassInfo)(void*, int, PClassInfo*);
    long (__stdcall *createInstance)(void*, const char* cid, const char* iid, void** obj);
};

int main(int argc, char** argv)
{
    if (argc < 2) { printf("usage: vst3scan <plugin.vst3 dir or dll>\n"); return 2; }

    char path[1024];
    snprintf(path, sizeof(path), "%s", argv[1]);
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
        const char* base = strrchr(path, '\\'); if (!base) base = strrchr(path, '/');
        base = base ? base + 1 : path;
        char name[512]; snprintf(name, sizeof(name), "%s", base);
        char* dot = strstr(name, ".vst3"); if (dot) *dot = 0;
        char inner[1024];
        snprintf(inner, sizeof(inner), "%s\\Contents\\x86_64-win\\%s.vst3", path, name);
        snprintf(path, sizeof(path), "%s", inner);
    }
    printf("loading: %s\n", path);

    HMODULE h = LoadLibraryA(path);
    if (!h) { printf("FAIL LoadLibrary error %lu\n", GetLastError()); return 1; }

    typedef IPluginFactory* (__stdcall *GetFactoryFn)(void);
    GetFactoryFn getFactory = (GetFactoryFn)(void*)GetProcAddress(h, "GetPluginFactory");
    if (!getFactory) { printf("FAIL no GetPluginFactory export\n"); return 1; }

    typedef int (__stdcall *InitDllFn)(void);
    InitDllFn initDll = (InitDllFn)(void*)GetProcAddress(h, "InitDll");
    if (initDll) initDll();

    IPluginFactory* f = getFactory();
    if (!f) { printf("FAIL GetPluginFactory returned null\n"); return 1; }

    int n = f->v->countClasses(f);
    printf("classes: %d\n", n);
    int madeComponent = 0;
    for (int i = 0; i < n; ++i) {
        PClassInfo ci; memset(&ci, 0, sizeof(ci));
        if (f->v->getClassInfo(f, i, &ci) != 0) continue;
        printf("  [%d] category='%s' name='%s'\n", i, ci.category, ci.name);
        if (strstr(ci.category, "Audio Module")) {
            static const unsigned char icomp[16] = {
                0x31,0xFF,0x31,0xE8, 0xD5,0xF2, 0x01,0x43,
                0x92,0x8E, 0xBB,0xEE,0x25,0x69,0x78,0x02 };
            void* obj = 0;
            long r = f->v->createInstance(f, ci.cid, (const char*)icomp, &obj);
            if (r == 0 && obj) { printf("      instantiated IComponent OK\n"); madeComponent = 1; }
            else printf("      createInstance failed r=%ld obj=%p\n", r, obj);
        }
    }

    printf(madeComponent ? "PASS\n" : "FAIL no component instantiated\n");
    return madeComponent ? 0 : 1;
}
