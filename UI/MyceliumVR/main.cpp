/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Application.h"
#include "Engine/Engine.h"
#include "Engine/SDKGenerator.h"
#include "Engine/ScriptRuntime.h"
#include "Engine/VirtualFileSystem.h"
#include "Engine/VulkanProbe.h"
#include "Engine/WorldManifest.h"
#include "WebContentView.h"

#include <LibCore/Environment.h>
#include <LibCore/Directory.h>
#include <LibCore/EventLoop.h>
#include <LibMain/Main.h>
#include <LibURL/Parser.h>
#include <LibURL/URL.h>
#include <LibWebView/Utilities.h>

#include <AK/LexicalPath.h>
#include <SDL3/SDL.h>

static Optional<StringView> take_option_value(Main::Arguments arguments, StringView option_name)
{
    auto option_with_equals = MUST(String::formatted("{}=", option_name));
    for (auto i = 1; i < arguments.argc; ++i) {
        StringView argument { arguments.argv[i], strlen(arguments.argv[i]) };
        if (argument.starts_with(option_with_equals))
            return argument.substring_view(option_name.length() + 1);
        if (argument == option_name && i + 1 < arguments.argc)
            return StringView { arguments.argv[i + 1], strlen(arguments.argv[i + 1]) };
    }
    return {};
}

ErrorOr<int> ladybird_main(Main::Arguments arguments)
{
    if (auto sdk_generation_target = take_option_value(arguments, "--generate-sdk"sv); sdk_generation_target.has_value()) {
        auto sdk_output_directory = take_option_value(arguments, "--sdk-output"sv).value_or("UI/MyceliumVR/sdk"sv);
        TRY(MyceliumVR::generate_sdk(*sdk_generation_target, sdk_output_directory));
        outln("Generated MyceliumVR SDK target '{}' in '{}'.", *sdk_generation_target, sdk_output_directory);
        return 0;
    }

    auto app = TRY(MyceliumVR::Application::create(arguments));

    if (!app->sdk_generation_target().is_empty()) {
        TRY(MyceliumVR::generate_sdk(app->sdk_generation_target(), app->sdk_output_directory()));
        outln("Generated MyceliumVR SDK target '{}' in '{}'.", app->sdk_generation_target(), app->sdk_output_directory());
        return 0;
    }

    if (app->vr_mode_requested()) {
        outln("VR mode requested but not yet implemented. Coming soon!");
        return 1;
    }

    outln("MyceliumVR starting in windowed mode...");

    // Copy default config files (content filters, etc.)
    auto config_dir = ByteString::formatted("{}/.config/MyceliumVR",
        Core::Environment::get("HOME"sv).value_or("/tmp"sv));
    WebView::copy_default_config_files(config_dir);

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        warnln("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }

    SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
    if (app->vulkan_probe_requested())
        window_flags = static_cast<SDL_WindowFlags>(window_flags | SDL_WINDOW_VULKAN);

    SDL_Window* window = SDL_CreateWindow("MyceliumVR", 1280, 720, window_flags);
    if (!window) {
        warnln("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (app->vulkan_probe_requested()) {
        TRY(MyceliumVR::run_vulkan_probe(*window));
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        warnln("SDL_CreateRenderer failed: {}", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    MyceliumVR::Engine engine(*renderer);
    MyceliumVR::VirtualFileSystem virtual_file_system;
    Optional<MyceliumVR::WorldManifest> world_manifest;
    String world_mount_root;

    if (!app->world_path().is_empty()) {
        TRY(virtual_file_system.mount_directory("world://_boot/"sv, ByteString(app->world_path())));
        world_manifest = TRY(MyceliumVR::load_world_manifest(virtual_file_system, "world://_boot/world.json"sv));
        world_mount_root = TRY(MyceliumVR::package_mount_root(world_manifest->package_name));
        TRY(virtual_file_system.mount_directory(world_mount_root, ByteString(app->world_path())));
        auto state_mount_root = MUST(String::formatted("state://{}/", world_manifest->package_name));
        auto state_path = LexicalPath::join(app->world_path(), "state"sv);
        TRY(Core::Directory::create(state_path, Core::Directory::CreateDirectories::Yes));
        TRY(virtual_file_system.mount_directory(state_mount_root, state_path.string(), MyceliumVR::MountPermissions::ReadWrite));
        engine.session().reset_active_world(world_manifest->name);
        outln("Mounted world '{}' from '{}' as {}.", world_manifest->name, app->world_path(), world_mount_root);
        outln("Mounted writable world state as {}.", state_mount_root);
    }

    MyceliumVR::ScriptRuntime script_runtime(engine.active_world(), &virtual_file_system);
    TRY(script_runtime.initialize());

    if (app->has_script_path_override()) {
        TRY(script_runtime.load_script(app->script_path()));
    } else if (world_manifest.has_value()) {
        auto script_path = TRY(MyceliumVR::resolve_world_relative_path(world_mount_root, world_manifest->entry_script));
        auto source = TRY(virtual_file_system.read_file(script_path));
        outln("Booting world script {}.", script_path);
        TRY(script_runtime.load_script_source(move(source), script_path));
    } else {
        TRY(script_runtime.load_script(app->script_path()));
    }

    auto view = make<MyceliumVR::WebContentView>(renderer, 1280, 720);
    auto const& browser_urls = WebView::Application::browser_options().urls;
    Optional<URL::URL> panel_url;
    if (!browser_urls.is_empty()) {
        panel_url = browser_urls.first();
    } else {
        auto default_url = URL::Parser::basic_parse("http://example.com"sv);
        if (default_url.has_value())
            panel_url = default_url.release_value();
    }

    if (panel_url.has_value())
        view->load(panel_url.value());

    outln("Loading {} — window should appear.", panel_url.has_value() ? panel_url->serialize() : "(invalid URL)"_string);

    bool running = true;
    SDL_Event event;
    auto last_ticks = SDL_GetTicksNS();
    while (running) {
        // Pump Ladybird's event loop briefly each frame
        Core::EventLoop::current().pump(Core::EventLoop::WaitMode::PollForEvents);

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
                running = false;
            if (event.type == SDL_EVENT_WINDOW_RESIZED) {
                view->resize(event.window.data1, event.window.data2);
                engine.resize(event.window.data1, event.window.data2);
            }
            view->handle_sdl_event(event);
        }

        auto now = SDL_GetTicksNS();
        auto delta_time = static_cast<double>(now - last_ticks) / 1'000'000'000.0;
        last_ticks = now;
        script_runtime.update(delta_time);

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        view->paint(renderer);
        engine.render();

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
