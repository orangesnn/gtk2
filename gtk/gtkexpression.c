/*
 * Copyright © 2019 Benjamin Otte
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
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Benjamin Otte <otte@gnome.org>
 */

#include "config.h"

#include "gtkexpression.h"

#include <gobject/gvaluecollector.h>

/**
 * SECTION:gtkexpression
 * @Short_description: Expressions to values
 * @Title: GtkExpression
 *
 * GtkExpression provides a way to describe references to #GValues.
 *
 * An expression needs to be `evaluated` to obtain the value that it currently refers
 * to. An evaluation always happens in the context of a current object called `this`
 * (it mirrors the behavior of object-oriented languages), which may or may not
 * influence the result of the evaluation. Use gtk_expression_evaluate() for
 * evaluating an expression.
 *
 * Various methods for defining expressions exist, from simple constants via
 * gtk_constant_expression_new() to looking up properties in a #GObject (even
 * recursively) via gtk_property_expression_new() or providing custom functions to
 * transform and combine expressions via gtk_closure_expression_new().
 *
 * By default, expressions are not paying attention to changes and evaluation is
 * just a snapshot of the current state at a given time. To get informed about
 * changes, an expression needs to be `watched` via a #GtkExpressionWatch, which
 * will cause a callback to be called whenever the value of the expression may
 * have changed. gtk_expression_watch() starts watching an expression, and
 * gtk_expression_watch_unwatch() stops.
 *
 * Watches can be created for automatically updating the propery of an object,
 * similar to GObject's #GBinding mechanism, by using gtk_expression_bind().
 *
 * #GtkExpression in ui files
 *
 * GtkBuilder has support for creating expressions. The syntax here can be used where
 * a #GtkExpression object is needed like in a <property> tag for an expression
 * property, or in a <binding> tag to bind a property to an expression.
 *
 * To create an property expression, use the <lookup> element. It can have a `type`
 * attribute to specify the object type, and a `name` attribute to specify the property
 * to look up. The content of <lookup> can either be an element specfiying the expression
 * to use the object, or a string that specifies the name of the object to use.
 * 
 * Example:
 * |[
 *   <lookup name='search'>string_filter</lookup>
 * |]
 *
 * To create a constant expression, use the <constant> element. If the type attribute
 * is specified, the element content is interpreted as a value of that type. Otherwise,
 * it is assumed to be an object.
 *
 * Example:
 * |[
 *   <constant>string_filter</constant>
 *   <constant type='gchararray'>Hello, world</constant>
 * ]|
 *
 * To create a closure expression, use the <closure> element. The `type` and `function`
 * attributes specify what function to use for the closure, the content of the element
 * contains the expressions for the parameters.
 * 
 * Example:
 * |[
 *   <closure type='gchararray' function='combine_args_somehow'>
 *     <constant type='gchararray'>File size:</constant>
 *     <lookup type='GFile' name='size'>myfile</lookup>
 *   </closure>
 * ]|
 */
typedef struct _GtkExpressionClass GtkExpressionClass;

struct _GtkExpression
{
  const GtkExpressionClass *expression_class;
  GType value_type;

  GtkExpression *owner;
};

typedef struct _GtkExpressionSubWatch GtkExpressionSubWatch;

struct _GtkExpressionWatch
{
  GtkExpression         *expression;
  GObject               *this;
  GDestroyNotify         user_destroy;
  GtkExpressionNotify    notify;
  gpointer               user_data;
  guchar                 sub[0];
};

struct _GtkExpressionClass
{
  gsize struct_size;
  const char *type_name;

  void                  (* finalize)            (GtkExpression          *expr);
  gboolean              (* is_static)           (GtkExpression          *expr);
  gboolean              (* evaluate)            (GtkExpression          *expr,
                                                 gpointer                this,
                                                 GValue                 *value);

  gsize                 (* watch_size)          (GtkExpression          *expr);
  void                  (* watch)               (GtkExpression          *self,
                                                 GtkExpressionSubWatch  *watch,
                                                 gpointer                this_,
                                                 GtkExpressionNotify     notify,
                                                 gpointer                user_data);
  void                  (* unwatch)             (GtkExpression          *self,
                                                 GtkExpressionSubWatch  *watch);
};

/**
 * GtkExpression: (ref-func gtk_expression_ref) (unref-func gtk_expression_unref)
 *
 * The `GtkExpression` structure contains only private data.
 */

G_DEFINE_BOXED_TYPE (GtkExpression, gtk_expression,
                     gtk_expression_ref,
                     gtk_expression_unref)

/*< private >
 * gtk_expression_alloc:
 * @expression_class: class structure for this expression
 * @value_type: the type of the value returned by this expression
 *
 * Returns: (transfer full): the newly created #GtkExpression
 */
static gpointer
gtk_expression_alloc (const GtkExpressionClass *expression_class,
                      GType                     value_type)
{
  GtkExpression *self;

  g_return_val_if_fail (expression_class != NULL, NULL);

  self = g_atomic_rc_box_alloc0 (expression_class->struct_size);

  self->expression_class = expression_class;
  self->value_type = value_type;

  return self;
}

