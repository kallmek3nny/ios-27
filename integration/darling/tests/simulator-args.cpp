/* Compile the actual host-dyld handoff block in a small host harness, then
 * consume it as modern dyld_sim does: reconstruct KernelArgs from argv[-2]. */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define DARLING 1
struct macho_header { uint32_t magic; };
extern "C" int _dyld_func_lookup(const char*, void**) { return 0; }
struct Arguments { const char** argv; const char** envp; const char** apple; };
static const char* build(int argc, const char** argv, const char** envp, const char** apple,
                         const macho_header* mainExecutableMH, Arguments* output) {
#include SIMULATOR_ARGS_BLOCK
    output->argv = argvParams; output->envp = envParams; output->apple = appleParams;
    return nullptr;
}
static unsigned checks;
#define CHECK(c) do { ++checks; if (!(c)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

static void consume(Arguments args, int count, const macho_header* main) {
    const uintptr_t* kernel = reinterpret_cast<const uintptr_t*>(args.argv) - 2;
    CHECK(kernel[0] == reinterpret_cast<uintptr_t>(main) && kernel[1] == static_cast<uintptr_t>(count));
    CHECK(args.argv[count] == nullptr);
    const char** env = args.argv + kernel[1] + 1;
    CHECK(env == args.envp);
    while (*env) ++env;
    const char** apple = env + 1;
    CHECK(apple == args.apple);
    size_t seen = 0;
    static const char key[] = "darling_dyld_lookup=0x";
    for (; *apple; ++apple) {
        if (std::strncmp(*apple, key, sizeof(key) - 1)) continue;
        char* end;
        auto pointer = std::strtoull(*apple + sizeof(key) - 1, &end, 16);
        CHECK(*end == '\0' && pointer == reinterpret_cast<uintptr_t>(&_dyld_func_lookup));
        ++seen;
    }
    CHECK(seen == 1);
}

int main() {
    macho_header main = {0xfeedfacf};
    const char* argv[] = {"launchd_sim", "configuration.plist", nullptr};
    const char* env[] = {"A=one", "B=two", nullptr};
    const char* apple[] = {"executable_path=/sbin/launchd_sim", "elf_calls=0x1234", nullptr};
    Arguments args{};
    CHECK(build(2, argv, env, apple, &main, &args) == nullptr);
    CHECK(args.argv != argv && args.envp != env && args.apple != apple);
    consume(args, 2, &main);
    // Copy the pointer vectors; retain the original process-lifetime strings.
    argv[0] = "replaced"; env[0] = "replaced"; apple[0] = "replaced";
    CHECK(std::strcmp(args.argv[0], "launchd_sim") == 0);
    CHECK(std::strcmp(args.envp[0], "A=one") == 0);
    CHECK(std::strcmp(args.apple[0], "executable_path=/sbin/launchd_sim") == 0);
    std::free(const_cast<uintptr_t*>(reinterpret_cast<const uintptr_t*>(args.argv) - 2));

    const char* empty[] = {nullptr};
    CHECK(build(0, empty, empty, empty, &main, &args) == nullptr);
    consume(args, 0, &main);
    std::free(const_cast<uintptr_t*>(reinterpret_cast<const uintptr_t*>(args.argv) - 2));
    CHECK(build(-1, empty, empty, empty, &main, &args) != nullptr);
    CHECK(build(65537, empty, empty, empty, &main, &args) != nullptr);
    CHECK(build(0, argv, empty, empty, &main, &args) != nullptr);

    auto longEnv = new const char*[4096];
    for (unsigned i = 0; i < 4096; ++i) longEnv[i] = "A=B";
    CHECK(build(0, empty, longEnv, empty, &main, &args) != nullptr);
    delete[] longEnv;
    auto longApple = new const char*[512];
    for (unsigned i = 0; i < 512; ++i) longApple[i] = "A=B";
    CHECK(build(0, empty, empty, longApple, &main, &args) != nullptr);
    delete[] longApple;
    std::printf("SIMULATOR_ARGUMENTS_OK checks=%u\n", checks);
}
