const std = @import("std");
const builtin = @import("builtin");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    // Build options
    const build_client = b.option(bool, "client", "Build the client executable") orelse true;
    const build_server = b.option(bool, "server", "Build the dedicated server") orelse false;
    const build_renderer_opengl1 = b.option(bool, "renderer-opengl1", "Build OpenGL 1.x renderer") orelse true;
    const build_renderer_rend2 = b.option(bool, "renderer-rend2", "Build OpenGL 2+ renderer (rend2)") orelse true;
    const build_game_so = b.option(bool, "game-so", "Build game modules as shared libraries") orelse true;
    const use_openal = b.option(bool, "openal", "Enable OpenAL audio") orelse true;
    const use_curl = b.option(bool, "curl", "Enable CURL for HTTP downloads") orelse false;
    const use_voip = b.option(bool, "voip", "Enable VoIP support") orelse true;
    const use_codec_vorbis = b.option(bool, "vorbis", "Enable Vorbis codec") orelse true;
    const use_codec_opus = b.option(bool, "opus", "Enable Opus codec") orelse true;
    const use_mumble = b.option(bool, "mumble", "Enable Mumble positional audio") orelse true;
    const use_freetype = b.option(bool, "freetype", "Enable FreeType font rendering") orelse true;
    const use_bloom = b.option(bool, "bloom", "Enable bloom post-processing") orelse true;
    const use_internal_libs = b.option(bool, "internal-libs", "Use internal/bundled libraries") orelse true;
    const use_renderer_dlopen = b.option(bool, "renderer-dlopen", "Dynamically load renderers") orelse true;

    const version = "1.51d-SP";

    // Determine architecture string for file naming
    const arch_str = switch (target.result.cpu.arch) {
        .x86_64 => "x86_64",
        .x86 => "i386",
        .arm, .armeb => "arm",
        .aarch64 => "arm64",
        .powerpc => "ppc",
        .powerpc64 => "ppc64",
        else => "unknown",
    };

    // Base defines that all targets need
    const version_define = b.fmt("-DPRODUCT_VERSION=\"{s}\"", .{version});
    const arch_define = b.fmt("-DARCH_STRING=\"{s}\"", .{arch_str});

    // =========================================================================
    // Client executable
    // =========================================================================
    if (build_client) {
        const client_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .link_libcpp = true, // For splines C++ code
        });

        const client = b.addExecutable(.{
            .name = "iowolfsp",
            .root_module = client_module,
        });

        // Build client cflags using allocator
        const client_cflags = buildClientCflags(b.allocator, version_define, arch_define, use_openal, use_curl, use_voip, use_codec_opus, use_codec_vorbis, use_mumble, use_bloom, use_renderer_dlopen);

        // Add C source files
        addClientSources(client, client_cflags, use_internal_libs, use_openal, use_voip, use_codec_opus, use_codec_vorbis);

        // Add include paths
        addCommonIncludes(client, use_internal_libs);

        // Link system libraries - OpenAL uses dlopen so don't link directly
        client.linkSystemLibrary("SDL2");
        client.linkSystemLibrary("m");
        client.linkSystemLibrary("dl");
        client.linkSystemLibrary("pthread");

        if (use_mumble) {
            client.linkSystemLibrary("rt");
        }

        b.installArtifact(client);
    }

    // =========================================================================
    // Dedicated server executable
    // =========================================================================
    if (build_server) {
        const server_cflags = buildServerCflags(b.allocator, version_define, arch_define, use_voip);

        const server_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        });

        const server = b.addExecutable(.{
            .name = "iowolfspded",
            .root_module = server_module,
        });

        addServerSources(server, server_cflags, use_internal_libs);
        addCommonIncludes(server, use_internal_libs);

        server.linkSystemLibrary("m");
        server.linkSystemLibrary("dl");
        server.linkSystemLibrary("pthread");

        b.installArtifact(server);
    }

    // Renderer cflags
    const renderer_cflags = buildRendererCflags(b.allocator, version_define, arch_define, use_freetype, use_bloom);

    // =========================================================================
    // OpenGL 1.x Renderer (shared library)
    // =========================================================================
    if (build_renderer_opengl1 and use_renderer_dlopen) {
        const renderer_name = b.fmt("renderer_sp_opengl1_{s}", .{arch_str});

        const renderer_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        });

        const renderer_opengl1 = b.addLibrary(.{
            .linkage = .dynamic,
            .name = renderer_name,
            .root_module = renderer_module,
        });

        addRendererOpenGL1Sources(renderer_opengl1, renderer_cflags, use_internal_libs, use_freetype, use_bloom);
        addRendererIncludes(renderer_opengl1, use_internal_libs, use_freetype);

        renderer_opengl1.linkSystemLibrary("SDL2");
        renderer_opengl1.linkSystemLibrary("GL");
        renderer_opengl1.linkSystemLibrary("m");

        if (use_freetype and !use_internal_libs) {
            renderer_opengl1.linkSystemLibrary("freetype");
        }

        b.installArtifact(renderer_opengl1);
    }

    // =========================================================================
    // Stringify tool (converts GLSL shaders to C strings)
    // =========================================================================
    const stringify_module = b.createModule(.{
        .target = b.graph.host,
        .optimize = .ReleaseFast,
        .link_libc = true,
    });

    const stringify = b.addExecutable(.{
        .name = "stringify",
        .root_module = stringify_module,
    });

    stringify.addCSourceFiles(.{
        .files = &.{"code/tools/stringify.c"},
        .flags = &.{"-Wall"},
    });

    // =========================================================================
    // Rend2 Renderer (OpenGL 2+ with GLSL)
    // =========================================================================
    if (build_renderer_rend2 and use_renderer_dlopen) {
        const renderer2_name = b.fmt("renderer_sp_rend2_{s}", .{arch_str});

        const renderer2_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        });

        const renderer_rend2 = b.addLibrary(.{
            .linkage = .dynamic,
            .name = renderer2_name,
            .root_module = renderer2_module,
        });

        // Generate C files from GLSL shaders
        const glsl_shaders = [_][]const u8{
            "bokeh_fp",        "bokeh_vp",
            "calclevels4x_fp", "calclevels4x_vp",
            "depthblur_fp",    "depthblur_vp",
            "dlight_fp",       "dlight_vp",
            "down4x_fp",       "down4x_vp",
            "fogpass_fp",      "fogpass_vp",
            "generic_fp",      "generic_vp",
            "lightall_fp",     "lightall_vp",
            "pshadow_fp",      "pshadow_vp",
            "shadowfill_fp",   "shadowfill_vp",
            "shadowmask_fp",   "shadowmask_vp",
            "ssao_fp",         "ssao_vp",
            "texturecolor_fp", "texturecolor_vp",
            "tonemap_fp",      "tonemap_vp",
        };

        for (glsl_shaders) |shader| {
            const glsl_file = b.fmt("code/rend2/glsl/{s}.glsl", .{shader});
            const run_stringify = b.addRunArtifact(stringify);
            run_stringify.addFileArg(.{ .cwd_relative = glsl_file });
            const generated_c = run_stringify.addOutputFileArg(b.fmt("{s}.c", .{shader}));
            renderer_rend2.addCSourceFile(.{
                .file = generated_c,
                .flags = renderer_cflags,
            });
        }

        addRendererRend2Sources(renderer_rend2, renderer_cflags, use_internal_libs, use_freetype);
        addRendererIncludes(renderer_rend2, use_internal_libs, use_freetype);

        renderer_rend2.linkSystemLibrary("SDL2");
        renderer_rend2.linkSystemLibrary("GL");
        renderer_rend2.linkSystemLibrary("m");

        if (use_freetype and !use_internal_libs) {
            renderer_rend2.linkSystemLibrary("freetype");
        }

        b.installArtifact(renderer_rend2);
    }

    // =========================================================================
    // Game modules (shared libraries)
    // =========================================================================
    if (build_game_so) {
        const game_cflags: []const []const u8 = &.{
            "-Wall",
            "-fno-strict-aliasing",
            "-fPIC",
            "-O3",
            "-DNDEBUG",
            version_define,
            arch_define,
            "-Wno-unused-but-set-variable",
            "-Wno-strict-prototypes",
            "-fwrapv", // Allow integer overflow (legacy C code)
            "-fno-sanitize=undefined", // Disable UB sanitizer for legacy C code
        };

        // cgame module
        const cgame_name = b.fmt("cgame.sp.{s}", .{arch_str});

        const cgame_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        });

        const cgame = b.addLibrary(.{
            .linkage = .dynamic,
            .name = cgame_name,
            .root_module = cgame_module,
        });

        addCgameSources(b, cgame, game_cflags);
        addGameModuleIncludes(cgame);
        cgame.linkSystemLibrary("m");

        b.installArtifact(cgame);

        // qagame (server-side game) module
        const qagame_name = b.fmt("qagame.sp.{s}", .{arch_str});

        const qagame_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
            .link_libcpp = true,
        });

        const qagame = b.addLibrary(.{
            .linkage = .dynamic,
            .name = qagame_name,
            .root_module = qagame_module,
        });

        addQagameSources(b, qagame, game_cflags);
        addGameModuleIncludes(qagame);

        qagame.linkSystemLibrary("m");
        // Link llama.cpp libraries
        qagame.addLibraryPath(.{ .cwd_relative = "code/llama.cpp/build/bin" });
        qagame.linkSystemLibrary("llama");
        qagame.linkSystemLibrary("ggml");
        qagame.linkSystemLibrary("ggml-base");
        qagame.linkSystemLibrary("ggml-cpu");

        b.installArtifact(qagame);

        // UI module
        const ui_name = b.fmt("ui.sp.{s}", .{arch_str});

        const ui_module = b.createModule(.{
            .target = target,
            .optimize = optimize,
            .link_libc = true,
        });

        const ui = b.addLibrary(.{
            .linkage = .dynamic,
            .name = ui_name,
            .root_module = ui_module,
        });

        addUiSources(b, ui, game_cflags);
        addGameModuleIncludes(ui);
        ui.linkSystemLibrary("m");

        b.installArtifact(ui);
    }

    // =========================================================================
    // Install to Steam step
    // =========================================================================
    const install_steam_step = b.step("install-steam", "Install to Steam Flatpak directory");
    install_steam_step.dependOn(b.getInstallStep());

    // Get HOME directory
    const home = std.posix.getenv("HOME") orelse "/home";
    const steam_rtcw_dir = b.fmt("{s}/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Return to Castle Wolfenstein", .{home});
    const steam_realrtcw_dir = b.fmt("{s}/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/RealRTCW", .{home});

    // Create directories
    const mkdir_main = b.addSystemCommand(&.{ "mkdir", "-p", b.fmt("{s}/main/models", .{steam_realrtcw_dir}) });
    install_steam_step.dependOn(&mkdir_main.step);

    // Print installation start message
    const print_start = b.addSystemCommand(&.{ "echo", "Installing RealRTCW to Steam directory..." });
    print_start.step.dependOn(&mkdir_main.step);
    install_steam_step.dependOn(&print_start.step);

    // Copy original RTCW assets (optional - may not exist)
    const pak_files = [_][]const u8{ "pak0.pk3", "sp_pak1.pk3", "sp_pak2.pk3", "sp_pak3.pk3", "sp_pak4.pk3" };
    for (pak_files) |pak| {
        const cp_pak = b.addSystemCommand(&.{
            "sh",                                                                                                                                       "-c",
            b.fmt("cp -f \"{s}/main/{s}\" \"{s}/main/\" 2>/dev/null || echo 'Note: {s} not found'", .{ steam_rtcw_dir, pak, steam_realrtcw_dir, pak }),
        });
        cp_pak.step.dependOn(&mkdir_main.step);
        install_steam_step.dependOn(&cp_pak.step);
    }

    // Copy RealRTCW content
    const cp_pk3 = b.addSystemCommand(&.{ "sh", "-c", b.fmt("cp -f Main/*.pk3 \"{s}/main/\" 2>/dev/null || true", .{steam_realrtcw_dir}) });
    cp_pk3.step.dependOn(&mkdir_main.step);
    install_steam_step.dependOn(&cp_pk3.step);

    const cp_cfg = b.addSystemCommand(&.{ "sh", "-c", b.fmt("cp -f Main/*.cfg \"{s}/main/\" 2>/dev/null || true", .{steam_realrtcw_dir}) });
    cp_cfg.step.dependOn(&mkdir_main.step);
    install_steam_step.dependOn(&cp_cfg.step);

    // Copy LLM models
    const cp_models = b.addSystemCommand(&.{ "sh", "-c", b.fmt("cp -rf Main/models/* \"{s}/main/models/\" 2>/dev/null || true", .{steam_realrtcw_dir}) });
    cp_models.step.dependOn(&mkdir_main.step);
    install_steam_step.dependOn(&cp_models.step);

    // Copy TTS files
    const cp_tts = b.addSystemCommand(&.{ "sh", "-c", b.fmt("cp -rf Main/tts \"{s}/main/\" 2>/dev/null || true", .{steam_realrtcw_dir}) });
    cp_tts.step.dependOn(&mkdir_main.step);
    install_steam_step.dependOn(&cp_tts.step);

    // Create GLSL shaders pk3
    const create_glsl = b.addSystemCommand(&.{
        "sh",                                                                                                               "-c",
        b.fmt("if [ -d Main/glsl ]; then cd Main && zip -r \"{s}/main/glsl_shaders.pk3\" glsl; fi", .{steam_realrtcw_dir}),
    });
    create_glsl.step.dependOn(&mkdir_main.step);
    install_steam_step.dependOn(&create_glsl.step);

    // Install game modules
    const cp_cgame = b.addSystemCommand(&.{
        "cp",                                                 "-f",
        b.fmt("zig-out/lib/libcgame.sp.{s}.so", .{arch_str}), b.fmt("{s}/main/cgame.sp.{s}.so", .{ steam_realrtcw_dir, arch_str }),
    });
    cp_cgame.step.dependOn(b.getInstallStep());
    install_steam_step.dependOn(&cp_cgame.step);

    const cp_qagame = b.addSystemCommand(&.{
        "cp",                                                  "-f",
        b.fmt("zig-out/lib/libqagame.sp.{s}.so", .{arch_str}), b.fmt("{s}/main/qagame.sp.{s}.so", .{ steam_realrtcw_dir, arch_str }),
    });
    cp_qagame.step.dependOn(b.getInstallStep());
    install_steam_step.dependOn(&cp_qagame.step);

    const cp_ui = b.addSystemCommand(&.{
        "cp",                                              "-f",
        b.fmt("zig-out/lib/libui.sp.{s}.so", .{arch_str}), b.fmt("{s}/main/ui.sp.{s}.so", .{ steam_realrtcw_dir, arch_str }),
    });
    cp_ui.step.dependOn(b.getInstallStep());
    install_steam_step.dependOn(&cp_ui.step);

    // Install renderers
    const cp_renderer1 = b.addSystemCommand(&.{
        "cp",                                                            "-f",
        b.fmt("zig-out/lib/librenderer_sp_opengl1_{s}.so", .{arch_str}), b.fmt("{s}/renderer_sp_opengl1_{s}.so", .{ steam_realrtcw_dir, arch_str }),
    });
    cp_renderer1.step.dependOn(b.getInstallStep());
    install_steam_step.dependOn(&cp_renderer1.step);

    const cp_renderer2 = b.addSystemCommand(&.{
        "cp",                                                          "-f",
        b.fmt("zig-out/lib/librenderer_sp_rend2_{s}.so", .{arch_str}), b.fmt("{s}/renderer_sp_rend2_{s}.so", .{ steam_realrtcw_dir, arch_str }),
    });
    cp_renderer2.step.dependOn(b.getInstallStep());
    install_steam_step.dependOn(&cp_renderer2.step);

    // Install main executable
    const cp_exe = b.addSystemCommand(&.{
        "cp",                   "-f",
        "zig-out/bin/iowolfsp", b.fmt("{s}/iowolfsp.{s}", .{ steam_realrtcw_dir, arch_str }),
    });
    cp_exe.step.dependOn(b.getInstallStep());
    install_steam_step.dependOn(&cp_exe.step);

    // Copy llama.cpp libraries
    const cp_llama = b.addSystemCommand(&.{ "sh", "-c", b.fmt("cp -f code/llama.cpp/build/bin/*.so* \"{s}/\" 2>/dev/null || true", .{steam_realrtcw_dir}) });
    cp_llama.step.dependOn(b.getInstallStep());
    install_steam_step.dependOn(&cp_llama.step);

    // Create enhanced config
    const create_cfg = b.addSystemCommand(&.{
        "sh",                                                                                                                                                                        "-c",
        b.fmt("echo '// RealRTCW Enhanced Rendering Settings\nseta r_glsl \"1\"\nseta r_ssao \"1\"\nseta r_hdr \"0\"' > \"{s}/main/realrtcw_enhanced.cfg\"", .{steam_realrtcw_dir}),
    });
    create_cfg.step.dependOn(&mkdir_main.step);
    install_steam_step.dependOn(&create_cfg.step);

    // Print completion message
    const print_done = b.addSystemCommand(&.{
        "sh",                                                                                                                                                                                                                                                                                                                                                                                                                                                                      "-c",
        b.fmt("echo '' && echo 'Installation complete!' && echo 'Installed to: {s}' && echo '' && echo 'Installed files:' && echo '  - iowolfsp.{s} (executable)' && echo '  - renderer_sp_opengl1_{s}.so' && echo '  - renderer_sp_rend2_{s}.so' && echo '  - main/cgame.sp.{s}.so' && echo '  - main/qagame.sp.{s}.so' && echo '  - main/ui.sp.{s}.so' && echo '  - llama.cpp libraries'", .{ steam_realrtcw_dir, arch_str, arch_str, arch_str, arch_str, arch_str, arch_str }),
    });
    print_done.step.dependOn(&cp_exe.step);
    print_done.step.dependOn(&cp_llama.step);
    print_done.step.dependOn(&create_cfg.step);
    install_steam_step.dependOn(&print_done.step);
}

