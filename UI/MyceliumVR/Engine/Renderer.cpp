/*
 * MyceliumVR - a social VR platform built on the Ladybird browser engine
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "Renderer.h"

#include <AK/Array.h>
#include <AK/Math.h>

namespace MyceliumVR {

struct Vec3 {
    float x { 0.0f };
    float y { 0.0f };
    float z { 0.0f };
};

struct ScreenPoint {
    float x { 0.0f };
    float y { 0.0f };
    bool visible { false };
};

static Vec3 rotate_by_quaternion(Vec3 point, float qx, float qy, float qz, float qw)
{
    auto tx = 2.0f * (qy * point.z - qz * point.y);
    auto ty = 2.0f * (qz * point.x - qx * point.z);
    auto tz = 2.0f * (qx * point.y - qy * point.x);

    return {
        point.x + qw * tx + (qy * tz - qz * ty),
        point.y + qw * ty + (qz * tx - qx * tz),
        point.z + qw * tz + (qx * ty - qy * tx),
    };
}

static ScreenPoint project_to_screen(Vec3 point, int width, int height)
{
    constexpr float camera_z = 6.0f;
    constexpr float near_plane = 0.1f;
    auto depth = camera_z - point.z;
    if (depth <= near_plane)
        return {};

    auto focal_length = static_cast<float>(height) * 0.85f;
    return {
        static_cast<float>(width) * 0.5f + point.x * focal_length / depth,
        static_cast<float>(height) * 0.5f - point.y * focal_length / depth,
        true,
    };
}

static void draw_line(SDL_Renderer& renderer, ScreenPoint const& a, ScreenPoint const& b)
{
    if (!a.visible || !b.visible)
        return;
    SDL_RenderLine(&renderer, a.x, a.y, b.x, b.y);
}

Renderer::Renderer(SDL_Renderer& renderer)
    : m_renderer(renderer)
{
}

void Renderer::resize(int width, int height)
{
    m_width = width;
    m_height = height;
}

void Renderer::draw_world(World const& world)
{
    static constexpr Array<Vec3, 8> cube_vertices {
        Vec3 { -0.5f, -0.5f, -0.5f },
        Vec3 { 0.5f, -0.5f, -0.5f },
        Vec3 { 0.5f, 0.5f, -0.5f },
        Vec3 { -0.5f, 0.5f, -0.5f },
        Vec3 { -0.5f, -0.5f, 0.5f },
        Vec3 { 0.5f, -0.5f, 0.5f },
        Vec3 { 0.5f, 0.5f, 0.5f },
        Vec3 { -0.5f, 0.5f, 0.5f },
    };

    static constexpr Array<Array<size_t, 2>, 12> cube_edges {
        Array<size_t, 2> { 0, 1 },
        Array<size_t, 2> { 1, 2 },
        Array<size_t, 2> { 2, 3 },
        Array<size_t, 2> { 3, 0 },
        Array<size_t, 2> { 4, 5 },
        Array<size_t, 2> { 5, 6 },
        Array<size_t, 2> { 6, 7 },
        Array<size_t, 2> { 7, 4 },
        Array<size_t, 2> { 0, 4 },
        Array<size_t, 2> { 1, 5 },
        Array<size_t, 2> { 2, 6 },
        Array<size_t, 2> { 3, 7 },
    };

    for (auto const& entity : world.entities()) {
        if (!entity.alive)
            continue;

        auto const& transform = entity.transform;
        Array<ScreenPoint, 8> projected_vertices;
        for (size_t i = 0; i < cube_vertices.size(); ++i) {
            auto vertex = cube_vertices[i];
            vertex.x *= transform.scale[0];
            vertex.y *= transform.scale[1];
            vertex.z *= transform.scale[2];

            vertex = rotate_by_quaternion(vertex, transform.rotation[0], transform.rotation[1], transform.rotation[2], transform.rotation[3]);
            vertex.x += transform.position[0];
            vertex.y += transform.position[1];
            vertex.z += transform.position[2];

            projected_vertices[i] = project_to_screen(vertex, m_width, m_height);
        }

        if (entity.mesh_renderer.material == "accent"_string)
            SDL_SetRenderDrawColor(&m_renderer, 255, 186, 73, 255);
        else if (entity.mesh_renderer.material == "example-accent"_string)
            SDL_SetRenderDrawColor(&m_renderer, 80, 220, 255, 255);
        else
            SDL_SetRenderDrawColor(&m_renderer, 32, 220, 160, 255);

        for (auto const& edge : cube_edges)
            draw_line(m_renderer, projected_vertices[edge[0]], projected_vertices[edge[1]]);
    }
}

}
