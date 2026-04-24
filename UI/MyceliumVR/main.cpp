/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Application.h"
#include "Engine/Engine.h"
#include "Rendering/Web/OverlayManager.h"
#include "Rendering/Backend/VulkanProbe.h"
#include "Scripting/SDKGenerator.h"
#include "Support/InputState.h"
#include "Support/VirtualFileSystem.h"

#include <LibCore/Environment.h>
#include <LibCore/EventLoop.h>
#include <LibMain/Main.h>
#include <LibWebView/Utilities.h>

#include <AK/Optional.h>
#include <SDL3/SDL.h>
#include <signal.h>

#include <UI/MyceliumVR/Support/Profiling.h>

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

static bool brute_force_overlay_sync_enabled()
{
    auto value = Core::Environment::get("MYCELIUMVR_OVERLAY_BRUTE_FORCE_SYNC"sv);
    if (!value.has_value())
        return false;
    return value.value() == "1"sv || value.value().equals_ignoring_ascii_case("true"sv);
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
    MyceliumVR::InputState input_state;
    MyceliumVR::Engine engine(*window, &virtual_file_system);
    engine.world_management_system().set_in_process_mode(!app->subprocess_mode_requested());
    TRY(engine.initialize_world_management_system(&virtual_file_system, &input_state));
    engine.set_shadow_quality(parse_shadow_quality(app->shadow_quality()));

    auto get_window_pixel_size = [&]() {
        int drawable_width = 0;
        int drawable_height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &drawable_width, &drawable_height))
            SDL_GetWindowSize(window, &drawable_width, &drawable_height);
        return AK::Array<int, 2> { drawable_width, drawable_height };
    };

    TRY(engine.world_management_system().boot({
        .world_path = ByteString(app->world_path()),
        .script_path = app->script_path(),
        .control_script_path = ByteString("UI/MyceliumVR/scripts/controls.js"),
        .has_script_path_override = app->has_script_path_override(),
    }));

    if (auto* runtime = engine.world_management_system().foreground_runtime()) {
        runtime->with_world_lock([&](auto& world) {
            engine.renderer().sync_web_views(runtime->id(), world);
        });
    }
    outln("WebViewManager is running for in-world panel textures.");
    bool const force_overlay_device_idle = brute_force_overlay_sync_enabled();
    if (force_overlay_device_idle)
        outln("Overlay brute-force sync enabled: device idle before sampling external overlay image.");
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
            auto should_capture_mouse = window_has_focus && !engine.world_management_system().overlay_manager().is_focused();
            set_mouse_capture(should_capture_mouse);
        };
        auto sync_window_surfaces = [&](Optional<SDL_Event> const& resize_event = {}) -> ErrorOr<void> {
            int drawable_width = 0;
            int drawable_height = 0;
            if (resize_event.has_value() && resize_event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                drawable_width = resize_event->window.data1;
                drawable_height = resize_event->window.data2;
            }
            if (drawable_width <= 0 || drawable_height <= 0) {
                auto window_size = get_window_pixel_size();
                drawable_width = window_size[0];
                drawable_height = window_size[1];
            }
            if (drawable_width <= 0 || drawable_height <= 0)
                return {};
            engine.resize(drawable_width, drawable_height);
            if (!engine.world_management_system().overlay_manager().is_initialized()) {
                TRY(engine.world_management_system().overlay_manager().initialize(drawable_width, drawable_height, engine.supports_external_image_import(), engine.vulkan_device(), engine, engine.world_management_system(), &virtual_file_system));
                engine.world_management_system().rebind_foreground_runtime_callbacks();
            } else {
                engine.world_management_system().overlay_manager().resize(drawable_width, drawable_height);
            }
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
                        engine.world_management_system().overlay_manager().toggle_visibility();
                        update_mouse_capture();
                        continue;
                    }
                    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F2) {
                        engine.world_management_system().overlay_manager().toggle_focus();
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

                    auto overlay_consumes_input = engine.world_management_system().overlay_manager().should_route_input(event);
                    auto overlay_has_exclusive_input = overlay_consumes_input && engine.world_management_system().overlay_manager().is_focused();

                    input_state.handle_sdl_event(event);

                    if (!overlay_has_exclusive_input)
                        engine.renderer().handle_web_view_event(event);
                    engine.world_management_system().overlay_manager().handle_sdl_event(event);
                }
            }

            if (Core::EventLoop::current().was_exit_requested())
                break;

            auto delta_time = static_cast<double>(frame_start_ticks - last_ticks) / 1'000'000'000.0;
            last_ticks = frame_start_ticks;

            engine.world_management_system().update(delta_time);

            auto fps = delta_time > 0.0 ? (1.0 / delta_time) : 0.0;
            if (auto* runtime = engine.world_management_system().foreground_runtime()) {
                runtime->with_world_lock([&](auto& world) {
                    engine.renderer().sync_web_views(runtime->id(), world);
                });
            }
            engine.world_management_system().overlay_manager().tick(fps, delta_time * 1000.0);

            auto& overlay = engine.world_management_system().overlay_manager();
            if (overlay.is_initialized() && overlay.is_visible() && overlay.has_overlay_vulkan_image()) {
                if (force_overlay_device_idle)
                    engine.renderer().wait_for_device_idle();
                engine.renderer().set_external_overlay_image(
                    overlay.overlay_vulkan_image(),
                    overlay.overlay_vulkan_width(),
                    overlay.overlay_vulkan_height());
            } else {
                engine.renderer().clear_external_overlay();
            }

            TRY(engine.render());
            engine.world_management_system().overlay_manager().post_render();

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
