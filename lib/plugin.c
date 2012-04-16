/*
 * Copyright (c) 2002-2010 BalaBit IT Ltd, Budapest, Hungary
 * Copyright (c) 1998-2010 Balázs Scheidler
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

#include "plugin.h"
#include "driver.h"
#include "messages.h"
#include "cfg-lexer.h"

#include <gmodule.h>
#include <string.h>

#ifdef _AIX
#define G_MODULE_SUFFIX "a"
#endif

const gchar *ignored_libraries[] = {"libnnz11.so","libocci.so",NULL};

void
plugin_register(GlobalConfig *cfg, Plugin *p, gint number)
{
  gint i;

  for (i = 0; i < number; i++)
    {
      if (plugin_find(cfg, p[i].type, p[i].name))
        {
          msg_debug("Attempted to register the same plugin multiple times, ignoring",
                    evt_tag_str("context", cfg_lexer_lookup_context_name_by_type(p[i].type)),
                    evt_tag_str("name", p[i].name),
                    NULL);
          continue;
        }
      cfg->plugins = g_list_prepend(cfg->plugins, &p[i]);
    }
}

Plugin *
plugin_find(GlobalConfig *cfg, gint plugin_type, const gchar *plugin_name)
{
  GList *p;
  Plugin *plugin;
  gint i;

  for (p = cfg->plugins; p; p = g_list_next(p))
    {
      plugin = p->data;
      if (plugin->type == plugin_type)
        {
          for (i = 0; plugin->name[i] && plugin_name[i]; i++)
            {
              if (plugin->name[i] != plugin_name[i] &&
                  !((plugin->name[i] == '-' || plugin->name[i] == '_') &&
                    (plugin_name[i] == '-' || plugin_name[i] == '_')))
                {
                  break;
                }
            }
          if (plugin_name[i] == 0 && plugin->name[i] == 0)
            return plugin;
        }
    }
  return NULL;
}

/* construct a plugin without having a configuration file to parse */
gpointer
plugin_construct(Plugin *self, GlobalConfig *cfg, gint plugin_type, const gchar *plugin_name)
{
  g_assert(self->parser == NULL);
  if (self->construct)
    {
      return self->construct(self, cfg, plugin_type, plugin_name);
    }
  return NULL;
}

/* construct a plugin instance by parsing a configuration file */
gpointer
plugin_parse_config(Plugin *self, GlobalConfig *cfg, YYLTYPE *yylloc, gpointer arg)
{
  gpointer instance = NULL;

  g_assert(self->construct == NULL);

  /* make sure '_' and '-' are handled equally in plugin name */
  if (!self->setup_context)
    {
      CfgTokenBlock *block;
      YYSTYPE token;

      block = cfg_token_block_new();

      memset(&token, 0, sizeof(token));
      token.type = LL_TOKEN;
      token.token = self->type;
      cfg_token_block_add_token(block, &token);
      cfg_lexer_push_context(cfg->lexer, self->parser->context, self->parser->keywords, self->parser->name);
      cfg_lexer_lookup_keyword(cfg->lexer, &token, yylloc, self->name);
      cfg_lexer_pop_context(cfg->lexer);
      cfg_token_block_add_token(block, &token);

      cfg_lexer_inject_token_block(cfg->lexer, block);
    }
  else
    {
      (self->setup_context)(self, cfg, self->type, self->name);
    }

  if (!cfg_parser_parse(self->parser, cfg->lexer, &instance, arg))
    {
      cfg_parser_cleanup(self->parser, instance);
      instance = NULL;
    }

  return instance;
}

static GModule *
plugin_dlopen_module(const gchar *module_name, const gchar *module_paths)
{
  gchar *plugin_module_name = NULL;
  gchar **module_path_dirs, *p, *dot;
  GModule *mod;
  gint i;

  module_path_dirs = g_strsplit(module_paths, G_SEARCHPATH_SEPARATOR_S, 0);
  i = 0;
  while (module_path_dirs && module_path_dirs[i])
    {
      plugin_module_name = g_module_build_path(module_path_dirs[i], module_name);
      if (g_file_test(plugin_module_name, G_FILE_TEST_EXISTS))
        break;

      /* also check if a libtool archive exists (for example in the build directory) */
#ifndef _AIX
      dot = strrchr(plugin_module_name, '.');
      if (dot)
        {
          *dot = 0;
          p = g_strdup_printf("%s.la", plugin_module_name);
          g_free(plugin_module_name);
          plugin_module_name = p;
        }
      if (g_file_test(plugin_module_name, G_FILE_TEST_EXISTS))
        break;

      /* On AIX the modules in .a files */
#else
      dot = strrchr(plugin_module_name, '.');
      if (dot)
        {
          *dot = 0;
          p = g_strdup_printf("%s.a", plugin_module_name);
          g_free(plugin_module_name);
          plugin_module_name = p;
        }
      if (g_file_test(plugin_module_name, G_FILE_TEST_EXISTS))
        break;
#endif

      g_free(plugin_module_name);
      plugin_module_name = NULL;
      i++;
    }
  g_strfreev(module_path_dirs);
  if (!plugin_module_name)
    {
      msg_error("Plugin module not found in 'module-path'",
                evt_tag_str("module-path", module_paths),
                evt_tag_str("module", module_name),
                evt_tag_id(MSG_CANT_FIND_PLUGIN),
                NULL);
      return NULL;
    }
  msg_debug("Trying to open module",
            evt_tag_str("module", module_name),
            evt_tag_str("filename", plugin_module_name),
            NULL);

  mod = g_module_open(plugin_module_name, G_MODULE_BIND_LOCAL);
  g_free(plugin_module_name);
  if (!mod)
    {
      msg_error("Error opening plugin module",
                evt_tag_str("module", module_name),
                evt_tag_str("error", g_module_error()),
                evt_tag_id(MSG_CANT_OPEN_PLUGIN),
                NULL);
      return NULL;
    }
  return mod;
}