static gsize
gtk_expression_watch_size_static (GtkExpression *self)
{
  return 0;
}

static void
gtk_expression_watch_static (GtkExpression         *self,
                             GtkExpressionSubWatch *watch,
                             gpointer               this_,
                             GtkExpressionNotify    notify,
                             gpointer               user_data)
{
}

static void
gtk_expression_unwatch_static (GtkExpression         *self,
                               GtkExpressionSubWatch *watch)
{
}

static gsize
gtk_expression_watch_size (GtkExpression *self)
{
  return self->expression_class->watch_size (self);
}

static void
gtk_expression_subwatch_init (GtkExpression         *self,
                              GtkExpressionSubWatch *watch,
                              gpointer               this,
                              GtkExpressionNotify    notify,
                              gpointer               user_data)
{
  self->expression_class->watch (self, watch, this, notify, user_data);
}

static void
gtk_expression_subwatch_finish (GtkExpression         *self,
                                GtkExpressionSubWatch *watch)
{
  self->expression_class->unwatch (self, watch);
}

/*** CONSTANT ***/

typedef struct _GtkConstantExpression GtkConstantExpression;

struct _GtkConstantExpression
{
  GtkExpression parent;

  GValue value;
};

static void
gtk_constant_expression_finalize (GtkExpression *expr)
{
  GtkConstantExpression *self = (GtkConstantExpression *) expr;

  g_value_unset (&self->value);
}

static gboolean
gtk_constant_expression_is_static (GtkExpression *expr)
{
  return TRUE;
}

static gboolean
gtk_constant_expression_evaluate (GtkExpression *expr,
                                  gpointer       this,
                                  GValue        *value)
{
  GtkConstantExpression *self = (GtkConstantExpression *) expr;

  g_value_init (value, G_VALUE_TYPE (&self->value));
  g_value_copy (&self->value, value);
  return TRUE;
}

static const GtkExpressionClass GTK_CONSTANT_EXPRESSION_CLASS =
{
  sizeof (GtkConstantExpression),
  "GtkConstantExpression",
  gtk_constant_expression_finalize,
  gtk_constant_expression_is_static,
  gtk_constant_expression_evaluate,
  gtk_expression_watch_size_static,
  gtk_expression_watch_static,
  gtk_expression_unwatch_static
};

/**
 * gtk_constant_expression_new:
 * @value_type: The type of the object
 * @...: arguments to create the object from
 *
 * Creates a GtkExpression that evaluates to the
 * object given by the arguments.
 *
 * Returns: a new #GtkExpression
 */
GtkExpression *
gtk_constant_expression_new (GType value_type,
                             ...)
{
  GValue value = G_VALUE_INIT;
  GtkExpression *result;
  va_list args;
  char *error;

  va_start (args, value_type);
  G_VALUE_COLLECT_INIT (&value, value_type,
                        args, G_VALUE_NOCOPY_CONTENTS,
                        &error);
  if (error)
    {
      g_critical ("%s: %s", G_STRLOC, error);
      g_free (error);
      /* we purposely leak the value here, it might not be
       * in a sane state if an error condition occurred
       */
      return NULL;
    }

  result = gtk_constant_expression_new_for_value (&value);

  g_value_unset (&value);
  va_end (args);

  return result;
}

/**
 * gtk_constant_expression_new_for_value:
 * @value: a #GValue
 *
 * Creates an expression that always evaluates to the given @value.
 *
 * Returns: a new #GtkExpression
 **/
GtkExpression *
gtk_constant_expression_new_for_value (const GValue *value)
{
  GtkConstantExpression *result;

  g_return_val_if_fail (G_IS_VALUE (value), NULL);

  result = gtk_expression_alloc (&GTK_CONSTANT_EXPRESSION_CLASS, G_VALUE_TYPE (value));

  g_value_init (&result->value, G_VALUE_TYPE (value));
  g_value_copy (value, &result->value);

  return (GtkExpression *) result;
}

/*** OBJECT ***/

typedef struct _GtkObjectExpression GtkObjectExpression;
typedef struct _GtkObjectExpressionWatch GtkObjectExpressionWatch;

struct _GtkObjectExpression
{
  GtkExpression parent;

  GObject *object;
  GSList *watches;
};

struct _GtkObjectExpressionWatch
{
  GtkExpressionNotify    notify;
  gpointer               user_data;
};

static void
gtk_object_expression_weak_ref_cb (gpointer  data,
                                   GObject  *object)
{
  GtkObjectExpression *self = (GtkObjectExpression *) data;
  GSList *l;

  self->object = NULL;

  for (l = self->watches; l; l = l->next)
    {
      GtkObjectExpressionWatch *owatch = l->data;

      owatch->notify (owatch->user_data);
    }
}

static void
gtk_object_expression_finalize (GtkExpression *expr)
{
  GtkObjectExpression *self = (GtkObjectExpression *) expr;

  if (self->object)
    g_object_weak_unref (self->object, gtk_object_expression_weak_ref_cb, self);

  g_assert (self->watches == NULL);
}

static gboolean
gtk_object_expression_is_static (GtkExpression *expr)
{
  return FALSE;
}

