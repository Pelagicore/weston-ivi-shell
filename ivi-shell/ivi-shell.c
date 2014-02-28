/*
 * Copyright (C) 2013 DENSO CORPORATION
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


/**
 * ivi-shell supports a type of shell for In-Vehicle Infotainment system.
 * In-Vehicle Infotainment system traditionally manages surfaces with global
 * identification. A protocol, ivi_application, supports such a feature
 * by implementing a request, ivi_application::surface_creation defined in
 * ivi_application.xml.
 *
 * Additionally, it initialize a library, weston-layout, to manage properties of
 * surfaces and group surfaces in layer. In detail, refer weston_layout.
 */

#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <linux/input.h>

#include "compositor.h"
#include "ivi-application-server-protocol.h"
#include "weston-layout.h"

struct ivi_shell;

struct ivi_shell_surface
{
    struct ivi_shell *shell;
    struct weston_layout_surface *layout_surface;

    struct weston_surface *surface;
    uint32_t id_surface;

    int32_t width;
    int32_t height;

    struct wl_list link;
};

struct ivi_shell
{
    struct wl_resource *resource;
    struct wl_listener destroy_listener;

    struct weston_compositor *compositor;
    struct weston_seat *seat;

    struct wl_list list_surface;
};

/* ------------------------------------------------------------------------- */
/* common functions                                                          */
/* ------------------------------------------------------------------------- */

static void
configure(struct weston_view *view, float x, float y)
{
    if (view != NULL) {
        weston_view_set_position(view, x, y);
        weston_view_update_transform(view);
    }
}

/**
 * Implementation of ivi_surface
 */

static void
ivi_shell_surface_configure(struct weston_surface *, int32_t, int32_t);

static struct ivi_shell_surface *
get_ivi_shell_surface(struct weston_surface *surface)
{
    if (surface->configure == ivi_shell_surface_configure) {
        return surface->configure_private;
    } else {
        return NULL;
    }
}

static void
ivi_shell_surface_configure(struct weston_surface *es,
                        int32_t sx, int32_t sy)
{
    struct ivi_shell_surface *ivisurf = get_ivi_shell_surface(es);
    struct weston_view *view = NULL;
    float from_x = 0.0f;
    float from_y = 0.0f;
    float to_x = 0.0f;
    float to_y = 0.0f;

    if ((es->width == 0) || (es->height == 0) || (ivisurf == NULL)) {
        return;
    }

    view = weston_layout_get_weston_view(ivisurf->layout_surface);
    if (view == NULL) {
        return;
    }

    if (ivisurf->width != es->width || ivisurf->height != es->height) {

        ivisurf->width  = es->width;
        ivisurf->height = es->height;

        weston_view_to_global_float(view, 0, 0, &from_x, &from_y);
        weston_view_to_global_float(view, sx, sy, &to_x, &to_y);
        configure(view,
                  view->geometry.x + to_x - from_x,
                  view->geometry.y + to_y - from_y);

        weston_layout_surfaceConfigure(ivisurf->layout_surface, es->width, es->height);
    }
}

static void
surface_destroy(struct wl_client   *client,
                struct wl_resource *resource)
{
    struct ivi_shell_surface *ivisurf = wl_resource_get_user_data(resource);
    struct ivi_shell *shell = ivisurf->shell;

    weston_layout_surfaceRemove(ivisurf->layout_surface);

    ivisurf->surface->configure = NULL;
    ivisurf->surface->configure_private = NULL;

    if (!wl_list_empty(&ivisurf->link)) {
        wl_list_remove(&ivisurf->link);
    }
    wl_list_init(&shell->list_surface);

    free(ivisurf);
    ivisurf = NULL;
}

static const struct ivi_surface_interface surface_implementation = {
    surface_destroy,
};

static struct ivi_shell_surface *
is_surf_in_surfaces(struct wl_list *list_surf, uint32_t id_surface)
{
    struct ivi_shell_surface *ivisurf;

    wl_list_for_each(ivisurf, list_surf, link) {
        if (ivisurf->id_surface == id_surface) {
            return ivisurf;
        }
    }

    return NULL;
}

/**
 * Implementation of ivi_application::surface_create.
 * Creating new ivi_shell_surface with identification to identify the surface
 * in the system.
 */