// =============================================================================
// Cflag builders
// =============================================================================

fn buildClientCflags(
    allocator: std.mem.Allocator,
    version_define: []const u8,
    arch_define: []const u8,
    use_openal: bool,
    use_curl: bool,
    use_voip: bool,
    use_codec_opus: bool,
    use_codec_vorbis: bool,
    use_mumble: bool,
    use_bloom: bool,
    use_renderer_dlopen: bool,
) []const []const u8 {
    // Base flags
    var flags = std.array_list.Managed([]const u8).init(allocator);
    flags.appendSlice(&.{
        "-Wall",
        "-fno-strict-aliasing",
        "-pipe",
        "-O3",
        "-ffast-math",
        "-DNDEBUG",
        "-DUSE_ICON",
        version_define,
        arch_define,
        "-Wno-unused-but-set-variable",
        "-Wno-strict-prototypes",
        "-DUSE_INTERNAL_JPEG=1",
        "-DUSE_LOCAL_HEADERS=1",
        "-DBOTLIB",
        "-fwrapv", // Allow integer overflow (legacy C code)
        "-fno-sanitize=undefined", // Disable UB sanitizer for legacy C code
    }) catch @panic("OOM");

    if (use_openal) {
        flags.appendSlice(&.{ "-DUSE_OPENAL=1", "-DUSE_OPENAL_DLOPEN=1" }) catch @panic("OOM");
    }
    if (use_curl) {
        flags.appendSlice(&.{ "-DUSE_CURL=1", "-DUSE_CURL_DLOPEN=1" }) catch @panic("OOM");
    }
    if (use_voip) {
        flags.append("-DUSE_VOIP=1") catch @panic("OOM");
    }
    if (use_codec_opus) {
        flags.append("-DUSE_CODEC_OPUS=1") catch @panic("OOM");
    }
    if (use_codec_vorbis) {
        flags.append("-DUSE_CODEC_VORBIS=1") catch @panic("OOM");
    }
    if (use_mumble) {
        flags.append("-DUSE_MUMBLE=1") catch @panic("OOM");
    }
    if (use_bloom) {
        flags.append("-DUSE_BLOOM=1") catch @panic("OOM");
    }
    if (use_renderer_dlopen) {
        flags.append("-DUSE_RENDERER_DLOPEN=1") catch @panic("OOM");
    }

    return flags.items;
}

