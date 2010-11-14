#include <e.h>
#include "e_mod_main.h"

typedef struct _Instance Instance;
typedef struct _Uptime Uptime;
typedef struct _E_Obj_Dialog E_Rename;

struct _Instance
{
   E_Gadcon_Client *gcc;
   Evas_Object *ut_obj;
   Uptime *ut;
   Ecore_Event_Handler *handler;
   time_t uptime;
   double la[3];
   Config_Item *ci;
};

struct _Uptime
{
   Instance *inst;
   Evas_Object *ut_obj;
};

static E_Gadcon_Client *_gc_init(E_Gadcon * gc, const char *name,
                                 const char *id, const char *style);
static void _gc_shutdown(E_Gadcon_Client * gcc);
static void _gc_orient(E_Gadcon_Client * gcc, E_Gadcon_Orient orient);
static char *_gc_label(E_Gadcon_Client_Class * client_class);
static Evas_Object *_gc_icon(E_Gadcon_Client_Class * client_class, Evas * evas);
static const char *_gc_id_new(E_Gadcon_Client_Class * client_class);
static void _ut_cb_mouse_down(void *data, Evas * e, Evas_Object * obj,
                              void *event_info);
static void _ut_menu_cb_configure(void *data, E_Menu * m, E_Menu_Item * mi);
static void _ut_menu_cb_post(void *data, E_Menu * m);
static Config_Item *_ut_config_item_get(const char *id);
static Uptime *_ut_new(Evas * evas);
static void _ut_free(Uptime * ut);
static Eina_Bool _ut_cb_check(void *data);
static E_Config_DD *conf_edd = NULL;
static E_Config_DD *conf_item_edd = NULL;

Config *ut_config = NULL;

static const E_Gadcon_Client_Class _gc_class = {
   GADCON_CLIENT_CLASS_VERSION, "desktitle",
   {
    _gc_init, _gc_shutdown, _gc_orient, _gc_label, _gc_icon, _gc_id_new, NULL,
    NULL},
   E_GADCON_CLIENT_STYLE_PLAIN
};

static Eina_Bool
_e_zone_cb_desk_after_show(void *data, int type, void *event)
{
   _ut_cb_check(data);
   return ECORE_CALLBACK_PASS_ON;
}

static E_Gadcon_Client *
_gc_init(E_Gadcon * gc, const char *name, const char *id, const char *style)
{
   Evas_Object *o;
   E_Gadcon_Client *gcc;
   Instance *inst;
   Uptime *ut;

   inst = E_NEW(Instance, 1);
   inst->ci = _ut_config_item_get(id);

   ut = _ut_new(gc->evas);
   ut->inst = inst;
   inst->ut = ut;

   o = ut->ut_obj;
   gcc = e_gadcon_client_new(gc, name, id, style, o);
   gcc->data = inst;
   inst->gcc = gcc;
   inst->ut_obj = o;

   inst->handler = ecore_event_handler_add(E_EVENT_DESK_AFTER_SHOW,
                                           _e_zone_cb_desk_after_show, inst);
   inst->handler = ecore_event_handler_add(E_EVENT_DESK_NAME_CHANGE,
                                           _e_zone_cb_desk_after_show, inst);

   evas_object_event_callback_add(o, EVAS_CALLBACK_MOUSE_DOWN,
                                  _ut_cb_mouse_down, inst);
   ut_config->instances = eina_list_append(ut_config->instances, inst);

   _ut_cb_check(inst);

   return gcc;
}

static void
_gc_shutdown(E_Gadcon_Client * gcc)
{
   Instance *inst;
   Uptime *ut;

   if (!gcc)
      return;
   if (!gcc->data)
      return;

   inst = gcc->data;
   if (!(ut = inst->ut))
      return;

   ut_config->instances = eina_list_remove(ut_config->instances, inst);
   evas_object_event_callback_del(ut->ut_obj, EVAS_CALLBACK_MOUSE_DOWN,
                                  _ut_cb_mouse_down);
   ecore_event_handler_del(inst->handler);

   _ut_free(ut);
   free(inst);
   inst = NULL;
}

static void
_gc_orient(E_Gadcon_Client * gcc, E_Gadcon_Orient orient)
{
   e_gadcon_client_aspect_set(gcc, 16, 16);
   e_gadcon_client_min_size_set(gcc, 16, 16);
}