static void
application_surface_create(struct wl_client   *client,
                           struct wl_resource *resource,
                           uint32_t id_surface,
                           struct wl_resource *surface_resource,
                           uint32_t id)
{
    struct ivi_shell *shell = wl_resource_get_user_data(resource);
    struct weston_layout_surface *layout_surface = NULL;
    struct ivi_shell_surface  *ivisurf = NULL;
    struct weston_surface     *es = NULL;

    es = wl_resource_get_user_data(surface_resource);
    if (es == NULL) {
        weston_log("application_surface_create: invalid surface\n");
        return;
    }

    ivisurf = is_surf_in_surfaces(&shell->list_surface, id_surface);

    layout_surface = weston_layout_surfaceCreate(es, id_surface);
    if (layout_surface == NULL) {
//FIXME
        weston_log("id_surface is already created\n");

        ivisurf->surface = es;
        ivisurf->width  = 0;
        ivisurf->height = 0;

        es->configure = ivi_shell_surface_configure;
        es->configure_private = ivisurf;

        resource = wl_resource_create(client, &ivi_surface_interface, 1, id);
        if (resource == NULL) {
            weston_log("couldn't get surface object");
            return;
        }

        wl_resource_set_implementation(resource,
                                       &surface_implementation,
                                       ivisurf, NULL);
        return;
    }

    ivisurf = calloc(1, sizeof *ivisurf);
    if (ivisurf == NULL) {
        weston_log("fails to allocate memory\n");
        return;
    }

    wl_list_init(&ivisurf->link);
    ivisurf->shell = shell;
    ivisurf->layout_surface = layout_surface;
    ivisurf->surface = es;
    ivisurf->id_surface = id_surface;
    ivisurf->width  = 0;
    ivisurf->height = 0;

    es->configure = ivi_shell_surface_configure;
    es->configure_private = ivisurf;

    wl_list_insert(&shell->list_surface, &ivisurf->link);

    resource = wl_resource_create(client, &ivi_surface_interface, 1, id);
    if (resource == NULL) {
        weston_log("couldn't get surface object");
        return;
    }

    wl_resource_set_implementation(resource,
                                   &surface_implementation,
                                   ivisurf, NULL);
}

static const struct ivi_application_interface application_implementation = {
    application_surface_create
};

static void
bind_ivi_application(struct wl_client *client,
                void *data, uint32_t version, uint32_t id)
{
    struct ivi_shell *shell = data;
    struct wl_resource *resource = NULL;

    resource = wl_resource_create(client, &ivi_application_interface, 1, id);

    wl_resource_set_implementation(resource,
                                   &application_implementation,
                                   shell, NULL);
}

/**
 * Initialization/destruction method of ivi-shell
 */
static void
shell_destroy(struct wl_listener *listener, void *data)
{
    struct ivi_shell *shell = NULL;

    shell = container_of(listener, struct ivi_shell, destroy_listener);

    free(shell);
    shell = NULL;
}

static void
init_ivi_shell(struct weston_compositor *ec, struct ivi_shell *shell)
{
    shell->compositor = ec;

    wl_list_init(&ec->layer_list);
    wl_list_init(&shell->list_surface);
}

/**
 * Initialization of ivi-shell. A library, weston_layout, is also initialized
 * here by calling weston_layout_initWithCompositor.
 *
 */

WL_EXPORT int
module_init(struct weston_compositor *ec,
            int *argc, char *argv[])
{
    struct ivi_shell  *shell = NULL;
    struct weston_seat *seat = NULL;

    shell = calloc(1, sizeof *shell);
    if (shell == NULL) {
        return -1;
    }

    init_ivi_shell(ec, shell);
    weston_layout_initWithCompositor(ec);

    shell->destroy_listener.notify = shell_destroy;
    wl_signal_add(&ec->destroy_signal, &shell->destroy_listener);

    if (wl_global_create(ec->wl_display, &ivi_application_interface, 1,
                         shell, bind_ivi_application) == NULL) {
        return -1;
    }

    wl_list_for_each(seat, &ec->seat_list, link) {
        if (seat) {
            shell->seat = seat;
        }
    }

    return 0;
}