fn buildServerCflags(
    allocator: std.mem.Allocator,
    version_define: []const u8,
    arch_define: []const u8,
    use_voip: bool,
) []const []const u8 {
    var flags = std.array_list.Managed([]const u8).init(allocator);
    flags.appendSlice(&.{
        "-Wall",
        "-fno-strict-aliasing",
        "-pipe",
        "-O3",
        "-ffast-math",
        "-DNDEBUG",
        version_define,
        arch_define,
        "-Wno-unused-but-set-variable",
        "-Wno-strict-prototypes",
        "-DDEDICATED=1",
        "-DBOTLIB",
    }) catch @panic("OOM");

    if (use_voip) {
        flags.append("-DUSE_VOIP=1") catch @panic("OOM");
    }

    return flags.items;
}

fn buildRendererCflags(
    allocator: std.mem.Allocator,
    version_define: []const u8,
    arch_define: []const u8,
    use_freetype: bool,
    use_bloom: bool,
) []const []const u8 {
    var flags = std.array_list.Managed([]const u8).init(allocator);
    flags.appendSlice(&.{
        "-Wall",
        "-fno-strict-aliasing",
        "-fPIC",
        "-fvisibility=hidden",
        "-O3",
        "-DNDEBUG",
        version_define,
        arch_define,
        "-DUSE_INTERNAL_JPEG=1",
        "-DUSE_RENDERER_DLOPEN=1",
        "-Wno-unused-but-set-variable",
        "-Wno-strict-prototypes",
        "-fwrapv", // Allow integer overflow (image parsers rely on this)
        "-fno-sanitize=undefined", // Disable UB sanitizer for legacy C code
    }) catch @panic("OOM");

    if (use_freetype) {
        flags.append("-DBUILD_FREETYPE=1") catch @panic("OOM");
    }
    if (use_bloom) {
        flags.append("-DUSE_BLOOM=1") catch @panic("OOM");
    }

    return flags.items;
}

// =============================================================================
// Source file lists
// =============================================================================

