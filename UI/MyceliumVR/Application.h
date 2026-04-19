/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/ByteString.h>
#include <AK/StringView.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/System.h>
#include <LibWebView/ProcessManager.h>
#include <LibWebView/Application.h>
#include <LibWebView/Options.h>

namespace MyceliumVR {

class Application : public WebView::Application {
    WEB_VIEW_APPLICATION(Application)

public:
    bool vr_mode_requested() const { return m_vr_argument; }
    bool vulkan_probe_requested() const { return m_vulkan_probe_argument; }
    bool has_script_path_override() const { return !m_script_path_argument.is_empty(); }
    StringView script_path() const
    {
        if (m_script_path_argument.is_empty())
            return "UI/MyceliumVR/scripts/main.js"sv;
        return m_script_path_argument;
    }
    StringView world_path() const { return m_world_path_argument; }
    StringView shadow_quality() const
    {
        if (m_shadow_quality_argument.is_empty())
            return "9"sv;
        return m_shadow_quality_argument;
    }
    StringView sdk_generation_target() const { return m_generate_sdk_argument; }
    StringView sdk_output_directory() const
    {
        if (m_sdk_output_argument.is_empty())
            return "UI/MyceliumVR/sdk"sv;
        return m_sdk_output_argument;
    }

private:
    explicit Application() = default;

    virtual void create_platform_arguments(Core::ArgsParser& args_parser) override
    {
        args_parser.add_option(m_vr_argument, "Start in VR mode (requires OpenXR)", "vr", 0);
        args_parser.add_option(m_vulkan_probe_argument, "Probe SDL3 Vulkan instance and surface support, then exit", "vulkan-probe", 0);
        args_parser.add_option(m_script_path_argument, "JavaScript runtime script to load", "script", 0, "path");
        args_parser.add_option(m_world_path_argument, "Loose folder world to mount by package name, e.g. world://example/", "world", 0, "path");
        args_parser.add_option(m_shadow_quality_argument, "Shadow quality level from 0 to 9 (0 = off, 1 = 1x1, 2 = 3x3, 3 = 5x5, ...)", "shadow-quality", 0, "level");
        args_parser.add_option(m_generate_sdk_argument, "Generate SDK files and exit (all, typescript, javascript, wasm, json)", "generate-sdk", 0, "target");
        args_parser.add_option(m_sdk_output_argument, "Output directory for generated SDK files", "sdk-output", 0, "path");
    }

    virtual bool should_capture_web_content_output() const override
    {
        return true;
    }

    virtual void process_did_exit(WebView::Process&& process) override
    {
        auto process_name = WebView::process_name_from_type(process.type());
        dbgln("MyceliumVR: helper process exited: {} ({})", process_name, process.pid());

        auto dump_capture = [&](StringView stream_name, auto const& file) {
            if (!file)
                return;
            auto output = file->read_until_eof();
            if (output.is_error()) {
                warnln("MyceliumVR: failed to read {} for {} ({}): {}", stream_name, process_name, process.pid(), output.error());
                return;
            }
            if (output.value().is_empty())
                return;

            dbgln("MyceliumVR: {} {} ({}):", process_name, stream_name, process.pid());
            out("{}", StringView { output.value() });
        };

        dump_capture("stdout"sv, process.output_capture().stdout_file);
        dump_capture("stderr"sv, process.output_capture().stderr_file);

        WebView::Application::process_did_exit(move(process));
    }

    virtual void create_platform_options(WebView::BrowserOptions&, WebView::RequestServerOptions& request_server_options, WebView::WebContentOptions&) override
    {
        // Provide the system CA bundle so SSL verification works
        static constexpr StringView system_ca_bundle = "/etc/ssl/certs/ca-certificates.crt"sv;
        if (!Core::System::stat(system_ca_bundle).is_error()) {
            outln("MyceliumVR: injecting CA bundle: {}", system_ca_bundle);
            request_server_options.certificates.append(ByteString(system_ca_bundle));
        } else {
            warnln("MyceliumVR: CA bundle not found at {}", system_ca_bundle);
        }
    }

    bool m_vr_argument { false };
    bool m_vulkan_probe_argument { false };
    StringView m_script_path_argument;
    StringView m_world_path_argument;
    StringView m_shadow_quality_argument;
    StringView m_generate_sdk_argument;
    StringView m_sdk_output_argument;
};

}
