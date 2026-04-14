/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include "WebContentView.h"

#include "../World/World.h"

#include <AK/Error.h>
#include <AK/HashMap.h>
#include <AK/OwnPtr.h>
#include <SDL3/SDL_events.h>

namespace MyceliumVR {

class WebViewManager {
public:
    WebViewManager();

    void sync_world(World const&);
    void resize(int width, int height);
    void handle_sdl_event(SDL_Event const&);

    bool has_active_panel() const;
    ErrorOr<Optional<WebContentBitmapView>> snapshot_active_panel_if_needed();

private:
    struct ManagedView {
        OwnPtr<WebContentView> view;
        String url;
        int pixel_width { 1280 };
        int pixel_height { 720 };
    };

    static void calculate_panel_pixel_size(Panel const&, int& width, int& height);

    ManagedView& ensure_panel_view(EntityId, Panel const&);
    void remove_stale_views(World const&);
    void choose_active_panel(World const&);

    HashMap<EntityId, ManagedView> m_views;
    EntityId m_active_panel { 0 };
};

}