fn addClientSources(exe: *std.Build.Step.Compile, base_cflags: []const []const u8, use_internal_libs: bool, use_openal: bool, use_voip: bool, use_codec_opus: bool, use_codec_vorbis: bool) void {
    const client_sources: []const []const u8 = &.{
        // Client
        "code/client/cl_cgame.c",
        "code/client/cl_cin.c",
        "code/client/cl_console.c",
        "code/client/cl_input.c",
        "code/client/cl_keys.c",
        "code/client/cl_main.c",
        "code/client/cl_net_chan.c",
        "code/client/cl_parse.c",
        "code/client/cl_scrn.c",
        "code/client/cl_ui.c",
        "code/client/cl_avi.c",
        "code/client/cl_curl.c",

        // Common
        "code/qcommon/cm_load.c",
        "code/qcommon/cm_patch.c",
        "code/qcommon/cm_polylib.c",
        "code/qcommon/cm_test.c",
        "code/qcommon/cm_trace.c",
        "code/qcommon/cmd.c",
        "code/qcommon/common.c",
        "code/qcommon/cvar.c",
        "code/qcommon/files.c",
        "code/qcommon/md4.c",
        "code/qcommon/md5.c",
        "code/qcommon/msg.c",
        "code/qcommon/net_chan.c",
        "code/qcommon/net_ip.c",
        "code/qcommon/huffman.c",
        "code/qcommon/q_math.c",
        "code/qcommon/q_shared.c",
        "code/zlib-1.2.11/unzip.c",
        "code/zlib-1.2.11/ioapi.c",
        "code/qcommon/puff.c",
        "code/qcommon/vm.c",
        "code/qcommon/vm_interpreted.c",

        // Sound
        "code/client/snd_adpcm.c",
        "code/client/snd_dma.c",
        "code/client/snd_mem.c",
        "code/client/snd_mix.c",
        "code/client/snd_wavelet.c",
        "code/client/snd_main.c",
        "code/client/snd_codec.c",
        "code/client/snd_codec_wav.c",
        "code/client/snd_codec_ogg.c",
        "code/client/snd_codec_opus.c",

        // Server
        "code/server/sv_bot.c",
        "code/server/sv_ccmds.c",
        "code/server/sv_client.c",
        "code/server/sv_game.c",
        "code/server/sv_init.c",
        "code/server/sv_main.c",
        "code/server/sv_net_chan.c",
        "code/server/sv_snapshot.c",
        "code/server/sv_world.c",

        // Bot library
        "code/botlib/be_aas_bspq3.c",
        "code/botlib/be_aas_cluster.c",
        "code/botlib/be_aas_debug.c",
        "code/botlib/be_aas_entity.c",
        "code/botlib/be_aas_file.c",
        "code/botlib/be_aas_main.c",
        "code/botlib/be_aas_move.c",
        "code/botlib/be_aas_optimize.c",
        "code/botlib/be_aas_reach.c",
        "code/botlib/be_aas_route.c",
        "code/botlib/be_aas_routealt.c",
        "code/botlib/be_aas_routetable.c",
        "code/botlib/be_aas_sample.c",
        "code/botlib/be_ai_char.c",
        "code/botlib/be_ai_chat.c",
        "code/botlib/be_ai_gen.c",
        "code/botlib/be_ai_goal.c",
        "code/botlib/be_ai_move.c",
        "code/botlib/be_ai_weap.c",
        "code/botlib/be_ai_weight.c",
        "code/botlib/be_ea.c",
        "code/botlib/be_interface.c",
        "code/botlib/l_crc.c",
        "code/botlib/l_libvar.c",
        "code/botlib/l_log.c",
        "code/botlib/l_memory.c",
        "code/botlib/l_precomp.c",
        "code/botlib/l_script.c",
        "code/botlib/l_struct.c",

        // SDL
        "code/sdl/sdl_input.c",
        "code/sdl/sdl_snd.c",

        // System
        "code/sys/con_log.c",
        "code/sys/sys_main.c",
        "code/sys/sys_unix.c",
        "code/sys/con_tty.c",

        // Mumble
        "code/client/libmumblelink.c",
    };

    exe.addCSourceFiles(.{
        .files = client_sources,
        .flags = base_cflags,
    });

    // Splines (C++)
    const spline_sources: []const []const u8 = &.{
        "code/splines/math_angles.cpp",
        "code/splines/math_matrix.cpp",
        "code/splines/math_quaternion.cpp",
        "code/splines/math_vector.cpp",
        "code/splines/q_parse.cpp",
        "code/splines/splines.cpp",
        "code/splines/util_str.cpp",
    };

    exe.addCSourceFiles(.{
        .files = spline_sources,
        .flags = base_cflags,
    });

    // OpenAL
    if (use_openal) {
        exe.addCSourceFiles(.{
            .files = &.{
                "code/client/qal.c",
                "code/client/snd_openal.c",
            },
            .flags = base_cflags,
        });
    }

    // Internal zlib
    if (use_internal_libs) {
        exe.addCSourceFiles(.{
            .files = &.{
                "code/zlib-1.2.11/adler32.c",
                "code/zlib-1.2.11/crc32.c",
                "code/zlib-1.2.11/inffast.c",
                "code/zlib-1.2.11/inflate.c",
                "code/zlib-1.2.11/inftrees.c",
                "code/zlib-1.2.11/zutil.c",
            },
            .flags = &.{ "-Wall", "-O3", "-DNO_GZIP" },
        });
    }

    // Internal OGG
    if (use_internal_libs and (use_codec_opus or use_codec_vorbis)) {
        exe.addCSourceFiles(.{
            .files = &.{
                "code/libogg-1.3.3/src/bitwise.c",
                "code/libogg-1.3.3/src/framing.c",
            },
            .flags = &.{ "-Wall", "-O3" },
        });
    }

    // Internal Vorbis
    if (use_internal_libs and use_codec_vorbis) {
        exe.addCSourceFiles(.{
            .files = &.{
                "code/libvorbis-1.3.6/lib/analysis.c",
                "code/libvorbis-1.3.6/lib/bitrate.c",
                "code/libvorbis-1.3.6/lib/block.c",
                "code/libvorbis-1.3.6/lib/codebook.c",
                "code/libvorbis-1.3.6/lib/envelope.c",
                "code/libvorbis-1.3.6/lib/floor0.c",
                "code/libvorbis-1.3.6/lib/floor1.c",
                "code/libvorbis-1.3.6/lib/info.c",
                "code/libvorbis-1.3.6/lib/lookup.c",
                "code/libvorbis-1.3.6/lib/lpc.c",
                "code/libvorbis-1.3.6/lib/lsp.c",
                "code/libvorbis-1.3.6/lib/mapping0.c",
                "code/libvorbis-1.3.6/lib/mdct.c",
                "code/libvorbis-1.3.6/lib/psy.c",
                "code/libvorbis-1.3.6/lib/registry.c",
                "code/libvorbis-1.3.6/lib/res0.c",
                "code/libvorbis-1.3.6/lib/sharedbook.c",
                "code/libvorbis-1.3.6/lib/smallft.c",
                "code/libvorbis-1.3.6/lib/synthesis.c",
                "code/libvorbis-1.3.6/lib/vorbisenc.c",
                "code/libvorbis-1.3.6/lib/vorbisfile.c",
                "code/libvorbis-1.3.6/lib/window.c",
            },
            .flags = &.{ "-Wall", "-O3", "-Icode/libvorbis-1.3.6/lib" },
        });
    }

    // Internal Opus
    if (use_internal_libs and (use_codec_opus or use_voip)) {
        addOpusSources(exe);
    }

    // x86_64 specific - need base_cflags for ARCH_STRING define
    exe.addCSourceFiles(.{
        .files = &.{
            "code/asm/snapvector.c",
            "code/asm/ftola.c",
        },
        .flags = base_cflags,
    });

    // VM x86 compilation
    exe.addCSourceFiles(.{
        .files = &.{"code/qcommon/vm_x86.c"},
        .flags = base_cflags,
    });
}

fn addServerSources(exe: *std.Build.Step.Compile, base_cflags: []const []const u8, use_internal_libs: bool) void {
    const server_sources: []const []const u8 = &.{
        "code/server/sv_bot.c",
        "code/server/sv_client.c",
        "code/server/sv_ccmds.c",
        "code/server/sv_game.c",
        "code/server/sv_init.c",
        "code/server/sv_main.c",
        "code/server/sv_net_chan.c",
        "code/server/sv_snapshot.c",
        "code/server/sv_world.c",
        "code/qcommon/cm_load.c",
        "code/qcommon/cm_patch.c",
        "code/qcommon/cm_polylib.c",
        "code/qcommon/cm_test.c",
        "code/qcommon/cm_trace.c",
        "code/qcommon/cmd.c",
        "code/qcommon/common.c",
        "code/qcommon/cvar.c",
        "code/qcommon/files.c",
        "code/qcommon/md4.c",
        "code/qcommon/msg.c",
        "code/qcommon/net_chan.c",
        "code/qcommon/net_ip.c",
        "code/qcommon/huffman.c",
        "code/qcommon/q_math.c",
        "code/qcommon/q_shared.c",
        "code/zlib-1.2.11/unzip.c",
        "code/zlib-1.2.11/ioapi.c",
        "code/qcommon/puff.c",
        "code/qcommon/vm.c",
        "code/qcommon/vm_interpreted.c",
        "code/qcommon/vm_x86.c",
        "code/botlib/be_aas_bspq3.c",
        "code/botlib/be_aas_cluster.c",
        "code/botlib/be_aas_debug.c",
        "code/botlib/be_aas_entity.c",
        "code/botlib/be_aas_file.c",
        "code/botlib/be_aas_main.c",
        "code/botlib/be_aas_move.c",
        "code/botlib/be_aas_optimize.c",
        "code/botlib/be_aas_reach.c",
        "code/botlib/be_aas_route.c",
        "code/botlib/be_aas_routealt.c",
        "code/botlib/be_aas_routetable.c",
        "code/botlib/be_aas_sample.c",
        "code/botlib/be_ai_char.c",
        "code/botlib/be_ai_chat.c",
        "code/botlib/be_ai_gen.c",
        "code/botlib/be_ai_goal.c",
        "code/botlib/be_ai_move.c",
        "code/botlib/be_ai_weap.c",
        "code/botlib/be_ai_weight.c",
        "code/botlib/be_ea.c",
        "code/botlib/be_interface.c",
        "code/botlib/l_crc.c",
        "code/botlib/l_libvar.c",
        "code/botlib/l_log.c",
        "code/botlib/l_memory.c",
        "code/botlib/l_precomp.c",
        "code/botlib/l_script.c",
        "code/botlib/l_struct.c",
        "code/sys/con_log.c",
        "code/sys/sys_main.c",
        "code/sys/sys_unix.c",
        "code/sys/con_tty.c",
        "code/null/null_client.c",
        "code/null/null_input.c",
        "code/null/null_snddma.c",
    };

    exe.addCSourceFiles(.{
        .files = server_sources,
        .flags = base_cflags,
    });

    // Internal zlib
    if (use_internal_libs) {
        exe.addCSourceFiles(.{
            .files = &.{
                "code/zlib-1.2.11/adler32.c",
                "code/zlib-1.2.11/crc32.c",
                "code/zlib-1.2.11/inffast.c",
                "code/zlib-1.2.11/inflate.c",
                "code/zlib-1.2.11/inftrees.c",
                "code/zlib-1.2.11/zutil.c",
            },
            .flags = &.{ "-Wall", "-O3", "-DNO_GZIP" },
        });
    }
}

