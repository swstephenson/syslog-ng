/*
 * Copyright (c) 2002-2017 Balabit
 * Copyright (c) 1998-2017 Balázs Scheidler
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "cfg-block-generator.h"

gboolean
cfg_block_generator_generate(CfgBlockGenerator *self, GlobalConfig *cfg, CfgLexer *lexer, CfgArgs *args)
{
  return self->generate(self, cfg, lexer, args);
}

void
cfg_block_generator_init_instance(CfgBlockGenerator *self, gint context, const gchar *name)
{
  self->context = context;
  self->name = g_strdup(name);
  self->free_fn = cfg_block_generator_free_instance;
}

void
cfg_block_generator_free_instance(CfgBlockGenerator *self)
{
  g_free(self->name);
}

void
cfg_block_generator_free(CfgBlockGenerator *self)
{
  if (self->free_fn)
    self->free_fn(self);
  g_free(self);
}
