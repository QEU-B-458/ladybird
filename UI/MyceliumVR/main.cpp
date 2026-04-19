/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Application.h"
#include "Engine/Engine.h"
#include "Rendering/Web/OverlayManager.h"
#include "Rendering/Backend/VulkanProbe.h"
#include "Rendering/Web/WebViewManager.h"
#include "Scripting/SDKGenerator.h"
#include "Scripting/ScriptRuntime.h"
#include "Support/InputState.h"
#include "Support/VirtualFileSystem.h"
#include "World/WorldManifest.h"

#include <LibCore/Environment.h>
#include <LibCore/Directory.h>
#include <LibCore/EventLoop.h>
#include <LibMain/Main.h>
#include <LibWebView/Utilities.h>

#include <AK/LexicalPath.h>
#include <AK/Optional.h>
#include <SDL3/SDL.h>
#include <signal.h>

#if defined(TRACY_ENABLE)
#    include <tracy/Tracy.hpp>
#endif

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

static MyceliumVR::ShadowQuality parse_shadow_quality(StringView value)
{
    auto parsed = value.to_number<i32>();
    if (!parsed.has_value())
        return MyceliumVR::default_shadow_quality;
    if (*parsed < MyceliumVR::min_shadow_quality)
        return MyceliumVR::min_shadow_quality;
    if (*parsed > MyceliumVR::max_shadow_quality)
        return MyceliumVR::max_shadow_quality;
    return *parsed;
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

    MyceliumVR::World::register_meta();

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
    else
        window_flags = static_cast<SDL_WindowFlags>(window_flags | SDL_WINDOW_VULKAN);

    SDL_Window* window = SDL_CreateWindow("MyceliumVR", 1280, 720, window_flags);
    if (!window) {
        warnln("SDL_CreateWindow failed: {}", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    auto set_mouse_capture = [&](bool enabled) {
        if (!SDL_SetWindowMouseGrab(window, enabled))
            warnln("SDL_SetWindowMouseGrab({}) failed: {}", enabled, SDL_GetError());
        if (!SDL_SetWindowRelativeMouseMode(window, enabled))
            warnln("SDL_SetWindowRelativeMouseMode({}) failed: {}", enabled, SDL_GetError());
    };
    set_mouse_capture(true);

    if (app->vulkan_probe_requested()) {
        TRY(MyceliumVR::run_vulkan_probe(*window));
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

    {
        MyceliumVR::VirtualFileSystem virtual_file_system;
        MyceliumVR::Engine engine(*window, &virtual_file_system);
        engine.set_shadow_quality(parse_shadow_quality(app->shadow_quality()));
        MyceliumVR::InputState input_state;
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

        MyceliumVR::ScriptRuntime script_runtime(
            engine.active_world(),
            &virtual_file_system,
            &input_state,
            [&](MyceliumVR::VulkanRenderer::CameraState const& camera_state) {
                engine.set_camera_state(camera_state);
            },
            [&]() {
                return engine.camera_state();
            },
            [&](MyceliumVR::VulkanRenderer::SceneLightData const& light) {
                engine.set_scene_light(light);
            },
            [&]() {
                return engine.scene_light();
            });
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
        TRY(script_runtime.load_control_script(ByteString("UI/MyceliumVR/scripts/controls.js")));

        MyceliumVR::WebViewManager web_view_manager;
        web_view_manager.sync_world(engine.active_world());
        outln("WebViewManager is running for in-world panel textures.");

        MyceliumVR::OverlayManager overlay_manager;
        bool window_has_focus = true;

        outln("Loading window UI and managed in-world webviews.");

        bool running = true;
        SDL_Event event;
        auto last_ticks = SDL_GetTicksNS();
        auto request_shutdown = [&]() {
            if (!running)
                return;
            running = false;
            Core::EventLoop::current().quit(0);
        };
        auto update_mouse_capture = [&]() {
            auto should_capture_mouse = window_has_focus && !overlay_manager.is_focused();
            set_mouse_capture(should_capture_mouse);
        };
        auto sync_window_surfaces = [&](Optional<SDL_Event> const& resize_event = {}) -> ErrorOr<void> {
            int drawable_width = 0;
            int drawable_height = 0;
            if (resize_event.has_value() && (resize_event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || resize_event->type == SDL_EVENT_WINDOW_RESIZED)) {
                drawable_width = resize_event->window.data1;
                drawable_height = resize_event->window.data2;
            }
            if (drawable_width <= 0 || drawable_height <= 0) {
                if (!SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height))
                    SDL_GetWindowSize(window, &drawable_width, &drawable_height);
            }
            if (drawable_width <= 0 || drawable_height <= 0)
                return {};
            engine.resize(drawable_width, drawable_height);
            web_view_manager.resize(drawable_width, drawable_height);
            if (!overlay_manager.is_initialized())
                TRY(overlay_manager.initialize(drawable_width, drawable_height));
            else
                overlay_manager.resize(drawable_width, drawable_height);
            return {};
        };
        update_mouse_capture();
        TRY(sync_window_surfaces());

        auto sigint_handler = Core::EventLoop::register_signal(SIGINT, [&](int) {
            request_shutdown();
        });
        auto sigterm_handler = Core::EventLoop::register_signal(SIGTERM, [&](int) {
            request_shutdown();
        });

        while (running) {
#if defined(TRACY_ENABLE)
            ZoneScopedN("Frame/Main");
#endif
            auto frame_start_ticks = SDL_GetTicksNS();
            input_state.begin_frame();

            Core::EventLoop::current().pump(Core::EventLoop::WaitMode::PollForEvents);

            {
#if defined(TRACY_ENABLE)
                ZoneScopedN("Input/Events");
#endif
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_EVENT_QUIT)
                        request_shutdown();
                    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1) {
                        overlay_manager.toggle_visibility();
                        update_mouse_capture();
                        continue;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F2) {
                        overlay_manager.toggle_focus();
                        update_mouse_capture();
                        continue;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE)
                        request_shutdown();
                    if (event.type == SDL_EVENT_WINDOW_SHOWN || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_RESIZED)
                        TRY(sync_window_surfaces(event));
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                        window_has_focus = true;
                        update_mouse_capture();
                    }
                    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                        window_has_focus = false;
                        update_mouse_capture();
                    }

                    auto overlay_consumes_input = overlay_manager.should_route_input(event);
                    if (!overlay_consumes_input)
                        input_state.handle_sdl_event(event);

                    if (!overlay_consumes_input)
                        web_view_manager.handle_sdl_event(event);
                    overlay_manager.handle_sdl_event(event);
                }
            }

            if (Core::EventLoop::current().was_exit_requested())
                break;

            auto delta_time = static_cast<double>(frame_start_ticks - last_ticks) / 1'000'000'000.0;
            last_ticks = frame_start_ticks;

            script_runtime.update(delta_time);

            auto fps = delta_time > 0.0 ? (1.0 / delta_time) : 0.0;
            auto camera_state = engine.camera_state();
            web_view_manager.sync_world(engine.active_world());
            overlay_manager.update_stats({
                .fps = fps,
                .frame_time_ms = delta_time * 1000.0,
                .entity_count = engine.active_world().alive_entity_count(),
                .camera_state = camera_state,
            });

            if (web_view_manager.has_active_panel()) {
                if (auto snapshot = TRY(web_view_manager.snapshot_active_panel_if_needed()); snapshot.has_value())
                    engine.renderer().set_panel_bitmap_view({
                        .bitmap = snapshot->bitmap,
                        .width = static_cast<u32>(snapshot->width),
                        .height = static_cast<u32>(snapshot->height),
                    });
            } else {
                engine.renderer().clear_panel_bitmap();
            }

            if (!overlay_manager.is_initialized() || !overlay_manager.is_visible()) {
                engine.renderer().clear_overlay_bitmap();
            } else if (auto overlay_view = TRY(overlay_manager.snapshot_overlay_view()); overlay_view.has_value()) {
                engine.renderer().set_overlay_bitmap_view({
                    .bitmap = overlay_view->bitmap,
                    .width = static_cast<u32>(overlay_view->width),
                    .height = static_cast<u32>(overlay_view->height),
                });
            }

            TRY(engine.render());

#if defined(TRACY_ENABLE)
            FrameMark;
#endif
        }

        Core::EventLoop::unregister_signal(sigterm_handler);
        Core::EventLoop::unregister_signal(sigint_handler);
        set_mouse_capture(false);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