fn addRendererOpenGL1Sources(lib: *std.Build.Step.Compile, renderer_cflags: []const []const u8, use_internal_libs: bool, use_freetype: bool, use_bloom: bool) void {
    const renderer_sources: []const []const u8 = &.{
        "code/renderer/tr_animation.c",
        "code/renderer/tr_backend.c",
        "code/renderer/tr_bsp.c",
        "code/renderer/tr_cmds.c",
        "code/renderer/tr_cmesh.c",
        "code/renderer/tr_curve.c",
        "code/renderer/tr_flares.c",
        "code/renderer/tr_font.c",
        "code/renderer/tr_image.c",
        "code/renderer/tr_image_bmp.c",
        "code/renderer/tr_image_jpg.c",
        "code/renderer/tr_image_pcx.c",
        "code/renderer/tr_image_png.c",
        "code/renderer/tr_image_tga.c",
        "code/renderer/tr_init.c",
        "code/renderer/tr_light.c",
        "code/renderer/tr_main.c",
        "code/renderer/tr_marks.c",
        "code/renderer/tr_mesh.c",
        "code/renderer/tr_model.c",
        "code/renderer/tr_model_iqm.c",
        "code/renderer/tr_noise.c",
        "code/renderer/tr_scene.c",
        "code/renderer/tr_shade.c",
        "code/renderer/tr_shade_calc.c",
        "code/renderer/tr_shader.c",
        "code/renderer/tr_shadows.c",
        "code/renderer/tr_sky.c",
        "code/renderer/tr_surface.c",
        "code/renderer/tr_world.c",
        "code/renderer/tr_subs.c",
        "code/sdl/sdl_gamma.c",
        "code/sdl/sdl_glimp.c",
        "code/qcommon/q_shared.c",
        "code/qcommon/puff.c",
        "code/qcommon/q_math.c",
    };

    lib.addCSourceFiles(.{
        .files = renderer_sources,
        .flags = renderer_cflags,
    });

    if (use_bloom) {
        lib.addCSourceFiles(.{
            .files = &.{"code/renderer/tr_bloom.c"},
            .flags = renderer_cflags,
        });
    }

    // Internal JPEG
    if (use_internal_libs) {
        addJpegSources(lib);
    }

    // Internal FreeType
    if (use_internal_libs and use_freetype) {
        addFreetypeSources(lib);
    }
}

fn addRendererRend2Sources(lib: *std.Build.Step.Compile, renderer_cflags: []const []const u8, use_internal_libs: bool, use_freetype: bool) void {
    const rend2_sources: []const []const u8 = &.{
        "code/rend2/tr_animation.c",
        "code/rend2/tr_backend.c",
        "code/rend2/tr_bsp.c",
        "code/rend2/tr_cmds.c",
        "code/rend2/tr_curve.c",
        "code/rend2/tr_dsa.c",
        "code/rend2/tr_extramath.c",
        "code/rend2/tr_extensions.c",
        "code/rend2/tr_fbo.c",
        "code/rend2/tr_flares.c",
        "code/rend2/tr_font.c",
        "code/rend2/tr_glsl.c",
        "code/rend2/tr_image.c",
        "code/rend2/tr_image_bmp.c",
        "code/rend2/tr_image_jpg.c",
        "code/rend2/tr_image_pcx.c",
        "code/rend2/tr_image_png.c",
        "code/rend2/tr_image_tga.c",
        "code/rend2/tr_image_dds.c",
        "code/rend2/tr_init.c",
        "code/rend2/tr_light.c",
        "code/rend2/tr_main.c",
        "code/rend2/tr_marks.c",
        "code/rend2/tr_mesh.c",
        "code/rend2/tr_model.c",
        "code/rend2/tr_model_iqm.c",
        "code/rend2/tr_noise.c",
        "code/rend2/tr_postprocess.c",
        "code/rend2/tr_scene.c",
        "code/rend2/tr_shade.c",
        "code/rend2/tr_shade_calc.c",
        "code/rend2/tr_shader.c",
        "code/rend2/tr_shadows.c",
        "code/rend2/tr_sky.c",
        "code/rend2/tr_surface.c",
        "code/rend2/tr_vbo.c",
        "code/rend2/tr_world.c",
        "code/renderer/tr_subs.c",
        "code/sdl/sdl_gamma.c",
        "code/sdl/sdl_glimp.c",
        "code/qcommon/q_shared.c",
        "code/qcommon/puff.c",
        "code/qcommon/q_math.c",
    };

    lib.addCSourceFiles(.{
        .files = rend2_sources,
        .flags = renderer_cflags,
    });

    // Internal JPEG
    if (use_internal_libs) {
        addJpegSources(lib);
    }

    // Internal FreeType
    if (use_internal_libs and use_freetype) {
        addFreetypeSources(lib);
    }
}

fn addCgameSources(b: *std.Build, lib: *std.Build.Step.Compile, game_cflags: []const []const u8) void {
    // Allocate new array with extra defines
    const allocator = b.allocator;
    const cgame_flags = allocator.alloc([]const u8, game_cflags.len + 2) catch @panic("OOM");
    @memcpy(cgame_flags[0..game_cflags.len], game_cflags);
    cgame_flags[game_cflags.len] = "-DCGAMEDLL=1";
    cgame_flags[game_cflags.len + 1] = "-DCGAME=1";

    const cgame_sources: []const []const u8 = &.{
        "code/cgame/cg_main.c",
        "code/cgame/cg_consolecmds.c",
        "code/cgame/cg_draw.c",
        "code/cgame/cg_drawtools.c",
        "code/cgame/cg_effects.c",
        "code/cgame/cg_ents.c",
        "code/cgame/cg_event.c",
        "code/cgame/cg_flamethrower.c",
        "code/cgame/cg_info.c",
        "code/cgame/cg_localents.c",
        "code/cgame/cg_marks.c",
        "code/cgame/cg_newdraw.c",
        "code/cgame/cg_particles.c",
        "code/cgame/cg_players.c",
        "code/cgame/cg_playerstate.c",
        "code/cgame/cg_predict.c",
        "code/cgame/cg_scoreboard.c",
        "code/cgame/cg_servercmds.c",
        "code/cgame/cg_snapshot.c",
        "code/cgame/cg_sound.c",
        "code/cgame/cg_trails.c",
        "code/cgame/cg_view.c",
        "code/cgame/cg_weapons.c",
        "code/cgame/cg_atmospheric.c",
        "code/cgame/cg_polybus.c",
        "code/cgame/cg_syscalls.c",
        "code/game/bg_animation.c",
        "code/game/bg_misc.c",
        "code/game/bg_pmove.c",
        "code/game/bg_slidemove.c",
        "code/game/bg_lib.c",
        "code/ui/ui_shared.c",
        "code/qcommon/q_math.c",
        "code/qcommon/q_shared.c",
    };

    lib.addCSourceFiles(.{
        .files = cgame_sources,
        .flags = cgame_flags,
    });
}