static gboolean
gtk_object_expression_evaluate (GtkExpression *expr,
                                gpointer       this,
                                GValue        *value)
{
  GtkObjectExpression *self = (GtkObjectExpression *) expr;

  if (self->object == NULL)
    return FALSE;

  g_value_init (value, gtk_expression_get_value_type (expr));
  g_value_set_object (value, self->object);
  return TRUE;
}

static gsize
gtk_object_expression_watch_size (GtkExpression *expr)
{
  return sizeof (GtkObjectExpressionWatch);
}

static void
gtk_object_expression_watch (GtkExpression         *expr,
                             GtkExpressionSubWatch *watch,
                             gpointer               this_,
                             GtkExpressionNotify    notify,
                             gpointer               user_data)
{
  GtkObjectExpression *self = (GtkObjectExpression *) expr;
  GtkObjectExpressionWatch *owatch = (GtkObjectExpressionWatch *) watch;

  owatch->notify = notify;
  owatch->user_data = user_data;
  self->watches = g_slist_prepend (self->watches, owatch);
}

static void
gtk_object_expression_unwatch (GtkExpression         *expr,
                               GtkExpressionSubWatch *watch)
{
  GtkObjectExpression *self = (GtkObjectExpression *) expr;

  self->watches = g_slist_remove (self->watches, watch);
}

static const GtkExpressionClass GTK_OBJECT_EXPRESSION_CLASS =
{
  sizeof (GtkObjectExpression),
  "GtkObjectExpression",
  gtk_object_expression_finalize,
  gtk_object_expression_is_static,
  gtk_object_expression_evaluate,
  gtk_object_expression_watch_size,
  gtk_object_expression_watch,
  gtk_object_expression_unwatch
};

/**
 * gtk_object_expression_new:
 * @object: (transfer none): object to watch
 *
 * Creates an expression evaluating to the given @object with a weak reference.
 * Once the @object is disposed, it will fail to evaluate.
 * This expression is meant to break reference cycles.
 *
 * If you want to keep a reference to @object, use gtk_constant_expression_new().
 *
 * Returns: a new #GtkExpression
 **/
GtkExpression *
gtk_object_expression_new (GObject *object)
{
  GtkObjectExpression *result;

  g_return_val_if_fail (G_IS_OBJECT (object), NULL);

  result = gtk_expression_alloc (&GTK_OBJECT_EXPRESSION_CLASS, G_OBJECT_TYPE (object));

  result->object = object;
  g_object_weak_ref (object, gtk_object_expression_weak_ref_cb, result);

  return (GtkExpression *) result;
}

/*** PROPERTY ***/

typedef struct _GtkPropertyExpression GtkPropertyExpression;

struct _GtkPropertyExpression
{
  GtkExpression parent;

  GtkExpression *expr;

  GParamSpec *pspec;
};

static void
gtk_property_expression_finalize (GtkExpression *expr)
{
  GtkPropertyExpression *self = (GtkPropertyExpression *) expr;

  g_clear_pointer (&self->expr, gtk_expression_unref);
}

static gboolean
gtk_property_expression_is_static (GtkExpression *expr)
{
  return FALSE;
}

static GObject *
gtk_property_expression_get_object (GtkPropertyExpression *self,
                                    gpointer               this)
{
  GValue expr_value = G_VALUE_INIT;
  GObject *object;

  if (self->expr == NULL)
    {
      if (this)
        return g_object_ref (this);
      else
        return NULL;
    }

  if (!gtk_expression_evaluate (self->expr, this, &expr_value))
    return NULL;

  if (!G_VALUE_HOLDS_OBJECT (&expr_value))
    {
      g_value_unset (&expr_value);
      return NULL;
    }

  object = g_value_dup_object (&expr_value);
  g_value_unset (&expr_value);
  if (object == NULL)
    return NULL;

  if (!G_TYPE_CHECK_INSTANCE_TYPE (object, self->pspec->owner_type))
    {
      g_object_unref (object);
      return NULL;
    }

  return object;
}

static gboolean
gtk_property_expression_evaluate (GtkExpression *expr,
                                  gpointer       this,
                                  GValue        *value)
{
  GtkPropertyExpression *self = (GtkPropertyExpression *) expr;
  GObject *object;

  object = gtk_property_expression_get_object (self, this);
  if (object == NULL)
    return FALSE;

  g_object_get_property (object, self->pspec->name, value);
  g_object_unref (object);
  return TRUE;
}

typedef struct _GtkPropertyExpressionWatch GtkPropertyExpressionWatch;

struct _GtkPropertyExpressionWatch
{
  GtkExpressionNotify    notify;
  gpointer               user_data;

  GtkPropertyExpression *expr;
  gpointer               this;
  GClosure              *closure;
  guchar                 sub[0];
};

static void
gtk_property_expression_watch_destroy_closure (GtkPropertyExpressionWatch *pwatch)
{
  if (pwatch->closure == NULL)
    return;

  g_closure_invalidate (pwatch->closure);
  g_closure_unref (pwatch->closure);
  pwatch->closure = NULL;
}