static char *
_gc_label(E_Gadcon_Client_Class * client_class)
{
   return D_("DeskTitle");
}

static Evas_Object *
_gc_icon(E_Gadcon_Client_Class * client_class, Evas * evas)
{
   Evas_Object *o;

   char buf[PATH_MAX];

   o = edje_object_add(evas);
   snprintf(buf, sizeof(buf), "%s/module.edj",
            e_module_dir_get(ut_config->module));
   edje_object_file_set(o, buf, "icon");

   return o;
}

static const char *
_gc_id_new(E_Gadcon_Client_Class * client_class)
{
   Config_Item *ci;

   ci = _ut_config_item_get(NULL);
   return ci->id;
}

static void
_cb_entry_ok(char *text, void *data)
{
   Instance *inst;
   E_Zone *zone;
   E_Desk *desk;
   E_Container *con;

   inst = data;

   zone = inst->gcc->gadcon->zone;
   con = zone->container;
   desk = e_desk_current_get(zone);

   e_desk_name_del(con->num, zone->num, desk->x, desk->y);
   e_desk_name_add(con->num, zone->num, desk->x, desk->y, text);
   e_desk_name_update();
   e_config_save_queue();
}

static void
_ut_cb_mouse_down(void *data, Evas * e, Evas_Object * obj, void *event_info)
{
   Instance *inst;

   Evas_Event_Mouse_Down *ev;

   E_Desk *desk;

   if (ut_config->menu)
      return;

   inst = data;
   ev = event_info;

   desk = e_desk_current_get(inst->gcc->gadcon->zone);

   if (ev->button == 1 && ev->flags & EVAS_BUTTON_DOUBLE_CLICK)
     {
        e_entry_dialog_show(D_("Edit Desktop Name"), "preferences-desktop",
                            D_("Enter a name for this desktop:"), desk->name,
                            D_("Save"), NULL, _cb_entry_ok, NULL, inst);
     }
   else if (ev->button == 3)
     {
        E_Menu *ma, *mg;
        E_Menu_Item *mi;
        int x, y, w, h;

        ma = e_menu_new();
        e_menu_post_deactivate_callback_set(ma, _ut_menu_cb_post, inst);
        ut_config->menu = ma;

        mg = e_menu_new();

        mi = e_menu_item_new(mg);
        e_menu_item_label_set(mi, D_("Settings"));
        e_util_menu_item_theme_icon_set(mi, "preferences-system");
        e_menu_item_callback_set(mi, _ut_menu_cb_configure, inst);

        e_gadcon_client_util_menu_items_append(inst->gcc, ma, mg, 0);
        e_gadcon_canvas_zone_geometry_get(inst->gcc->gadcon, &x, &y, &w, &h);
        e_menu_activate_mouse(ma,
                              e_util_zone_current_get(e_manager_current_get()),
                              x + ev->output.x, y + ev->output.y, 1, 1,
                              E_MENU_POP_DIRECTION_DOWN, ev->timestamp);
        evas_event_feed_mouse_up(inst->gcc->gadcon->evas, ev->button,
                                 EVAS_BUTTON_NONE, ev->timestamp, NULL);
     }
}

static void
_ut_menu_cb_post(void *data, E_Menu * m)
{
   if (!ut_config->menu)
      return;

   e_object_del(E_OBJECT(ut_config->menu));
   ut_config->menu = NULL;
}

static void
_ut_menu_cb_configure(void *data, E_Menu * m, E_Menu_Item * mi)
{
   Instance *inst;

   inst = data;
   _config_ut_module(inst->ci);
}

static Config_Item *
_ut_config_item_get(const char *id)
{
   Eina_List *l;
   Config_Item *ci;
   char buf[128];

   if (!id)
     {
        int num = 0;

        /* Create id */
        if (ut_config->items)
          {
             const char *p;

             ci = eina_list_last(ut_config->items)->data;
             p = strrchr(ci->id, '.');
             if (p)
               {
                  num = atoi(p + 1) + 1;
               }
          }
        snprintf(buf, sizeof(buf), "%s.%d", _gc_class.name, num);
        id = buf;
     }
   else
     {
        for (l = ut_config->items; l; l = l->next)
          {
             ci = l->data;
             if (!ci->id)
                continue;
             if (strcmp(ci->id, id) == 0)
                return ci;
          }
     }

   ci = E_NEW(Config_Item, 1);
   ci->id = eina_stringshare_add(id);
   ci->check_interval = 60.0;
   ci->update_interval = 60.0;

   ut_config->items = eina_list_append(ut_config->items, ci);

   return ci;
}