fn addQagameSources(b: *std.Build, lib: *std.Build.Step.Compile, game_cflags: []const []const u8) void {
    // Allocate new array with extra defines
    const allocator = b.allocator;
    const qagame_flags = allocator.alloc([]const u8, game_cflags.len + 2) catch @panic("OOM");
    @memcpy(qagame_flags[0..game_cflags.len], game_cflags);
    qagame_flags[game_cflags.len] = "-DGAMEDLL=1";
    qagame_flags[game_cflags.len + 1] = "-DQAGAME=1";

    const qagame_c_sources: []const []const u8 = &.{
        "code/game/g_main.c",
        "code/game/ai_cast.c",
        "code/game/ai_cast_characters.c",
        "code/game/ai_cast_debug.c",
        "code/game/ai_cast_events.c",
        "code/game/ai_cast_fight.c",
        "code/game/ai_cast_func_attack.c",
        "code/game/ai_cast_func_boss1.c",
        "code/game/ai_cast_funcs.c",
        "code/game/ai_cast_script_actions.c",
        "code/game/ai_cast_script.c",
        "code/game/ai_cast_script_ents.c",
        "code/game/ai_cast_sight.c",
        "code/game/ai_cast_think.c",
        "code/game/ai_cast_survival.c",
        "code/game/ai_cast_func_hein.c",
        "code/game/ai_chat.c",
        "code/game/ai_cmd.c",
        "code/game/ai_dmnet.c",
        "code/game/ai_dmq3.c",
        "code/game/ai_main.c",
        "code/game/ai_team.c",
        "code/game/bg_animation.c",
        "code/game/bg_misc.c",
        "code/game/bg_pmove.c",
        "code/game/bg_slidemove.c",
        "code/game/bg_lib.c",
        "code/game/g_active.c",
        "code/game/g_alarm.c",
        "code/game/g_bot.c",
        "code/game/g_client.c",
        "code/game/g_cmds.c",
        "code/game/g_combat.c",
        "code/game/g_items.c",
        "code/game/g_mem.c",
        "code/game/g_misc.c",
        "code/game/g_missile.c",
        "code/game/g_mover.c",
        "code/game/g_props.c",
        "code/game/g_save.c",
        "code/game/g_script_actions.c",
        "code/game/g_script.c",
        "code/game/g_session.c",
        "code/game/g_spawn.c",
        "code/game/g_svcmds.c",
        "code/game/g_target.c",
        "code/game/g_team.c",
        "code/game/g_tramcar.c",
        "code/game/g_trigger.c",
        "code/game/g_utils.c",
        "code/game/g_weapon.c",
        "code/game/g_survival_buy.c",
        "code/game/g_survival_score.c",
        "code/game/g_survival_misc.c",
        "code/game/g_tts.c",
        "code/game/ai_tactical_memory.c",
        "code/game/ai_squad.c",
        "code/game/ai_strategy.c",
        "code/game/g_syscalls.c",
        "code/steamshim/steamshim_child.c",
        "code/steam/steam.c",
        "code/qcommon/q_math.c",
        "code/qcommon/q_shared.c",
    };

    lib.addCSourceFiles(.{
        .files = qagame_c_sources,
        .flags = qagame_flags,
    });

    // C++ sources (LLM integration) - need to include the same flags as C sources
    // to get ARCH_STRING and PRODUCT_VERSION defines
    const cpp_flags = allocator.alloc([]const u8, qagame_flags.len + 1) catch @panic("OOM");
    @memcpy(cpp_flags[0..qagame_flags.len], qagame_flags);
    cpp_flags[qagame_flags.len] = "-std=c++11";

    lib.addCSourceFiles(.{
        .files = &.{"code/game/ai_llm.cpp"},
        .flags = cpp_flags,
    });
}

fn addUiSources(b: *std.Build, lib: *std.Build.Step.Compile, game_cflags: []const []const u8) void {
    // Allocate new array with extra defines
    const allocator = b.allocator;
    const ui_flags = allocator.alloc([]const u8, game_cflags.len + 1) catch @panic("OOM");
    @memcpy(ui_flags[0..game_cflags.len], game_cflags);
    ui_flags[game_cflags.len] = "-DUI=1";

    const ui_sources: []const []const u8 = &.{
        "code/ui/ui_main.c",
        "code/ui/ui_atoms.c",
        "code/ui/ui_gameinfo.c",
        "code/ui/ui_players.c",
        "code/ui/ui_shared.c",
        "code/ui/ui_graphics_enhancements.c",
        "code/ui/ui_syscalls.c",
        "code/game/bg_misc.c",
        "code/game/bg_lib.c",
        "code/qcommon/q_math.c",
        "code/qcommon/q_shared.c",
    };

    lib.addCSourceFiles(.{
        .files = ui_sources,
        .flags = ui_flags,
    });
}