static void
gtk_property_expression_watch_notify_cb (GObject                    *object,
                                         GParamSpec                 *pspec,
                                         GtkPropertyExpressionWatch *pwatch)
{
  pwatch->notify (pwatch->user_data);
}

static void
gtk_property_expression_watch_create_closure (GtkPropertyExpressionWatch *pwatch)
{
  GObject *object;

  object = gtk_property_expression_get_object (pwatch->expr, pwatch->this);
  if (object == NULL)
    return;

  pwatch->closure = g_cclosure_new (G_CALLBACK (gtk_property_expression_watch_notify_cb), pwatch, NULL);
  if (!g_signal_connect_closure_by_id (object,
                                       g_signal_lookup ("notify", G_OBJECT_TYPE (object)),
                                       g_quark_from_string (pwatch->expr->pspec->name),
                                       g_closure_ref (pwatch->closure),
                                       FALSE))
    {
      g_assert_not_reached ();
    }

  g_object_unref (object);
}

static void
gtk_property_expression_watch_expr_notify_cb (gpointer data)
{
  GtkPropertyExpressionWatch *pwatch = data;

  gtk_property_expression_watch_destroy_closure (pwatch);
  gtk_property_expression_watch_create_closure (pwatch);
  pwatch->notify (pwatch->user_data);
}

static gsize
gtk_property_expression_watch_size (GtkExpression *expr)
{
  GtkPropertyExpression *self = (GtkPropertyExpression *) expr;
  gsize result;

  result = sizeof (GtkPropertyExpressionWatch);
  if (self->expr)
    result += gtk_expression_watch_size (self->expr);

  return result;
}

static void
gtk_property_expression_watch (GtkExpression         *expr,
                               GtkExpressionSubWatch *watch,
                               gpointer               this_,
                               GtkExpressionNotify    notify,
                               gpointer               user_data)
{
  GtkPropertyExpressionWatch *pwatch = (GtkPropertyExpressionWatch *) watch;
  GtkPropertyExpression *self = (GtkPropertyExpression *) expr;

  pwatch->notify = notify;
  pwatch->user_data = user_data;
  pwatch->expr = self;
  pwatch->this = this_;
  if (self->expr && !gtk_expression_is_static (self->expr))
    {
      gtk_expression_subwatch_init (self->expr,
                                    (GtkExpressionSubWatch *) pwatch->sub,
                                    this_,
                                    gtk_property_expression_watch_expr_notify_cb,
                                    pwatch);
    }

  gtk_property_expression_watch_create_closure (pwatch);
}

static void
gtk_property_expression_unwatch (GtkExpression         *expr,
                                 GtkExpressionSubWatch *watch)
{
  GtkPropertyExpressionWatch *pwatch = (GtkPropertyExpressionWatch *) watch;
  GtkPropertyExpression *self = (GtkPropertyExpression *) expr;

  gtk_property_expression_watch_destroy_closure (pwatch);

  if (self->expr && !gtk_expression_is_static (self->expr))
    gtk_expression_subwatch_finish (self->expr, (GtkExpressionSubWatch *) pwatch->sub);
}

static const GtkExpressionClass GTK_PROPERTY_EXPRESSION_CLASS =
{
  sizeof (GtkPropertyExpression),
  "GtkPropertyExpression",
  gtk_property_expression_finalize,
  gtk_property_expression_is_static,
  gtk_property_expression_evaluate,
  gtk_property_expression_watch_size,
  gtk_property_expression_watch,
  gtk_property_expression_unwatch
};

/**
 * gtk_property_expression_new:
 * @this_type: The type to expect for the this type
 * @expression: (nullable) (transfer full): Expression to
 *     evaluate to get the object to query or %NULL to
 *     query the `this` object
 * @property_name: name of the property
 *
 * Creates an expression that looks up a property via the
 * given @expression or the `this` argument when @expression
 * is %NULL.
 *
 * If the resulting object conforms to @this_type, its property
 * named @property_name will be queried.
 * Otherwise, this expression's evaluation will fail.
 *
 * The given @this_type must have a property with @property_name.  
 *
 * Returns: a new #GtkExpression
 **/
GtkExpression *
gtk_property_expression_new (GType          this_type,
                             GtkExpression *expression,
                             const char    *property_name)
{
  GtkPropertyExpression *result;
  GParamSpec *pspec;

  if (g_type_is_a (this_type, G_TYPE_OBJECT))
    {
      GObjectClass *class = g_type_class_ref (this_type);
      pspec = g_object_class_find_property (class, property_name);
      g_type_class_unref (class);
    }
  else if (g_type_is_a (this_type, G_TYPE_INTERFACE))
    {
      GTypeInterface *iface = g_type_default_interface_ref (this_type);
      pspec = g_object_interface_find_property (iface, property_name);
      g_type_default_interface_unref (iface);
    }
  else
    {
      g_critical ("Type `%s` does not support properties", g_type_name (this_type));
      return NULL;
    }

  if (pspec == NULL)
    {
      g_critical ("Type `%s` does not have a property named `%s`", g_type_name (this_type), property_name);
      return NULL;
    }

  result = gtk_expression_alloc (&GTK_PROPERTY_EXPRESSION_CLASS, pspec->value_type);

  result->pspec = pspec;
  result->expr = expression;

  return (GtkExpression *) result;
}

