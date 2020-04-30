//-------------------------------------------------------------------------
/*
Copyright (C) 2010 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#include "build.h"
#include "common.h"
#include "common_game.h"
#include "compat.h"
#include "dynamicgtk.h"
#include "game.h"
#include "grpscan.h"
#include "gtkpixdata.h"
#include "config.h"

#if defined POLYMER && 0
# define POLYMEROPTION
#endif

enum
{
    NONE,
    ALL,
    POPULATE_VIDEO,
    POPULATE_CONFIG,
    POPULATE_GAME,
};

enum
{
    TAB_CONFIG,
    TAB_GAME,
    TAB_MESSAGES,
};

enum
{
    INPUT_KB,
    INPUT_MOUSE,
    INPUT_JOYSTICK,
    INPUT_ALL,
};

static struct
{
    GtkWidget *startwin;
    GtkWidget *hlayout;
    GtkWidget *banner;
    GtkWidget *vlayout;
    GtkWidget *tabs;
    GtkWidget *configtlayout;
    GtkWidget *displayvlayout;
    GtkWidget *vmode3dlabel;
    GtkWidget *vmode3dcombo;
    GtkWidget *fullscreencheck;
#ifdef POLYMEROPTION
    GtkWidget *polymercheck;
#endif
    GtkWidget *inputdevlabel;
    GtkWidget *inputdevcombo;
    GtkWidget *emptyhlayout;
    GtkWidget *alwaysshowcheck;
    GtkWidget *configtab;
    GtkWidget *gamevlayout;
    GtkWidget *gamelabel;
    GtkWidget *gamescroll;
    GtkWidget *gamelist;
    GtkWidget *gametab;
    GtkWidget *messagesscroll;
    GtkWidget *messagestext;
    GtkWidget *messagestab;
    GtkWidget *buttons;
    GtkWidget *cancelbutton;
    GtkWidget *cancelbuttonalign;
    GtkWidget *cancelbuttonlayout;
    GtkWidget *cancelbuttonicon;
    GtkWidget *cancelbuttonlabel;
    GtkWidget *startbutton;
    GtkWidget *startbuttonalign;
    GtkWidget *startbuttonlayout;
    GtkWidget *startbuttonicon;
    GtkWidget *startbuttonlabel;
} stwidgets;

static struct
{
    grpfile_t const * grp;
    ud_setup_t shared;
#ifdef POLYMEROPTION
    int polymer;
#endif
} settings;

static int32_t retval = -1, mode = TAB_MESSAGES;
extern int32_t gtkenabled;
static void PopulateForm(unsigned char pgs);


// -- EVENT CALLBACKS AND CREATION STUFF --------------------------------------

static void on_vmode3dcombo_changed(GtkComboBox *combobox, gpointer user_data)
{
    GtkTreeModel *data;
    GtkTreeIter iter;
    int32_t val;
    UNREFERENCED_PARAMETER(user_data);

    if (!gtk_combo_box_get_active_iter(combobox, &iter)) return;
    if (!(data = gtk_combo_box_get_model(combobox))) return;
    gtk_tree_model_get(data, &iter, 1, &val, -1);
    settings.shared.xdim = validmode[val].xdim;
    settings.shared.ydim = validmode[val].ydim;
    settings.shared.bpp = validmode[val].bpp;
}

static void on_fullscreencheck_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    UNREFERENCED_PARAMETER(user_data);
    settings.shared.fullscreen = gtk_toggle_button_get_active(togglebutton);
    PopulateForm(POPULATE_VIDEO);
}

#ifdef POLYMEROPTION
static void on_polymercheck_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    UNREFERENCED_PARAMETER(user_data);
    if (gtk_toggle_button_get_active(togglebutton))
    {
        glrendmode = REND_POLYMER;
        settings.polymer = TRUE;
        if (settings.shared.bpp == 8)
        {
            settings.shared.bpp = 32;
            PopulateForm(POPULATE_VIDEO);
        }
    }
    else
    {
        glrendmode = REND_POLYMOST;
        settings.polymer = FALSE;
    }
}
#endif

static void on_inputdevcombo_changed(GtkComboBox *combobox, gpointer user_data)
{
    UNREFERENCED_PARAMETER(user_data);
    switch (gtk_combo_box_get_active(combobox))
    {
    case 0: settings.shared.usemouse = 0; settings.shared.usejoystick = 0; break;
    case 1:	settings.shared.usemouse = 1; settings.shared.usejoystick = 0; break;
    case 2:	settings.shared.usemouse = 0; settings.shared.usejoystick = 1; break;
    case 3:	settings.shared.usemouse = 1; settings.shared.usejoystick = 1; break;
    }
}

static void on_alwaysshowcheck_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
    UNREFERENCED_PARAMETER(user_data);
    settings.shared.forcesetup = gtk_toggle_button_get_active(togglebutton);
}

static void on_cancelbutton_clicked(GtkButton *button, gpointer user_data)
{
    UNREFERENCED_PARAMETER(button);
    UNREFERENCED_PARAMETER(user_data);
    if (mode == TAB_CONFIG) { retval = 0; gtk_main_quit(); }
    else quitevent++;
}

static void on_startbutton_clicked(GtkButton *button, gpointer user_data)
{
    UNREFERENCED_PARAMETER(button);
    UNREFERENCED_PARAMETER(user_data);
    retval = 1;
    gtk_main_quit();
}

static void on_gamelist_selection_changed(GtkTreeSelection *selection, gpointer user_data)
{
    GtkTreeIter iter;
    GtkTreeModel *model;
    UNREFERENCED_PARAMETER(user_data);

    if (gtk_tree_selection_get_selected(selection, &model, &iter))
    {
        grpfile_t const *fg;
        gtk_tree_model_get(model, &iter, 2, (gpointer)&fg, -1);
        settings.grp = fg;
    }
}

static gboolean on_startwin_delete_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    UNREFERENCED_PARAMETER(widget);
    UNREFERENCED_PARAMETER(event);
    UNREFERENCED_PARAMETER(user_data);
    if (mode == TAB_CONFIG) { retval = 0; gtk_main_quit(); }
    else quitevent++;
    return TRUE;	// FALSE would let the event go through. we want the game to decide when to close
}


// -- SUPPORT FUNCTIONS -------------------------------------------------------

static GdkPixbuf *load_banner(void)
{
    return gdk_pixbuf_from_pixdata((GdkPixdata const *)&startbanner_pixdata, FALSE, NULL);
}

static void SetPage(int32_t n)
{
    if (!gtkenabled || !stwidgets.startwin) return;
    mode = n;
    gtk_notebook_set_current_page(GTK_NOTEBOOK(stwidgets.tabs), n);

    // each control in the config page vertical layout plus the start button should be made (in)sensitive
    if (n == TAB_CONFIG) n = TRUE; else n = FALSE;
    gtk_widget_set_sensitive(stwidgets.startbutton, n);
    gtk_container_foreach(GTK_CONTAINER(stwidgets.configtlayout),
                          (GtkCallback)gtk_widget_set_sensitive,
                          (gpointer)&n);
}

static void PopulateForm(unsigned char pgs)
{
    if ((pgs == ALL) || (pgs == POPULATE_VIDEO))
    {
        int32_t mode3d, i;
        GtkListStore *modes3d;
        GtkTreeIter iter;
        char buf[64];

        mode3d = videoCheckMode(&settings.shared.xdim, &settings.shared.ydim, settings.shared.bpp, settings.shared.fullscreen, 1);
        if (mode3d < 0)
        {
            int32_t i, cd[] = { 32, 24, 16, 15, 8, 0 };

            for (i=0; cd[i];) { if (cd[i] >= settings.shared.bpp) i++; else break; }
            for (; cd[i]; i++)
            {
                mode3d = videoCheckMode(&settings.shared.xdim, &settings.shared.ydim, cd[i], settings.shared.fullscreen, 1);
                if (mode3d < 0) continue;
                settings.shared.bpp = cd[i];
                break;
            }
        }

        modes3d = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(stwidgets.vmode3dcombo)));
        gtk_list_store_clear(modes3d);

        for (i=0; i<validmodecnt; i++)
        {
            if (validmode[i].fs != settings.shared.fullscreen) continue;

            // all modes get added to the 3D mode list
            Bsprintf(buf, "%dx%d %s", validmode[i].xdim, validmode[i].ydim, validmode[i].bpp == 8 ? "software" : "OpenGL");
            gtk_list_store_append(modes3d, &iter);
            gtk_list_store_set(modes3d, &iter, 0,buf, 1,i, -1);
            if (i == mode3d)
            {
                g_signal_handlers_block_by_func(stwidgets.vmode3dcombo, (gpointer)on_vmode3dcombo_changed, NULL);
                gtk_combo_box_set_active_iter(GTK_COMBO_BOX(stwidgets.vmode3dcombo), &iter);
                g_signal_handlers_unblock_by_func(stwidgets.vmode3dcombo, (gpointer)on_vmode3dcombo_changed, NULL);
            }
        }
    }

    if ((pgs == ALL) || (pgs == POPULATE_CONFIG))
    {
        GtkTreeIter iter;
        unsigned char i;
        const char *availabledev[] =
        {
            "Keyboard only",
            "Keyboard and mouse",
            "Keyboard and joystick",
            "All supported devices"
        };

        // populate input devices combo
        GtkListStore * devlist = GTK_LIST_STORE(gtk_combo_box_get_model(GTK_COMBO_BOX(stwidgets.inputdevcombo)));
        gtk_list_store_clear(devlist);

        for (i=0; i<(int32_t)G_N_ELEMENTS(availabledev); i++)
        {
            gtk_list_store_append(devlist, &iter);
            gtk_list_store_set(devlist, &iter, 0,availabledev[i], -1);
        }
        switch (settings.shared.usemouse)
        {
        case 0: if (settings.shared.usejoystick)
                gtk_combo_box_set_active(GTK_COMBO_BOX(stwidgets.inputdevcombo), INPUT_JOYSTICK);
            else
                gtk_combo_box_set_active(GTK_COMBO_BOX(stwidgets.inputdevcombo), INPUT_KB);
            break;
        case 1:	if (settings.shared.usejoystick)
                gtk_combo_box_set_active(GTK_COMBO_BOX(stwidgets.inputdevcombo), INPUT_ALL);
            else
                gtk_combo_box_set_active(GTK_COMBO_BOX(stwidgets.inputdevcombo), INPUT_MOUSE);
            break;
        }

        // populate check buttons
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stwidgets.fullscreencheck), settings.shared.fullscreen);
#ifdef POLYMEROPTION
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stwidgets.polymercheck), settings.polymer);
#endif
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(stwidgets.alwaysshowcheck), settings.shared.forcesetup);
    }

    if ((pgs == ALL) || (pgs == POPULATE_GAME))
    {
        GtkListStore *list;
        GtkTreeIter iter;
        GtkTreeView *gamelist;

        gamelist = GTK_TREE_VIEW(stwidgets.gamelist);
        list = GTK_LIST_STORE(gtk_tree_view_get_model(gamelist));
        gtk_list_store_clear(list);

        for (grpfile_t const * fg = foundgrps; fg; fg=fg->next)
        {
            gtk_list_store_append(list, &iter);
            gtk_list_store_set(list, &iter, 0, fg->type->name, 1, fg->filename, 2, (void const *)fg, -1);
            if (settings.grp == fg)
            {
                GtkTreeSelection *sel = gtk_tree_view_get_selection(gamelist);
                g_signal_handlers_block_by_func(sel, (gpointer)on_gamelist_selection_changed, NULL);
                gtk_tree_selection_select_iter(sel, &iter);
                g_signal_handlers_unblock_by_func(sel, (gpointer)on_gamelist_selection_changed, NULL);
            }
        }
    }
}

static gint name_sorter(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer user_data)
{
    gchar *as, *bs;
    gint r;
    UNREFERENCED_PARAMETER(user_data);
    gtk_tree_model_get(model, a, 0, &as, -1);
    gtk_tree_model_get(model, b, 0, &bs, -1);

    r = g_utf8_collate(as,bs);

    g_free(as);
    g_free(bs);

    return r;
}

static GtkWidget *create_window(void)
{
    // Basic window
    stwidgets.startwin = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(stwidgets.startwin), apptitle);	// NOTE: use global app title
    gtk_window_set_position(GTK_WINDOW(stwidgets.startwin), GTK_WIN_POS_CENTER);
    gtk_window_set_resizable(GTK_WINDOW(stwidgets.startwin), FALSE);
    gtk_window_set_type_hint(GTK_WINDOW(stwidgets.startwin), GDK_WINDOW_TYPE_HINT_DIALOG);

    // Horizontal layout of banner and controls
    stwidgets.hlayout = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(stwidgets.startwin), stwidgets.hlayout);

    // banner
    {
        GdkPixbuf *pixbuf = load_banner();
        stwidgets.banner = gtk_image_new_from_pixbuf(pixbuf);
        g_object_unref((gpointer)pixbuf);
    }
    gtk_box_pack_start(GTK_BOX(stwidgets.hlayout), stwidgets.banner, FALSE, FALSE, 0);
    gtk_misc_set_alignment(GTK_MISC(stwidgets.banner), 0.5, 0);

    // Vertical layout of tab control and start+cancel buttons
    stwidgets.vlayout = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stwidgets.hlayout), stwidgets.vlayout, TRUE, TRUE, 0);

    // Tab control
    stwidgets.tabs = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(stwidgets.vlayout), stwidgets.tabs, TRUE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(stwidgets.tabs), 4);

    // layout table of config page
    stwidgets.configtlayout = gtk_table_new(6, 3, FALSE);
    gtk_container_add(GTK_CONTAINER(stwidgets.tabs), stwidgets.configtlayout);

    // 3D video mode LabelText
    stwidgets.vmode3dlabel = gtk_label_new_with_mnemonic("_Video mode:");
    gtk_misc_set_alignment(GTK_MISC(stwidgets.vmode3dlabel), 0.3, 0);
#ifdef POLYMEROPTION
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.vmode3dlabel, 0,1, 0,1, GTK_FILL, (GtkAttachOptions)0, 4, 0);
#else
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.vmode3dlabel, 0,1, 0,1, GTK_FILL, (GtkAttachOptions)0, 4, 7);
#endif

    // 3D video mode combo
    {
        GtkListStore *list = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_INT);
        GtkCellRenderer *cell;

        stwidgets.vmode3dcombo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(list));
        g_object_unref(G_OBJECT(list));

        cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(stwidgets.vmode3dcombo), cell, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(stwidgets.vmode3dcombo), cell, "text", 0, nullptr);
    }

#ifdef POLYMEROPTION
   gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.vmode3dcombo, 1,2, 0,1,
       (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), (GtkAttachOptions)0, 4, 0);
#else
   gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.vmode3dcombo, 1,2, 0,1, (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), (GtkAttachOptions)0, 4, 7);
#endif

    // Fullscreen checkbox
    stwidgets.displayvlayout = gtk_vbox_new(TRUE, 0);
#ifdef POLYMEROPTION
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.displayvlayout, 2,3, 0,1, GTK_FILL, (GtkAttachOptions)0, 4, 0);
#else
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.displayvlayout, 2,3, 0,1, GTK_FILL, (GtkAttachOptions)0, 4, 7);
#endif

    stwidgets.fullscreencheck = gtk_check_button_new_with_mnemonic("_Fullscreen");
    gtk_box_pack_start(GTK_BOX(stwidgets.displayvlayout), stwidgets.fullscreencheck, FALSE, FALSE, 0);

#ifdef POLYMEROPTION
    // Polymer checkbox
    stwidgets.polymercheck = gtk_check_button_new_with_mnemonic("_Polymer");
    gtk_box_pack_start(GTK_BOX(stwidgets.displayvlayout), stwidgets.polymercheck, FALSE, FALSE, 0);
#endif

    // Input devices LabelText
    stwidgets.inputdevlabel = gtk_label_new_with_mnemonic("_Input devices:");
    gtk_misc_set_alignment(GTK_MISC(stwidgets.inputdevlabel), 0.3, 0);
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.inputdevlabel, 0,1, 1,2, GTK_FILL, (GtkAttachOptions)0, 4, 0);

    // Input devices combo
    {
        GtkListStore *list = gtk_list_store_new(1, G_TYPE_STRING);
        GtkCellRenderer *cell;

        stwidgets.inputdevcombo = gtk_combo_box_new_with_model(GTK_TREE_MODEL(list));
        g_object_unref(G_OBJECT(list));

        cell = gtk_cell_renderer_text_new();
        gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(stwidgets.inputdevcombo), cell, FALSE);
        gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(stwidgets.inputdevcombo), cell, "text", 0, nullptr);
    }
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.inputdevcombo, 1,2, 1,2,
        (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), (GtkAttachOptions)0, 4, 0);

    // Empty horizontal layout
    stwidgets.emptyhlayout = gtk_hbox_new(TRUE, 0);
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.emptyhlayout, 0,3, 3,4, (GtkAttachOptions)0,
        (GtkAttachOptions)(GTK_EXPAND | GTK_FILL), 4, 0);

    // Always show config checkbox
    stwidgets.alwaysshowcheck = gtk_check_button_new_with_mnemonic("_Always show this window at startup");
    gtk_table_attach(GTK_TABLE(stwidgets.configtlayout), stwidgets.alwaysshowcheck, 0,3, 5,6, GTK_FILL, (GtkAttachOptions)0, 2, 2);

    // Configuration tab
    stwidgets.configtab = gtk_label_new("Configuration");
    gtk_notebook_set_tab_label(GTK_NOTEBOOK(stwidgets.tabs), gtk_notebook_get_nth_page(GTK_NOTEBOOK(stwidgets.tabs), 0), stwidgets.configtab);

    // Game data layout
    stwidgets.gamevlayout = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(stwidgets.tabs), stwidgets.gamevlayout);
    gtk_container_set_border_width(GTK_CONTAINER(stwidgets.gamevlayout), 4);

    // Game data field LabelText
    stwidgets.gamelabel = gtk_label_new_with_mnemonic("_Game:");
    gtk_box_pack_start(GTK_BOX(stwidgets.gamevlayout), stwidgets.gamelabel, FALSE, FALSE, 0);
    gtk_misc_set_alignment(GTK_MISC(stwidgets.gamelabel), 0, 0.5);

    // Game data scrollable area
    stwidgets.gamescroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_box_pack_start(GTK_BOX(stwidgets.gamevlayout), stwidgets.gamescroll, TRUE, TRUE, 0);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(stwidgets.gamescroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(stwidgets.gamescroll), GTK_SHADOW_IN);

    // Game data list
    {
        GtkListStore *list = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_POINTER);
        GtkCellRenderer *cell;
        GtkTreeViewColumn *col;

        gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(list), 0, name_sorter, NULL, NULL);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(list), 0, GTK_SORT_ASCENDING);

        stwidgets.gamelist = gtk_tree_view_new_with_model(GTK_TREE_MODEL(list));
        g_object_unref(G_OBJECT(list));

        cell = gtk_cell_renderer_text_new();
        col = gtk_tree_view_column_new_with_attributes("Game", cell, "text", 0, nullptr);
        gtk_tree_view_column_set_expand(col, TRUE);
        gtk_tree_view_append_column(GTK_TREE_VIEW(stwidgets.gamelist), col);
        col = gtk_tree_view_column_new_with_attributes("GRP file", cell, "text", 1, nullptr);
        gtk_tree_view_column_set_min_width(col, 64);
        gtk_tree_view_append_column(GTK_TREE_VIEW(stwidgets.gamelist), col);
    }
    gtk_container_add(GTK_CONTAINER(stwidgets.gamescroll), stwidgets.gamelist);

    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(stwidgets.gamelist), FALSE);
    gtk_tree_view_set_enable_search(GTK_TREE_VIEW(stwidgets.gamelist), FALSE);

    // Game tab
    stwidgets.gametab = gtk_label_new("Game");
    gtk_notebook_set_tab_label(GTK_NOTEBOOK(stwidgets.tabs), gtk_notebook_get_nth_page(GTK_NOTEBOOK(stwidgets.tabs), 1), stwidgets.gametab);

    // Messages scrollable area
    stwidgets.messagesscroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(stwidgets.tabs), stwidgets.messagesscroll);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(stwidgets.messagesscroll), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

    // Messages text area
    stwidgets.messagestext = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(stwidgets.messagesscroll), stwidgets.messagestext);
    gtk_text_view_set_editable(GTK_TEXT_VIEW(stwidgets.messagestext), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(stwidgets.messagestext), GTK_WRAP_WORD);
    gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(stwidgets.messagestext), FALSE);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(stwidgets.messagestext), 2);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(stwidgets.messagestext), 2);

    // Messages tab
    stwidgets.messagestab = gtk_label_new("Messages");
    gtk_notebook_set_tab_label(GTK_NOTEBOOK(stwidgets.tabs), gtk_notebook_get_nth_page(GTK_NOTEBOOK(stwidgets.tabs), 2), stwidgets.messagestab);

    // Dialogue box buttons layout
    stwidgets.buttons = gtk_hbutton_box_new();
    gtk_box_pack_start(GTK_BOX(stwidgets.vlayout), stwidgets.buttons, FALSE, TRUE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(stwidgets.buttons), 3);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(stwidgets.buttons), GTK_BUTTONBOX_END);

    // Cancel button
    stwidgets.cancelbutton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(stwidgets.buttons), stwidgets.cancelbutton);
    GTK_WIDGET_SET_FLAGS(stwidgets.cancelbutton, GTK_CAN_DEFAULT);

    stwidgets.cancelbuttonalign = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(stwidgets.cancelbutton), stwidgets.cancelbuttonalign);

    stwidgets.cancelbuttonlayout = gtk_hbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(stwidgets.cancelbuttonalign), stwidgets.cancelbuttonlayout);

    stwidgets.cancelbuttonicon = gtk_image_new_from_stock("gtk-cancel", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(stwidgets.cancelbuttonlayout), stwidgets.cancelbuttonicon, FALSE, FALSE, 0);

    stwidgets.cancelbuttonlabel = gtk_label_new_with_mnemonic("_Cancel");
    gtk_box_pack_start(GTK_BOX(stwidgets.cancelbuttonlayout), stwidgets.cancelbuttonlabel, FALSE, FALSE, 0);

    // Start button
    stwidgets.startbutton = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(stwidgets.buttons), stwidgets.startbutton);
    GTK_WIDGET_SET_FLAGS(stwidgets.startbutton, GTK_CAN_DEFAULT);

    gtk_window_set_default(GTK_WINDOW(stwidgets.startwin), stwidgets.startbutton);

    stwidgets.startbuttonalign = gtk_alignment_new(0.5, 0.5, 0, 0);
    gtk_container_add(GTK_CONTAINER(stwidgets.startbutton), stwidgets.startbuttonalign);

    stwidgets.startbuttonlayout = gtk_hbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(stwidgets.startbuttonalign), stwidgets.startbuttonlayout);

    stwidgets.startbuttonicon = gtk_image_new_from_stock("gtk-execute", GTK_ICON_SIZE_BUTTON);
    gtk_box_pack_start(GTK_BOX(stwidgets.startbuttonlayout), stwidgets.startbuttonicon, FALSE, FALSE, 0);

    stwidgets.startbuttonlabel = gtk_label_new_with_mnemonic("_Start");
    gtk_box_pack_start(GTK_BOX(stwidgets.startbuttonlayout), stwidgets.startbuttonlabel, FALSE, FALSE, 0);

    // Wire up the signals
    g_signal_connect((gpointer) stwidgets.startwin, "delete_event",
                     G_CALLBACK(on_startwin_delete_event),
                     NULL);
    g_signal_connect((gpointer) stwidgets.vmode3dcombo, "changed",
                     G_CALLBACK(on_vmode3dcombo_changed),
                     NULL);
    g_signal_connect((gpointer) stwidgets.fullscreencheck, "toggled",
                     G_CALLBACK(on_fullscreencheck_toggled),
                     NULL);
#ifdef POLYMEROPTION
    g_signal_connect((gpointer) stwidgets.polymercheck, "toggled",
                     G_CALLBACK(on_polymercheck_toggled),
                     NULL);
#endif
    g_signal_connect((gpointer) stwidgets.inputdevcombo, "changed",
                     G_CALLBACK(on_inputdevcombo_changed),
                     NULL);
    g_signal_connect((gpointer) stwidgets.alwaysshowcheck, "toggled",
                     G_CALLBACK(on_alwaysshowcheck_toggled),
                     NULL);
    g_signal_connect((gpointer) stwidgets.cancelbutton, "clicked",
                     G_CALLBACK(on_cancelbutton_clicked),
                     NULL);
    g_signal_connect((gpointer) stwidgets.startbutton, "clicked",
                     G_CALLBACK(on_startbutton_clicked),
                     NULL);
    {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(stwidgets.gamelist));
        gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);
        g_signal_connect((gpointer) sel, "changed",
                         G_CALLBACK(on_gamelist_selection_changed),
                         NULL);
    }

    // Associate labels with their controls
    gtk_label_set_mnemonic_widget(GTK_LABEL(stwidgets.vmode3dlabel), stwidgets.vmode3dcombo);
    gtk_label_set_mnemonic_widget(GTK_LABEL(stwidgets.inputdevlabel), stwidgets.inputdevcombo);
    gtk_label_set_mnemonic_widget(GTK_LABEL(stwidgets.gamelabel), stwidgets.gamelist);

    return stwidgets.startwin;
}


// -- BUILD ENTRY POINTS ------------------------------------------------------

int32_t startwin_open(void)
{
    if (!gtkenabled) return 0;
    if (stwidgets.startwin) return 1;

    stwidgets.startwin = create_window();
    if (stwidgets.startwin)
    {
        SetPage(TAB_MESSAGES);
        gtk_widget_show_all(stwidgets.startwin);
        gtk_main_iteration_do(FALSE);
        return 0;
    }
    return -1;
}

int32_t startwin_close(void)
{
    if (!gtkenabled) return 0;
    if (!stwidgets.startwin) return 1;
    gtk_widget_destroy(stwidgets.startwin);
    stwidgets.startwin = NULL;
    return 0;
}

int32_t startwin_puts(const char *str)
{
    GtkWidget *textview;
    GtkTextBuffer *textbuffer;
    GtkTextIter enditer;
    GtkTextMark *mark;
    const char *aptr, *bptr;

    if (!gtkenabled || !str) return 0;
    if (!stwidgets.startwin) return 1;
    if (!(textview = stwidgets.messagestext)) return -1;
    textbuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(textview));

    gtk_text_buffer_get_end_iter(textbuffer, &enditer);
    for (aptr = bptr = str; *aptr != 0;)
    {
        switch (*bptr)
        {
        case '\b':
            if (bptr > aptr)
                gtk_text_buffer_insert(textbuffer, &enditer, (const gchar *)aptr, (gint)(bptr-aptr)-1);
#if GTK_CHECK_VERSION(2,6,0)
            gtk_text_buffer_backspace(textbuffer, &enditer, FALSE, TRUE);
#else
            {
                GtkTextIter iter2 = enditer;
                gtk_text_iter_backward_cursor_position(&iter2);
                //FIXME: this seems be deleting one too many chars somewhere!
                if (!gtk_text_iter_equal(&iter2, &enditer))
                    gtk_text_buffer_delete_interactive(textbuffer, &iter2, &enditer, TRUE);
            }
#endif
            aptr = ++bptr;
            break;
        case 0:
            if (bptr > aptr)
                gtk_text_buffer_insert(textbuffer, &enditer, (const gchar *)aptr, (gint)(bptr-aptr));
            aptr = bptr;
            break;
        case '\r':	// FIXME
        default:
            bptr++;
            break;
        }
    }

    mark = gtk_text_buffer_create_mark(textbuffer, NULL, &enditer, 1);
    gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(textview), mark, 0.0, FALSE, 0.0, 1.0);
    gtk_text_buffer_delete_mark(textbuffer, mark);

    return 0;
}

int32_t startwin_settitle(const char *title)
{
    if (!gtkenabled) return 0;
    if (!stwidgets.startwin) return 1;
    gtk_window_set_title(GTK_WINDOW(stwidgets.startwin), title);
    return 0;
}

int32_t startwin_idle(void *s)
{
    UNREFERENCED_PARAMETER(s);
    if (!gtkenabled) return 0;
    //if (!stwidgets.startwin) return 1;
    gtk_main_iteration_do(FALSE);
    return 0;
}

int32_t startwin_run(void)
{
    if (!gtkenabled) return 1;
    if (!stwidgets.startwin) return 1;

    SetPage(TAB_CONFIG);

    settings.shared = ud_setup;
    settings.grp = g_selectedGrp;
#ifdef POLYMEROPTION
    settings.polymer = (glrendmode == REND_POLYMER) & (settings.shared.bpp != 8);
#endif
    PopulateForm(ALL);

    gtk_main();

    SetPage(TAB_MESSAGES);
    if (retval) // launch the game with these parameters
    {
        ud_setup = settings.shared;
#ifdef POLYMEROPTION
        glrendmode = (settings.polymer) ? REND_POLYMER : REND_POLYMOST;
#endif
        g_selectedGrp = settings.grp;

    }

    return retval;
}