fn addOpusSources(exe: *std.Build.Step.Compile) void {
    const opus_flags: []const []const u8 = &.{
        "-Wall",
        "-O3",
        "-DOPUS_BUILD",
        "-DHAVE_LRINTF",
        "-DFLOATING_POINT",
        "-DFLOAT_APPROX",
        "-DUSE_ALLOCA",
    };

    // Core Opus sources
    exe.addCSourceFiles(.{
        .files = &.{
            "code/opus-1.2.1/src/opus.c",
            "code/opus-1.2.1/src/opus_decoder.c",
            "code/opus-1.2.1/src/opus_encoder.c",
            "code/opus-1.2.1/src/opus_multistream.c",
            "code/opus-1.2.1/src/opus_multistream_encoder.c",
            "code/opus-1.2.1/src/opus_multistream_decoder.c",
            "code/opus-1.2.1/src/repacketizer.c",
            "code/opus-1.2.1/src/analysis.c",
            "code/opus-1.2.1/src/mlp.c",
            "code/opus-1.2.1/src/mlp_data.c",
        },
        .flags = opus_flags,
    });

    // CELT sources
    exe.addCSourceFiles(.{
        .files = &.{
            "code/opus-1.2.1/celt/bands.c",
            "code/opus-1.2.1/celt/celt.c",
            "code/opus-1.2.1/celt/celt_encoder.c",
            "code/opus-1.2.1/celt/celt_decoder.c",
            "code/opus-1.2.1/celt/cwrs.c",
            "code/opus-1.2.1/celt/entcode.c",
            "code/opus-1.2.1/celt/entdec.c",
            "code/opus-1.2.1/celt/entenc.c",
            "code/opus-1.2.1/celt/kiss_fft.c",
            "code/opus-1.2.1/celt/laplace.c",
            "code/opus-1.2.1/celt/mathops.c",
            "code/opus-1.2.1/celt/mdct.c",
            "code/opus-1.2.1/celt/modes.c",
            "code/opus-1.2.1/celt/pitch.c",
            "code/opus-1.2.1/celt/celt_lpc.c",
            "code/opus-1.2.1/celt/quant_bands.c",
            "code/opus-1.2.1/celt/rate.c",
            "code/opus-1.2.1/celt/vq.c",
        },
        .flags = opus_flags,
    });

    // SILK sources
    exe.addCSourceFiles(.{
        .files = &.{
            "code/opus-1.2.1/silk/CNG.c",
            "code/opus-1.2.1/silk/code_signs.c",
            "code/opus-1.2.1/silk/init_decoder.c",
            "code/opus-1.2.1/silk/decode_core.c",
            "code/opus-1.2.1/silk/decode_frame.c",
            "code/opus-1.2.1/silk/decode_parameters.c",
            "code/opus-1.2.1/silk/decode_indices.c",
            "code/opus-1.2.1/silk/decode_pulses.c",
            "code/opus-1.2.1/silk/decoder_set_fs.c",
            "code/opus-1.2.1/silk/dec_API.c",
            "code/opus-1.2.1/silk/enc_API.c",
            "code/opus-1.2.1/silk/encode_indices.c",
            "code/opus-1.2.1/silk/encode_pulses.c",
            "code/opus-1.2.1/silk/gain_quant.c",
            "code/opus-1.2.1/silk/interpolate.c",
            "code/opus-1.2.1/silk/LP_variable_cutoff.c",
            "code/opus-1.2.1/silk/NLSF_decode.c",
            "code/opus-1.2.1/silk/NSQ.c",
            "code/opus-1.2.1/silk/NSQ_del_dec.c",
            "code/opus-1.2.1/silk/PLC.c",
            "code/opus-1.2.1/silk/shell_coder.c",
            "code/opus-1.2.1/silk/tables_gain.c",
            "code/opus-1.2.1/silk/tables_LTP.c",
            "code/opus-1.2.1/silk/tables_NLSF_CB_NB_MB.c",
            "code/opus-1.2.1/silk/tables_NLSF_CB_WB.c",
            "code/opus-1.2.1/silk/tables_other.c",
            "code/opus-1.2.1/silk/tables_pitch_lag.c",
            "code/opus-1.2.1/silk/tables_pulses_per_block.c",
            "code/opus-1.2.1/silk/VAD.c",
            "code/opus-1.2.1/silk/control_audio_bandwidth.c",
            "code/opus-1.2.1/silk/quant_LTP_gains.c",
            "code/opus-1.2.1/silk/VQ_WMat_EC.c",
            "code/opus-1.2.1/silk/HP_variable_cutoff.c",
            "code/opus-1.2.1/silk/NLSF_encode.c",
            "code/opus-1.2.1/silk/NLSF_VQ.c",
            "code/opus-1.2.1/silk/NLSF_unpack.c",
            "code/opus-1.2.1/silk/NLSF_del_dec_quant.c",
            "code/opus-1.2.1/silk/process_NLSFs.c",
            "code/opus-1.2.1/silk/stereo_LR_to_MS.c",
            "code/opus-1.2.1/silk/stereo_MS_to_LR.c",
            "code/opus-1.2.1/silk/check_control_input.c",
            "code/opus-1.2.1/silk/control_SNR.c",
            "code/opus-1.2.1/silk/init_encoder.c",
            "code/opus-1.2.1/silk/control_codec.c",
            "code/opus-1.2.1/silk/A2NLSF.c",
            "code/opus-1.2.1/silk/ana_filt_bank_1.c",
            "code/opus-1.2.1/silk/biquad_alt.c",
            "code/opus-1.2.1/silk/bwexpander_32.c",
            "code/opus-1.2.1/silk/bwexpander.c",
            "code/opus-1.2.1/silk/debug.c",
            "code/opus-1.2.1/silk/decode_pitch.c",
            "code/opus-1.2.1/silk/inner_prod_aligned.c",
            "code/opus-1.2.1/silk/lin2log.c",
            "code/opus-1.2.1/silk/log2lin.c",
            "code/opus-1.2.1/silk/LPC_analysis_filter.c",
            "code/opus-1.2.1/silk/LPC_inv_pred_gain.c",
            "code/opus-1.2.1/silk/table_LSF_cos.c",
            "code/opus-1.2.1/silk/NLSF2A.c",
            "code/opus-1.2.1/silk/NLSF_stabilize.c",
            "code/opus-1.2.1/silk/NLSF_VQ_weights_laroia.c",
            "code/opus-1.2.1/silk/pitch_est_tables.c",
            "code/opus-1.2.1/silk/resampler.c",
            "code/opus-1.2.1/silk/resampler_down2_3.c",
            "code/opus-1.2.1/silk/resampler_down2.c",
            "code/opus-1.2.1/silk/resampler_private_AR2.c",
            "code/opus-1.2.1/silk/resampler_private_down_FIR.c",
            "code/opus-1.2.1/silk/resampler_private_IIR_FIR.c",
            "code/opus-1.2.1/silk/resampler_private_up2_HQ.c",
            "code/opus-1.2.1/silk/resampler_rom.c",
            "code/opus-1.2.1/silk/sigm_Q15.c",
            "code/opus-1.2.1/silk/sort.c",
            "code/opus-1.2.1/silk/sum_sqr_shift.c",
            "code/opus-1.2.1/silk/stereo_decode_pred.c",
            "code/opus-1.2.1/silk/stereo_encode_pred.c",
            "code/opus-1.2.1/silk/stereo_find_predictor.c",
            "code/opus-1.2.1/silk/stereo_quant_pred.c",
            "code/opus-1.2.1/silk/LPC_fit.c",
        },
        .flags = opus_flags,
    });

    // SILK float sources
    exe.addCSourceFiles(.{
        .files = &.{
            "code/opus-1.2.1/silk/float/apply_sine_window_FLP.c",
            "code/opus-1.2.1/silk/float/corrMatrix_FLP.c",
            "code/opus-1.2.1/silk/float/encode_frame_FLP.c",
            "code/opus-1.2.1/silk/float/find_LPC_FLP.c",
            "code/opus-1.2.1/silk/float/find_LTP_FLP.c",
            "code/opus-1.2.1/silk/float/find_pitch_lags_FLP.c",
            "code/opus-1.2.1/silk/float/find_pred_coefs_FLP.c",
            "code/opus-1.2.1/silk/float/LPC_analysis_filter_FLP.c",
            "code/opus-1.2.1/silk/float/LTP_analysis_filter_FLP.c",
            "code/opus-1.2.1/silk/float/LTP_scale_ctrl_FLP.c",
            "code/opus-1.2.1/silk/float/noise_shape_analysis_FLP.c",
            "code/opus-1.2.1/silk/float/process_gains_FLP.c",
            "code/opus-1.2.1/silk/float/regularize_correlations_FLP.c",
            "code/opus-1.2.1/silk/float/residual_energy_FLP.c",
            "code/opus-1.2.1/silk/float/warped_autocorrelation_FLP.c",
            "code/opus-1.2.1/silk/float/wrappers_FLP.c",
            "code/opus-1.2.1/silk/float/autocorrelation_FLP.c",
            "code/opus-1.2.1/silk/float/burg_modified_FLP.c",
            "code/opus-1.2.1/silk/float/bwexpander_FLP.c",
            "code/opus-1.2.1/silk/float/energy_FLP.c",
            "code/opus-1.2.1/silk/float/inner_product_FLP.c",
            "code/opus-1.2.1/silk/float/k2a_FLP.c",
            "code/opus-1.2.1/silk/float/LPC_inv_pred_gain_FLP.c",
            "code/opus-1.2.1/silk/float/pitch_analysis_core_FLP.c",
            "code/opus-1.2.1/silk/float/scale_copy_vector_FLP.c",
            "code/opus-1.2.1/silk/float/scale_vector_FLP.c",
            "code/opus-1.2.1/silk/float/schur_FLP.c",
            "code/opus-1.2.1/silk/float/sort_FLP.c",
        },
        .flags = opus_flags,
    });

    // Opusfile sources
    exe.addCSourceFiles(.{
        .files = &.{
            "code/opusfile-0.9/src/info.c",
            "code/opusfile-0.9/src/internal.c",
            "code/opusfile-0.9/src/opusfile.c",
            "code/opusfile-0.9/src/stream.c",
        },
        .flags = &.{ "-Wall", "-O3" },
    });
}

fn addJpegSources(lib: *std.Build.Step.Compile) void {
    const jpeg_sources: []const []const u8 = &.{
        "code/jpeg-8c/jaricom.c",
        "code/jpeg-8c/jcapimin.c",
        "code/jpeg-8c/jcapistd.c",
        "code/jpeg-8c/jcarith.c",
        "code/jpeg-8c/jccoefct.c",
        "code/jpeg-8c/jccolor.c",
        "code/jpeg-8c/jcdctmgr.c",
        "code/jpeg-8c/jchuff.c",
        "code/jpeg-8c/jcinit.c",
        "code/jpeg-8c/jcmainct.c",
        "code/jpeg-8c/jcmarker.c",
        "code/jpeg-8c/jcmaster.c",
        "code/jpeg-8c/jcomapi.c",
        "code/jpeg-8c/jcparam.c",
        "code/jpeg-8c/jcprepct.c",
        "code/jpeg-8c/jcsample.c",
        "code/jpeg-8c/jctrans.c",
        "code/jpeg-8c/jdapimin.c",
        "code/jpeg-8c/jdapistd.c",
        "code/jpeg-8c/jdarith.c",
        "code/jpeg-8c/jdatadst.c",
        "code/jpeg-8c/jdatasrc.c",
        "code/jpeg-8c/jdcoefct.c",
        "code/jpeg-8c/jdcolor.c",
        "code/jpeg-8c/jddctmgr.c",
        "code/jpeg-8c/jdhuff.c",
        "code/jpeg-8c/jdinput.c",
        "code/jpeg-8c/jdmainct.c",
        "code/jpeg-8c/jdmarker.c",
        "code/jpeg-8c/jdmaster.c",
        "code/jpeg-8c/jdmerge.c",
        "code/jpeg-8c/jdpostct.c",
        "code/jpeg-8c/jdsample.c",
        "code/jpeg-8c/jdtrans.c",
        "code/jpeg-8c/jerror.c",
        "code/jpeg-8c/jfdctflt.c",
        "code/jpeg-8c/jfdctfst.c",
        "code/jpeg-8c/jfdctint.c",
        "code/jpeg-8c/jidctflt.c",
        "code/jpeg-8c/jidctfst.c",
        "code/jpeg-8c/jidctint.c",
        "code/jpeg-8c/jmemmgr.c",
        "code/jpeg-8c/jmemnobs.c",
        "code/jpeg-8c/jquant1.c",
        "code/jpeg-8c/jquant2.c",
        "code/jpeg-8c/jutils.c",
    };

    lib.addCSourceFiles(.{
        .files = jpeg_sources,
        .flags = &.{
            "-Wall",
            "-fPIC",
            "-O3",
            "-Wno-shift-negative-value",
            "-fwrapv", // Allow signed integer overflow (JPEG lib relies on this)
        },
    });
}