/*** CLOSURE ***/

typedef struct _GtkClosureExpression GtkClosureExpression;

struct _GtkClosureExpression
{
  GtkExpression parent;

  GClosure *closure;
  guint n_params;
  GtkExpression **params;
};

static void
gtk_closure_expression_finalize (GtkExpression *expr)
{
  GtkClosureExpression *self = (GtkClosureExpression *) expr;
  guint i;

  for (i = 0; i < self->n_params; i++)
    {
      gtk_expression_unref (self->params[i]);
    }
  g_free (self->params);

  g_closure_unref (self->closure);
}

static gboolean
gtk_closure_expression_is_static (GtkExpression *expr)
{
  GtkClosureExpression *self = (GtkClosureExpression *) expr;
  guint i;

  for (i = 0; i < self->n_params; i++)
    {
      if (!gtk_expression_is_static (self->params[i]))
        return FALSE;
    }

  return TRUE;
}

static gboolean
gtk_closure_expression_evaluate (GtkExpression *expr,
                                 gpointer       this,
                                 GValue        *value)
{
  GtkClosureExpression *self = (GtkClosureExpression *) expr;
  GValue *instance_and_params;
  gboolean result = TRUE;
  guint i;

  instance_and_params = g_alloca (sizeof (GValue) * (self->n_params + 1));
  memset (instance_and_params, 0, sizeof (GValue) * (self->n_params + 1));

  for (i = 0; i < self->n_params; i++)
    {
      if (!gtk_expression_evaluate (self->params[i], this, &instance_and_params[i + 1]))
        {
          result = FALSE;
          goto out;
        }
    }
  if (this)
    g_value_init_from_instance (instance_and_params, this);
  else
    g_value_init (instance_and_params, G_TYPE_OBJECT);

  g_value_init (value, expr->value_type);
  g_closure_invoke (self->closure,
                    value,
                    self->n_params + 1,
                    instance_and_params,
                    NULL);

out:
  for (i = 0; i < self->n_params + 1; i++)
    g_value_unset (&instance_and_params[i]);

  return result;
}

typedef struct _GtkClosureExpressionWatch GtkClosureExpressionWatch;
struct _GtkClosureExpressionWatch
{
  GtkExpressionNotify    notify;
  gpointer               user_data;

  guchar                 sub[0];
};

static void
gtk_closure_expression_watch_notify_cb (gpointer data)
{
  GtkClosureExpressionWatch *cwatch = data;

  cwatch->notify (cwatch->user_data);
}

static gsize
gtk_closure_expression_watch_size (GtkExpression *expr)
{
  GtkClosureExpression *self = (GtkClosureExpression *) expr;
  gsize size;
  guint i;

  size = sizeof (GtkClosureExpressionWatch);

  for (i = 0; i < self->n_params; i++)
    {
      if (gtk_expression_is_static (self->params[i]))
        continue;

      size += gtk_expression_watch_size (self->params[i]);
    }

  return size;
}

static void
gtk_closure_expression_watch (GtkExpression         *expr,
                              GtkExpressionSubWatch *watch,
                              gpointer               this_,
                              GtkExpressionNotify    notify,
                              gpointer               user_data)
{
  GtkClosureExpressionWatch *cwatch = (GtkClosureExpressionWatch *) watch;
  GtkClosureExpression *self = (GtkClosureExpression *) expr;
  guchar *sub;
  guint i;

  cwatch->notify = notify;
  cwatch->user_data = user_data;

  sub = cwatch->sub;
  for (i = 0; i < self->n_params; i++)
    {
      if (gtk_expression_is_static (self->params[i]))
        continue;

      gtk_expression_subwatch_init (self->params[i],
                                    (GtkExpressionSubWatch *) sub,
                                    this_,
                                    gtk_closure_expression_watch_notify_cb,
                                    watch);
      sub += gtk_expression_watch_size (self->params[i]);
    }
}

static void
gtk_closure_expression_unwatch (GtkExpression         *expr,
                                GtkExpressionSubWatch *watch)
{
  GtkClosureExpressionWatch *cwatch = (GtkClosureExpressionWatch *) watch;
  GtkClosureExpression *self = (GtkClosureExpression *) expr;
  guchar *sub;
  guint i;

  sub = cwatch->sub;
  for (i = 0; i < self->n_params; i++)
    {
      if (gtk_expression_is_static (self->params[i]))
        continue;

      gtk_expression_subwatch_finish (self->params[i],
                                      (GtkExpressionSubWatch *) sub);
      sub += gtk_expression_watch_size (self->params[i]);
    }
}

static const GtkExpressionClass GTK_CLOSURE_EXPRESSION_CLASS =
{
  sizeof (GtkClosureExpression),
  "GtkClosureExpression",
  gtk_closure_expression_finalize,
  gtk_closure_expression_is_static,
  gtk_closure_expression_evaluate,
  gtk_closure_expression_watch_size,
  gtk_closure_expression_watch,
  gtk_closure_expression_unwatch
};