gboolean
plugin_load_module(const gchar *module_name, GlobalConfig *cfg, CfgArgs *args)
{
  GModule *mod;
  static GModule *main_module_handle;
  gboolean (*init_func)(GlobalConfig *cfg, CfgArgs *args);
  gchar *module_init_func;
  const gchar *mp;
  gchar *p;

  if (!main_module_handle)
    main_module_handle = g_module_open(NULL, 0);
  module_init_func = g_strdup_printf("%s_module_init", module_name);
  for (p = module_init_func; *p; p++)
    {
      if ((*p) == '-')
        *p = '_';
    }

  if (g_module_symbol(main_module_handle, module_init_func, (gpointer *) &init_func))
    {
      /* already linked in, no need to load explicitly */
      goto call_init;
    }

  if (cfg->lexer)
    mp = cfg_args_get(cfg->lexer->globals, "module-path");
  else
    mp = module_path;

  mod = plugin_dlopen_module(module_name, mp);

  if (!mod)
    {
      g_free(module_init_func);
      return FALSE;
    }
  g_module_make_resident(mod);

  if (!g_module_symbol(mod, module_init_func, (gpointer *) &init_func))
    {
      msg_error("Error finding init function in module",
                evt_tag_str("module", module_name),
                evt_tag_str("symbol", module_init_func),
                evt_tag_str("error", g_module_error()),
                evt_tag_id(MSG_CANT_FIND_PLUGIN_INIT_FUNCTION),
                NULL);
      g_free(module_init_func);
      return FALSE;
    }
 call_init:
  g_free(module_init_func);
  return (*init_func)(cfg, args);
}

void
plugin_list_modules(FILE *out, gboolean verbose)
{
  GModule *mod;
  gchar **mod_paths;
  gint i, j, k;
  gboolean first = TRUE;

  mod_paths = g_strsplit(module_path, ":", 0);
  for (i = 0; mod_paths[i]; i++)
    {
      GDir *dir;
      const gchar *fname;

      dir = g_dir_open(mod_paths[i], 0, NULL);
      if (!dir)
        continue;
      while ((fname = g_dir_read_name(dir)))
        {
          gint index = 0;
          while(ignored_libraries[index])
            {
              if (strcmp(fname,ignored_libraries[index]) == 0)
                {
                  break;
                }
              index++;
            }
          if (g_str_has_suffix(fname, G_MODULE_SUFFIX))
            {
              gchar *module_name;
              ModuleInfo *module_info;
              gboolean success;

              if (g_str_has_prefix(fname, "lib"))
                fname += 3;
              module_name = g_strndup(fname, (gint) (strlen(fname) - strlen(G_MODULE_SUFFIX) - 1));

              mod = plugin_dlopen_module(module_name, mod_paths[i]);
              if (mod)
                success = g_module_symbol(mod, "module_info", (gpointer *) &module_info);
              else
                success = FALSE;
              if (verbose)
                {
                  fprintf(out, "Module: %s\n", module_name);
                  if (mod)
                    {
                      if (!success || !module_info)
                        {
                          fprintf(out, "Status: Unable to resolve module_info variable, probably not a syslog-ng module\n");
                        }
                      else
                        {
                          if (strcmp(module_info->canonical_name, module_name) != 0)
                            {
                              fprintf(out, "Status: This module is to be loaded under the name %s instead of %s\n", module_info->canonical_name, module_name);
                            }
                          else
                            {
                              gchar **lines;

                              fprintf(out, "Status: ok\n"
                                           "Version: %s\n"
                                           "Core-Revision: %s\n"
                                           "Description:\n", module_info->version, module_info->core_revision);

                              lines = g_strsplit(module_info->description, "\n", 0);
                              for (k = 0; lines[k]; k++)
                                fprintf(out, "  %s\n", lines[k][0] ? lines[k] : ".");
                              g_strfreev(lines);

                              fprintf(out, "Plugins:\n");
                              for (j = 0; j < module_info->plugins_len; j++)
                                {
                                  Plugin *plugin = &module_info->plugins[j];

                                  fprintf(out, "  %-15s %s\n", cfg_lexer_lookup_context_name_by_type(plugin->type), plugin->name);
                                }
                            }
                        }
                      g_module_close(mod);
                    }
                  else
                    {
                      fprintf(out, "Status: Unable to dlopen shared object, probably not a syslog-ng module\n");
                    }
                  fprintf(out, "\n");
                }
              else if (success && module_info)
                {
                  fprintf(out, "%s%s", first ? "" : ",", module_name);
                  first = FALSE;
                }
              g_free(module_name);
            }
        }
      g_dir_close(dir);
    }
  g_strfreev(mod_paths);
  if (!verbose)
    fprintf(out, "\n");
}