void
_ut_config_updated(Config_Item * ci)
{
   Eina_List *l;

   if (!ut_config)
      return;

   for (l = ut_config->instances; l; l = l->next)
     {
        Instance *inst;

        inst = l->data;
        if (inst->ci != ci)
           continue;
     }
}

EAPI E_Module_Api e_modapi = {
   E_MODULE_API_VERSION, "DeskTitle"
};

EAPI void *
e_modapi_init(E_Module * m)
{
   char buf[PATH_MAX];

   snprintf(buf, sizeof(buf), "%s/locale", e_module_dir_get(m));
   bindtextdomain(PACKAGE, buf);
   bind_textdomain_codeset(PACKAGE, "UTF-8");

   conf_item_edd = E_CONFIG_DD_NEW("DeskTitle_Config_Item", Config_Item);
#undef T
#undef D
#define T Config_Item
#define D conf_item_edd
   E_CONFIG_VAL(D, T, id, STR);
   E_CONFIG_VAL(D, T, check_interval, INT);
   E_CONFIG_VAL(D, T, update_interval, INT);

   conf_edd = E_CONFIG_DD_NEW("DeskTitle_Config", Config);
#undef T
#undef D
#define T Config
#define D conf_edd
   E_CONFIG_LIST(D, T, items, conf_item_edd);

   ut_config = e_config_domain_load("module.desktitle", conf_edd);
   if (!ut_config)
     {
        Config_Item *ci;

        ut_config = E_NEW(Config, 1);
        ci = E_NEW(Config_Item, 1);
        ci->id = eina_stringshare_add("0");
        ci->check_interval = 60.0;
        ci->update_interval = 60.0;
        ut_config->items = eina_list_append(ut_config->items, ci);
     }
   ut_config->module = m;
   e_gadcon_provider_register(&_gc_class);

   return m;
}

EAPI int
e_modapi_shutdown(E_Module * m)
{
   ut_config->module = NULL;
   e_gadcon_provider_unregister(&_gc_class);

   if (ut_config->config_dialog)
      e_object_del(E_OBJECT(ut_config->config_dialog));

   if (ut_config->menu)
     {
        e_menu_post_deactivate_callback_set(ut_config->menu, NULL, NULL);
        e_object_del(E_OBJECT(ut_config->menu));
        ut_config->menu = NULL;
     }

   while (ut_config->items)
     {
        Config_Item *ci;

        ci = ut_config->items->data;
        ut_config->items = eina_list_remove_list(ut_config->items,
                                                 ut_config->items);

        if (ci->id)
           eina_stringshare_del(ci->id);

        E_FREE(ci);
     }

   E_FREE(ut_config);
   E_CONFIG_DD_FREE(conf_item_edd);
   E_CONFIG_DD_FREE(conf_edd);

   return 1;
}

EAPI int
e_modapi_save(E_Module * m)
{
   e_config_domain_save("module.desktitle", conf_edd, ut_config);
   return 1;
}

static Uptime *
_ut_new(Evas * evas)
{
   Uptime *ut;
   char buf[PATH_MAX];

   ut = E_NEW(Uptime, 1);
   snprintf(buf, sizeof(buf), "%s/desktitle.edj",
            e_module_dir_get(ut_config->module));

   ut->ut_obj = edje_object_add(evas);
   if (!e_theme_edje_object_set(ut->ut_obj,
                                "base/theme/modules/desktitle",
                                "modules/desktitle/main"))
      edje_object_file_set(ut->ut_obj, buf, "modules/desktitle/main");

   evas_object_show(ut->ut_obj);
   return ut;
}

static void
_ut_free(Uptime * ut)
{
   evas_object_del(ut->ut_obj);
   E_FREE(ut);
}

static Eina_Bool
_ut_cb_check(void *data)
{
   Instance *inst;
   E_Desk *desk;

   if (!(inst = data))
      return EINA_FALSE;

   desk = e_desk_current_get(inst->gcc->gadcon->zone);

   if (desk->name != NULL)
     {
        edje_object_part_text_set(inst->ut->ut_obj, "desktitle", desk->name);
        return EINA_TRUE;
     }

   return EINA_TRUE;
}