/**
 * gtk_closure_expression_new:
 * @type: the type of the value that this expression evaluates to
 * @closure: closure to call when evaluating this expression. If closure is floating, it is adopted
 * @n_params: the number of params needed for evaluating @closure
 * @params: (array length=n_params) (transfer full): expressions for each parameter
 *
 * Creates a GtkExpression that calls @closure when it is evaluated.
 * @closure is called with the @this object and the results of evaluating
 * the @params expressions.
 *
 * Returns: a new #GtkExpression
 */
GtkExpression *
gtk_closure_expression_new (GType                value_type,
                            GClosure            *closure,
                            guint                n_params,
                            GtkExpression      **params)
{
  GtkClosureExpression *result;
  guint i;

  g_return_val_if_fail (closure != NULL, NULL);
  g_return_val_if_fail (n_params == 0 || params != NULL, NULL);

  result = gtk_expression_alloc (&GTK_CLOSURE_EXPRESSION_CLASS, value_type);

  result->closure = g_closure_ref (closure);
  g_closure_sink (closure);
  if (G_CLOSURE_NEEDS_MARSHAL (closure))
    g_closure_set_marshal (closure, g_cclosure_marshal_generic);

  result->n_params = n_params;
  result->params = g_new (GtkExpression *, n_params);
  for (i = 0; i < n_params; i++)
    result->params[i] = params[i];

  return (GtkExpression *) result;
}

/**
 * gtk_cclosure_expression_new:
 * @type: the type of the value that this expression evaluates to
 * @marshal: marshaller used for creating a closure
 * @n_params: the number of params needed for evaluating @closure
 * @params: (array length=n_params) (transfer full): expressions for each parameter
 * @callback_func: callback used for creating a closure
 * @user_data: user data used for creating a closure
 * @user_destroy: destroy notify for @user_data
 *
 * This function is a variant of gtk_closure_expression_new() that
 * creates a #GClosure by calling gtk_cclosure_new() with the given
 * @callback_func, @user_data and @user_destroy.
 *
 * Returns: a new #GtkExpression
 */
GtkExpression *
gtk_cclosure_expression_new (GType                value_type,
                             GClosureMarshal      marshal,
                             guint                n_params,
                             GtkExpression      **params,
                             GCallback            callback_func,
                             gpointer             user_data,
                             GClosureNotify       user_destroy)
{
  GClosure *closure;

  closure = g_cclosure_new (callback_func, user_data, user_destroy);
  if (marshal)
    g_closure_set_marshal (closure, marshal);

  return gtk_closure_expression_new (value_type, closure, n_params, params);
}

/*** PUBLIC API ***/

static void
gtk_expression_finalize (GtkExpression *self)
{
  self->expression_class->finalize (self);
}

/**
 * gtk_expression_ref:
 * @self: (allow-none): a #GtkExpression
 *
 * Acquires a reference on the given #GtkExpression.
 *
 * Returns: (transfer none): the #GtkExpression with an additional reference
 */
GtkExpression *
gtk_expression_ref (GtkExpression *self)
{
  return g_atomic_rc_box_acquire (self);
}

/**
 * gtk_expression_unref:
 * @self: (allow-none): a #GtkExpression
 *
 * Releases a reference on the given #GtkExpression.
 *
 * If the reference was the last, the resources associated to the @self are
 * freed.
 */
void
gtk_expression_unref (GtkExpression *self)
{
  g_atomic_rc_box_release_full (self, (GDestroyNotify) gtk_expression_finalize);
}

/**
 * gtk_expression_get_value_type:
 * @self: a #GtkExpression
 *
 * Gets the #GType that this expression evaluates to. This type
 * is constant and will not change over the lifetime of this expression.
 *
 * Returns: The type returned from gtk_expression_evaluate()
 **/
GType
gtk_expression_get_value_type (GtkExpression *self)
{
  g_return_val_if_fail (GTK_IS_EXPRESSION (self), G_TYPE_INVALID);

  return self->value_type;
}

/**
 * gtk_expression_evaluate:
 * @self: a #GtkExpression
 * @this_: (transfer none) (type GObject) (nullable): the this argument for the evaluation
 * @value: an empty #GValue
 *
 * Evaluates the given expression and on success stores the result
 * in @value. The #GType of @value will be the type given by
 * gtk_expression_get_value_type().
 *
 * It is possible that expressions cannot be evaluated - for example
 * when the expression references objects that have been destroyed or
 * set to %NULL. In that case @value will remain empty and %FALSE
 * will be returned.
 *
 * Returns: %TRUE if the expression could be evaluated
 **/
gboolean
gtk_expression_evaluate (GtkExpression *self,
                         gpointer       this_,
                         GValue        *value)
{
  g_return_val_if_fail (GTK_IS_EXPRESSION (self), FALSE);
  g_return_val_if_fail (this_ == NULL || G_IS_OBJECT (this_), FALSE);
  g_return_val_if_fail (value != NULL, FALSE);

  return self->expression_class->evaluate (self, this_, value);
}

/**
 * gtk_expression_is_static:
 * @self: a #GtkExpression
 *
 * Checks if the expression is static.
 *
 * A static expression will never change its result when 
 * gtk_expression_evaluate() is called on it with the same arguments.
 *
 * That means a call to gtk_expression_watch() is not necessary because
 * it will never trigger a notify.
 *
 * Returns: %TRUE if the expression is static
 **/
