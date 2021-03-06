/* gtktestatcontextprivate.h: Private test AT context
 *
 * Copyright 2020  GNOME Foundation
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "gtktestatcontext.h"
#include "gtkatcontextprivate.h"

G_BEGIN_DECLS

#define GTK_TYPE_TEST_AT_CONTEXT (gtk_test_at_context_get_type())

G_DECLARE_FINAL_TYPE (GtkTestATContext, gtk_test_at_context, GTK, TEST_AT_CONTEXT, GtkATContext)

GtkATContext *
gtk_test_at_context_new (GtkAccessibleRole  accessible_role,
                         GtkAccessible     *accessible);

G_END_DECLS