fn addFreetypeSources(lib: *std.Build.Step.Compile) void {
    const freetype_sources: []const []const u8 = &.{
        "code/freetype-2.9/src/base/ftsystem.c",
        "code/freetype-2.9/src/base/ftdebug.c",
        "code/freetype-2.9/src/base/ftinit.c",
        "code/freetype-2.9/src/base/ftbase.c",
        "code/freetype-2.9/src/base/ftbbox.c",
        "code/freetype-2.9/src/base/ftbdf.c",
        "code/freetype-2.9/src/base/ftbitmap.c",
        "code/freetype-2.9/src/base/ftcid.c",
        "code/freetype-2.9/src/base/ftfntfmt.c",
        "code/freetype-2.9/src/base/ftfstype.c",
        "code/freetype-2.9/src/base/ftgasp.c",
        "code/freetype-2.9/src/base/ftglyph.c",
        "code/freetype-2.9/src/base/ftgxval.c",
        "code/freetype-2.9/src/base/ftlcdfil.c",
        "code/freetype-2.9/src/base/ftmm.c",
        "code/freetype-2.9/src/base/ftotval.c",
        "code/freetype-2.9/src/base/ftpatent.c",
        "code/freetype-2.9/src/base/ftpfr.c",
        "code/freetype-2.9/src/base/ftstroke.c",
        "code/freetype-2.9/src/base/ftsynth.c",
        "code/freetype-2.9/src/base/fttype1.c",
        "code/freetype-2.9/src/base/ftwinfnt.c",
        "code/freetype-2.9/src/truetype/truetype.c",
        "code/freetype-2.9/src/type1/type1.c",
        "code/freetype-2.9/src/cff/cff.c",
        "code/freetype-2.9/src/cid/type1cid.c",
        "code/freetype-2.9/src/pfr/pfr.c",
        "code/freetype-2.9/src/type42/type42.c",
        "code/freetype-2.9/src/winfonts/winfnt.c",
        "code/freetype-2.9/src/pcf/pcf.c",
        "code/freetype-2.9/src/bdf/bdf.c",
        "code/freetype-2.9/src/sfnt/sfnt.c",
        "code/freetype-2.9/src/autofit/autofit.c",
        "code/freetype-2.9/src/pshinter/pshinter.c",
        "code/freetype-2.9/src/raster/raster.c",
        "code/freetype-2.9/src/smooth/smooth.c",
        "code/freetype-2.9/src/cache/ftcache.c",
        "code/freetype-2.9/src/gzip/ftgzip.c",
        "code/freetype-2.9/src/lzw/ftlzw.c",
        "code/freetype-2.9/src/bzip2/ftbzip2.c",
        "code/freetype-2.9/src/psaux/psaux.c",
        "code/freetype-2.9/src/psnames/psnames.c",
    };

    lib.addCSourceFiles(.{
        .files = freetype_sources,
        .flags = &.{
            "-Wall",
            "-fPIC",
            "-O3",
            "-DFT2_BUILD_LIBRARY",
            "-Wno-dangling-pointer",
        },
    });
}

// =============================================================================
// Include paths
// =============================================================================

fn addCommonIncludes(compile: *std.Build.Step.Compile, use_internal_libs: bool) void {
    compile.addIncludePath(.{ .cwd_relative = "code" });
    compile.addIncludePath(.{ .cwd_relative = "code/qcommon" });
    compile.addIncludePath(.{ .cwd_relative = "code/client" });
    compile.addIncludePath(.{ .cwd_relative = "code/server" });
    compile.addIncludePath(.{ .cwd_relative = "code/renderer" });
    compile.addIncludePath(.{ .cwd_relative = "code/botlib" });
    compile.addIncludePath(.{ .cwd_relative = "code/splines" });
    compile.addIncludePath(.{ .cwd_relative = "code/sys" });
    compile.addIncludePath(.{ .cwd_relative = "code/sdl" });

    if (use_internal_libs) {
        compile.addIncludePath(.{ .cwd_relative = "code/SDL2/include" });
        compile.addIncludePath(.{ .cwd_relative = "code/AL" });
        compile.addIncludePath(.{ .cwd_relative = "code/zlib-1.2.11" });
        compile.addIncludePath(.{ .cwd_relative = "code/jpeg-8c" });
        compile.addIncludePath(.{ .cwd_relative = "code/libogg-1.3.3/include" });
        compile.addIncludePath(.{ .cwd_relative = "code/libvorbis-1.3.6/include" });
        // Note: code/libvorbis-1.3.6/lib is NOT added here because its mdct.h
        // conflicts with opus's mdct.h. Vorbis sources include it via -I flag.
        compile.addIncludePath(.{ .cwd_relative = "code/opus-1.2.1/include" });
        compile.addIncludePath(.{ .cwd_relative = "code/opus-1.2.1/celt" });
        compile.addIncludePath(.{ .cwd_relative = "code/opus-1.2.1/silk" });
        compile.addIncludePath(.{ .cwd_relative = "code/opus-1.2.1/silk/float" });
        compile.addIncludePath(.{ .cwd_relative = "code/opusfile-0.9/include" });
        compile.addIncludePath(.{ .cwd_relative = "code/freetype-2.9/include" });
    }
}

fn addRendererIncludes(compile: *std.Build.Step.Compile, use_internal_libs: bool, use_freetype: bool) void {
    compile.addIncludePath(.{ .cwd_relative = "code" });
    compile.addIncludePath(.{ .cwd_relative = "code/qcommon" });
    compile.addIncludePath(.{ .cwd_relative = "code/renderer" });
    compile.addIncludePath(.{ .cwd_relative = "code/rend2" });
    compile.addIncludePath(.{ .cwd_relative = "code/sdl" });

    if (use_internal_libs) {
        compile.addIncludePath(.{ .cwd_relative = "code/SDL2/include" });
        compile.addIncludePath(.{ .cwd_relative = "code/jpeg-8c" });
        if (use_freetype) {
            compile.addIncludePath(.{ .cwd_relative = "code/freetype-2.9/include" });
        }
    }
}

fn addGameModuleIncludes(compile: *std.Build.Step.Compile) void {
    compile.addIncludePath(.{ .cwd_relative = "code" });
    compile.addIncludePath(.{ .cwd_relative = "code/qcommon" });
    compile.addIncludePath(.{ .cwd_relative = "code/game" });
    compile.addIncludePath(.{ .cwd_relative = "code/cgame" });
    compile.addIncludePath(.{ .cwd_relative = "code/ui" });
    compile.addIncludePath(.{ .cwd_relative = "code/botlib" });
    compile.addIncludePath(.{ .cwd_relative = "code/steamshim" });
    compile.addIncludePath(.{ .cwd_relative = "code/steam" });
    compile.addIncludePath(.{ .cwd_relative = "code/llama.cpp/include" });
    compile.addIncludePath(.{ .cwd_relative = "code/llama.cpp/ggml/include" });
}