gboolean
gtk_expression_is_static (GtkExpression *self)
{
  return self->expression_class->is_static (self);
}

static gboolean
gtk_expression_watch_is_watching (GtkExpressionWatch *watch)
{
  return watch->expression != NULL;
}

static void
gtk_expression_watch_this_cb (gpointer data,
                              GObject *this)
{
  GtkExpressionWatch *watch = data;

  watch->this = NULL;

  watch->notify (watch->user_data);
  gtk_expression_watch_unwatch (watch);
}

static void
gtk_expression_watch_cb (gpointer data)
{
  GtkExpressionWatch *watch = data;

  if (!gtk_expression_watch_is_watching (watch))
    return;

  watch->notify (watch->user_data);
}

/**
 * gtk_expression_watch:
 * @self: a #GtkExpression
 * @this_: (transfer none) (type GObject) (nullable): the this argument to
 *     watch
 * @notify: (closure user_data): callback to invoke when the
 *     expression changes
 * @user_data: user data to pass to @notify callback
 * @user_destroy: destroy notify for @user_data
 *
 * Installs a watch for the given @expression that calls the @notify function
 * whenever the evaluation of @self may have changed.
 *
 * GTK cannot guarantee that the evaluation did indeed change when the @notify
 * gets invoked, but it guarantees the opposite: When it did in fact change,
 * the @notify will be invoked.
 *
 * Returns: (transfer none): The newly installed watch. Note that the only
 *     reference held to the watch will be released when the watch is unwatched
 *     which can happen automatically, and not just via
 *     gtk_expression_watch_unwatch(). You should call gtk_expression_watch_ref()
 *     if you want to keep the watch around.
 **/
GtkExpressionWatch *
gtk_expression_watch (GtkExpression       *self,
                      gpointer             this_,
                      GtkExpressionNotify  notify,
                      gpointer             user_data,
                      GDestroyNotify       user_destroy)
{
  GtkExpressionWatch *watch;

  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (this_ == NULL || G_IS_OBJECT (this_), NULL);
  g_return_val_if_fail (notify != NULL, NULL);

  watch = g_atomic_rc_box_alloc0 (sizeof (GtkExpressionWatch) + gtk_expression_watch_size (self));

  watch->expression = gtk_expression_ref (self);
  watch->this = this_;
  if (this_)
    g_object_weak_ref (this_, gtk_expression_watch_this_cb, watch);
  watch->notify = notify;
  watch->user_data = user_data;
  watch->user_destroy = user_destroy;

  gtk_expression_subwatch_init (self,
                                (GtkExpressionSubWatch *) watch->sub,
                                this_,
                                gtk_expression_watch_cb,
                                watch);

  return watch;
}

/**
 * gtk_expression_watch_ref:
 * @self: (allow-none): a #GtkExpressionWatch
 *
 * Acquires a reference on the given #GtkExpressionWatch.
 *
 * Returns: (transfer none): the #GtkExpression with an additional reference
 */
GtkExpressionWatch *
gtk_expression_watch_ref (GtkExpressionWatch *self)
{
  return g_atomic_rc_box_acquire (self);
}

static void
gtk_expression_watch_finalize (gpointer data)
{
  GtkExpressionWatch *watch = data;

  g_assert (!gtk_expression_watch_is_watching (watch));
}

/**
 * gtk_expression_watch_unref:
 * @self: (allow-none): a #GtkExpressionWatch
 *
 * Releases a reference on the given #GtkExpressionWatch.
 *
 * If the reference was the last, the resources associated to @self are
 * freed.
 */
void
gtk_expression_watch_unref (GtkExpressionWatch *self)
{
  g_atomic_rc_box_release_full (self, gtk_expression_watch_finalize);
}

/**
 * gtk_expression_watch_unwatch:
 * @watch: (transfer none): watch to release
 *
 * Stops watching an expression that was established via gtk_expression_watch().
 **/
void
gtk_expression_watch_unwatch (GtkExpressionWatch *watch)
{
  if (!gtk_expression_watch_is_watching (watch))
    return;

  gtk_expression_subwatch_finish (watch->expression, (GtkExpressionSubWatch *) watch->sub);

  if (watch->this)
    g_object_weak_unref (watch->this, gtk_expression_watch_this_cb, watch);

  if (watch->user_destroy)
    watch->user_destroy (watch->user_data);

  g_clear_pointer (&watch->expression, gtk_expression_unref);

  gtk_expression_watch_unref (watch);
}

/**
 * gtk_expression_watch_evaluate:
 * @watch: a #GtkExpressionWatch
 * @value: an empty #GValue to be set
 *
 * Evaluates the watched expression and on success stores the result
 * in @value.
 *
 * This is equivalent to calling gtk_expression_evaluate() with the
 * expression and this pointer originally used to create @watch.
 *
 * Returns: %TRUE if the expression could be evaluated and @value was set
 **/
gboolean
gtk_expression_watch_evaluate (GtkExpressionWatch *watch,
                               GValue             *value)
{
  g_return_val_if_fail (watch != NULL, FALSE);

  if (!gtk_expression_watch_is_watching (watch))
    return FALSE;

  return gtk_expression_evaluate (watch->expression, watch->this, value);
}

typedef struct {
  GtkExpressionWatch *watch;
  GObject *target;
  GParamSpec *pspec;
} GtkExpressionBind;

static void
invalidate_binds (gpointer unused,
                  GObject *object)
{
  GSList *l, *binds;

  binds = g_object_get_data (object, "gtk-expression-binds");
  for (l = binds; l; l = l->next)
    {
      GtkExpressionBind *bind = l->data;

      /* This guarantees we neither try to update bindings
       * (which would wreck havoc because the object is
       * dispose()ing itself) nor try to destroy bindings
       * anymore, so destruction can be done in free_binds().
       */
      bind->target = NULL;
    }
}

static void
free_binds (gpointer data)
{
  GSList *l;

  for (l = data; l; l = l->next)
    {
      GtkExpressionBind *bind = l->data;

      g_assert (bind->target == NULL);
      if (bind->watch)
        gtk_expression_watch_unwatch (bind->watch);
      g_slice_free (GtkExpressionBind, bind);
    }
  g_slist_free (data);
}

static void
gtk_expression_bind_free (gpointer data)
{
  GtkExpressionBind *bind = data;

  if (bind->target)
    {
      GSList *binds;
      binds = g_object_steal_data (bind->target, "gtk-expression-binds");
      binds = g_slist_remove (binds, bind);
      if (binds)
        g_object_set_data_full (bind->target, "gtk-expression-binds", binds, free_binds);
      else
        g_object_weak_unref (bind->target, invalidate_binds, NULL);

      g_slice_free (GtkExpressionBind, bind);
    }
  else
    {
      /* If a bind gets unwatched after invalidate_binds() but
       * before free_binds(), we end up here. This can happen if
       * the bind was watching itself or if the target's dispose()
       * function freed the object that was watched.
       * We make sure we don't destroy the binding or free_binds() will do
       * bad stuff, but we clear the watch, so free_binds() won't try to
       * unwatch() it.
       */
      bind->watch = NULL;
    }
}

static void
gtk_expression_bind_notify (gpointer data)
{
  GValue value = G_VALUE_INIT;
  GtkExpressionBind *bind = data;

  if (bind->target == NULL)
    return;

  if (!gtk_expression_watch_evaluate (bind->watch, &value))
    return;

  g_object_set_property (bind->target, bind->pspec->name, &value);
  g_value_unset (&value);
}

/**
 * gtk_expression_bind:
 * @self: (transfer full): a #GtkExpression
 * @target: (transfer none) (type GObject): the target object to bind to
 * @property: name of the property on @target to bind to
 * @this_: (transfer none) (type GObject): the this argument for
 *     the evaluation of @self
 *
 * Bind @target's property named @property to @self.
 *
 * The value that @self evaluates to is set via g_object_set() on
 * @target. This is repeated whenever @self changes to ensure that
 * the object's property stays synchronized with @self.
 *
 * If @self's evaluation fails, @target's @property is not updated.
 * You can ensure that this doesn't happen by using a fallback
 * expression.
 *
 * Note that this function takes ownership of @self. If you want
 * to keep it around, you should gtk_expression_ref() it beforehand.
 *
 * Returns: (transfer none): a #GtkExpressionWatch
 **/
GtkExpressionWatch *
gtk_expression_bind (GtkExpression *self,
                     gpointer       target,
                     const char    *property,
                     gpointer       this_)
{
  GtkExpressionBind *bind;
  GParamSpec *pspec;
  GSList *binds;

  g_return_val_if_fail (GTK_IS_EXPRESSION (self), NULL);
  g_return_val_if_fail (G_IS_OBJECT (target), NULL);
  g_return_val_if_fail (property != NULL, NULL);
  pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (target), property);
  if (G_UNLIKELY (pspec == NULL))
    {
      g_critical ("%s: Class '%s' has no property named '%s'",
                  G_STRFUNC, G_OBJECT_TYPE_NAME (target), property);
      return NULL;
    }
  if (G_UNLIKELY ((pspec->flags & (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY)) != G_PARAM_WRITABLE))
    {
      g_critical ("%s: property '%s' of class '%s' is not writable",
                 G_STRFUNC, pspec->name, G_OBJECT_TYPE_NAME (target));
      return NULL;
    }

  bind = g_slice_new0 (GtkExpressionBind);
  binds = g_object_steal_data (target, "gtk-expression-binds");
  if (binds == NULL)
    g_object_weak_ref (target, invalidate_binds, NULL);
  bind->target = target;
  bind->pspec = pspec;
  bind->watch = gtk_expression_watch (self,
                                      this_,
                                      gtk_expression_bind_notify,
                                      bind,
                                      gtk_expression_bind_free);
  binds = g_slist_prepend (binds, bind);
  g_object_set_data_full (target, "gtk-expression-binds", binds, free_binds);

  gtk_expression_unref (self);

  gtk_expression_bind_notify (bind);

  return bind->watch;
}