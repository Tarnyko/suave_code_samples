/*
* motif-editor.c
* Copyright (C) 2025  Manuel Bachmann <tarnyko.tarnyko.net>
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

 /*  Compile with:
  * gcc ... -lXm -lXt -lX11
  *  or to get HTML help:
  * gcc ... -DXMHTML -lXm -lXt -lX11 libXmHTML.a -ljpeg -lXpm
  *
  *  Run with:
  * xrdb -merge Xresources
  * ./a.out
  */

#include <Xm/Xm.h>
#include <Xm/MainW.h>          /* XmMainWindow */
#include <Xm/DialogS.h>        /* XmDialogShell */
#include <Xm/RowColumn.h>      /* XmCreateMenuBar */
#include <Xm/CascadeB.h>       /* XmCascadeButton */
#include <Xm/PushBG.h>         /* XmPushButtonGadget */
#include <Xm/ToggleBG.h>       /* XmToggleButtonGadget */
#include <Xm/SeparatoG.h>      /* XmSeparatorGadget */
#include <Xm/Frame.h>          /* XmFrame */
#include <Xm/Form.h>           /* XmForm */
#include <Xm/Label.h>          /* XmLabel */
#include <Xm/Text.h>           /* XmScrolledText ... */
#include <Xm/List.h>           /* XmCreateScrolledList ... */
#include <Xm/ComboBox.h>       /* XmCreateDropDownComboBox ... */
#include <Xm/PushB.h>          /* XmPushButton */
#include <Xm/MessageB.h>       /* XmInformationDialog ... */ 
#include <Xm/FileSB.h>         /* XmFileSelectionDialog */
#ifdef XMHTML
#  include <XmHTML/XmHTML.h>   /* xmHTML */
#endif

#include <X11/bitmaps/plaid>   /* main window icon */

#include <stdio.h>             /* printf() */
#include <stdlib.h>            /* exit(), rand() */
#include <time.h>              /* time() */
#include <sys/stat.h>          /* stat() */


 /* GLOBAL VARIABLES */
Widget text, messagebar;
Pixel whitebg;
XColor graybg;
XColor bluebg;
char *fontname = "fixed"; /* current font */
char **fontlist;          /* font list, gets filled then freed */


 /* RIGHT-CLICK POPUP ON TEXT: */
 /* 1) callbacks */
void on_cut(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    XmTextCut(text, CurrentTime);
}
void on_copy(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    XmTextCopy(text, CurrentTime);
}
void on_paste(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    XmTextPaste(text);
}
 /* 2) widget */
void on_popup(Widget widget, void *data, XEvent *event)
{
    XButtonEvent *mbutton = (XButtonEvent *)event;

    if (event->type == ButtonPress)
    {
        if (mbutton->button == Button3)  /* mouse right-click */
        {
            Widget popup_menu, option_cut, option_copy, option_paste;
            Arg args[1];

            popup_menu = XmCreateSimplePopupMenu(text, "edit menu", NULL, 0);
            /*XtManageChild(popup_menu);*/

            XtSetArg(args[0], XmNacceleratorText, XmStringCreateLocalized("Ctrl+X"));
            option_cut = XmCreatePushButtonGadget(popup_menu, "Cut", args, 1);
            XtManageChild(option_cut);
	    
            XtSetArg(args[0], XmNacceleratorText, XmStringCreateLocalized("Ctrl+C"));
            option_copy = XmCreatePushButtonGadget(popup_menu, "Copy", args, 1);
            XtManageChild(option_copy);

            XtSetArg(args[0], XmNacceleratorText, XmStringCreateLocalized("Ctrl+V"));
            option_paste = XmCreatePushButtonGadget(popup_menu, "Paste", args, 1);
            XtManageChild(option_paste);

            /* position the menu where the XEvent has been generated */
            XmMenuPosition(popup_menu, (XButtonPressedEvent*)event);
            /* only show the menu after the subobtions, otherwise it'll appear empty */
            XtManageChild(popup_menu);

            /* setup callbacks */
            XtAddCallback(option_cut,   XmNactivateCallback, on_cut, NULL);
            XtAddCallback(option_copy,  XmNactivateCallback, on_copy, NULL);
            XtAddCallback(option_paste, XmNactivateCallback, on_paste, NULL);
        }
    }
}

 /* FONT SELECTION WINDOW: */
 /* 1) callbacks */
void on_font_changed(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    Widget window = (Widget)client_data; /* for s.reason, XtDisplay() doesn't work on 'widget' */

    XFontStruct *font = XLoadQueryFont(XtDisplay(window), fontname);
    if (font != NULL)
    {
        /* change the current font of XmScrolledText */
        XmFontList mfontlist = XmFontListCreate(font, (XmStringCharSet)XmSTRING_DEFAULT_CHARSET);
        XtVaSetValues(text, XmNfontList, mfontlist, NULL);

        XFreeFontNames(fontlist);
    }
}
void on_prefs_styleselect(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    int style = (int)client_data;
    printf("SELECTED : %d\n", style);
}
void on_prefs_sizeselect(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    XmComboBoxCallbackStruct *cbs = (XmComboBoxCallbackStruct*)callback_data;
    printf("SELECTED: %d\n", cbs->item_position);
}
void on_prefs_fontselect(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    Widget bt_ok = (Widget)client_data;

    /* reactivates the "Ok" button */
    XtVaSetValues(bt_ok, XmNsensitive, TRUE, NULL);

    /* gets selected element; WARNING, first element of XmList is 1, not 0 !*/
    int *selected, selcount;
    XmListGetSelectedPos(widget, &selected, &selcount);
    fontname = fontlist[selected[0]-1]; /* GLOBAL font change */

    printf("SELECTED: %d - %s\n", selected[0], fontlist[selected[0]-1]);
}
 /* 2) widget */
void on_prefs(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    Widget window = (Widget)client_data;
    int count = 0;

    /* retrieve the 50 first fonts available */
    fontlist = XListFonts(XtDisplay(window), "*", 50, &count);
    if (count != 0)
    {
        Widget prefs_dialog, box;
        Arg args[2];

        XtSetArg(args[0], XmNtitle, "Preferences");
        XtSetArg(args[1], XmNallowShellResize, FALSE);
        prefs_dialog = XmCreateDialogShell(window, "Preferences", args, 2);

        XtSetArg(args[0], XmNdialogType, XmDIALOG_MESSAGE);
        XtSetArg(args[1], XmNheight, 420); /* if too small, XmList will be sad*/
        box = XmCreateMessageBox(prefs_dialog, "", args, 2);

        /* we hide the useless "Help" button */
        Widget bt_help;
        bt_help = XmMessageBoxGetChild(box, XmDIALOG_HELP_BUTTON);
        XtUnmanageChild(bt_help);

        /* we deactivate the "Ok" button */
        Widget bt_ok;
        bt_ok = XmMessageBoxGetChild(box, XmDIALOG_OK_BUTTON);
        XtVaSetValues(bt_ok, XmNsensitive, FALSE, NULL);

        /* we add a title XmFrame... */
        Widget frame, label;
        frame = XmCreateFrame(box, "frame", NULL, 0);
        XtSetArg(args[0], XmNchildType, XmFRAME_TITLE_CHILD);/* ...with this label as title */
        label = XmCreateLabel(frame, "Font selection", args, 1);

         /* all in a vertical (automatic) XmRowColumn layout */
        Widget column;
        XtSetArg(args[0], XmNisHomogeneous, FALSE); /* XmList needs space */
        column = XmCreateRowColumn(frame, "column", args, 1);
         /* horizontal XmRowColumn for the 2 first widgets */
        Widget hcolumn;
        XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
        hcolumn = XmCreateRowColumn(column, "hcolumn", args, 1);
         /* */
         /* 1) XmRadioBox with several... */
        Widget radio, toggle_normal, toggle_bold, toggle_italic;
        XtSetArg(args[0], XmNorientation, XmHORIZONTAL);
        radio = XmCreateRadioBox(hcolumn, "radio", args, 1);
         /* ... XmToggleButtonGadgets (round buttons are prettier) */
        XtSetArg (args[0], XmNindicatorType, XmONE_OF_MANY_ROUND);
        toggle_normal = XmCreateToggleButtonGadget(radio, "Normal", args, 1);
        toggle_bold   = XmCreateToggleButtonGadget(radio, "Bold", args, 1);
        toggle_italic = XmCreateToggleButtonGadget(radio, "Italic", args, 1);
         /* 2) XmComboBox with several... */
        Widget combo;
        XtSetArg (args[0], XmNvisibleItemCount, 3); /* or ComboBox will be too tall*/
        combo = XmCreateDropDownComboBox (hcolumn, "combo", args, 1);
         /* ... XmComboItems */
        XmComboBoxAddItem(combo, XmStringCreateLocalized("10"), 1, TRUE);
        XmComboBoxAddItem(combo, XmStringCreateLocalized("16"), 2, TRUE);
        XmComboBoxAddItem(combo, XmStringCreateLocalized("24"), 3, TRUE);

        /* retrieves the ComboBox' child TextField to change its color */
        Widget combotext;
        XtSetArg(args[0], XmNtextField, &combotext);
        XtGetValues(combo, args, 1);
        XmChangeColor(combotext, whitebg);

        /* add a XmScrolledList */
        Widget list;
        XtSetArg(args[0], XmNvisibleItemCount, 10);
        list = XmCreateScrolledList(column, "fontlist", args, 1);
        XmChangeColor(list, whitebg);
        /* iterate on our fontlist to fill in the XmScrolledList */
        for (count = 0; count < 50; count++) {
            XmListAddItem(list, XmStringCreateLocalized(fontlist[count]), count+1); }

        XtManageChild(frame);
        XtManageChild(label);
        XtManageChild(column);
        XtManageChild(hcolumn);
        XtManageChild(radio);
        XtManageChild(toggle_normal);
        XtManageChild(toggle_bold);
        XtManageChild(toggle_italic);
        XtManageChild(combo);
        XtManageChild(list);
        XtManageChild(box);
        XtManageChild(prefs_dialog);

        /* setup callbacks */
        XtAddCallback(toggle_normal, XmNvalueChangedCallback, on_prefs_styleselect, (XtPointer)1);
        XtAddCallback(toggle_bold, XmNvalueChangedCallback, on_prefs_styleselect, (XtPointer)2);
        XtAddCallback(toggle_italic, XmNvalueChangedCallback, on_prefs_styleselect, (XtPointer)3);
        XtAddCallback(combo, XmNselectionCallback, on_prefs_sizeselect, NULL);
        XtAddCallback(list, XmNbrowseSelectionCallback, on_prefs_fontselect, bt_ok);
        XtAddCallback(bt_ok, XmNactivateCallback, on_font_changed, window);
    }
}

 /* STATUS BAR: callback (set background to white = modifed) */
void on_text_changed(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    XtVaSetValues(messagebar, XmNlabelString, XmStringCreateLocalized("Text modified"), NULL);
    XmChangeColor(messagebar, whitebg);
}

 /* FILE OPEN WINDOW: */
 /* 1) callbacks */
void on_open_ok(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    XmFileSelectionBoxCallbackStruct *cbs = (XmFileSelectionBoxCallbackStruct *)callback_data;
    char *filename, *string;

    if (XmStringGetLtoR(cbs->value, XmFONTLIST_DEFAULT_TAG, &filename))
    {
        printf("File selected : %s\n", filename);

        /* send text file content to widget */
        FILE *f;
        if (f = fopen(filename, "r"))
        {
            /* obtain file size, allocate same size, fread() to it */
            struct stat statb;
            stat(filename, &statb);

            string = XtMalloc((unsigned)statb.st_size + 1);
            fread(string, sizeof(char), statb.st_size + 1, f);
            fclose(f);

            XmTextSetString(text, string);

            /* reset statusbar background to gray (= unmodified) */
            XtVaSetValues(messagebar, XmNlabelString, XmStringCreateLocalized("File opened"), NULL);
            XmChangeColor(messagebar, graybg.pixel);

            XtUnmanageChild(widget);
            XtFree(string);
        }

        XtFree(filename);
    }
}
void on_open_cancel(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    XtUnmanageChild(widget);
}
 /* 2) widget */
void on_open(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    Widget window = (Widget)client_data; /* parent of the dialog MUST be the window */
    Widget fileopen_dialog;
    Arg args[4];

    XtSetArg(args[0], XmNdialogTitle, XmStringCreateLocalized("Open file..."));
    XtSetArg(args[1], XmNdialogStyle, XmDIALOG_APPLICATION_MODAL);
    fileopen_dialog = XmCreateFileSelectionDialog(window, "Open file...", args, 2);
    XtManageChild(fileopen_dialog);

    /* we hide the useless "Help" button */
    Widget bt_help;
    bt_help = XmFileSelectionBoxGetChild (fileopen_dialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild (bt_help);

    /* we set the background of most text fields to white */
    Widget dirlist, filelist, filtertext, filetext;
    dirlist    = XmFileSelectionBoxGetChild(fileopen_dialog, XmDIALOG_DIR_LIST);
    filelist   = XmFileSelectionBoxGetChild(fileopen_dialog, XmDIALOG_LIST);
    filtertext = XmFileSelectionBoxGetChild(fileopen_dialog, XmDIALOG_FILTER_TEXT);
    filetext   = XmFileSelectionBoxGetChild(fileopen_dialog, XmDIALOG_TEXT);
    XmChangeColor(dirlist, whitebg);
    XmChangeColor(filelist, whitebg);
    /*XmChangeColor(filtertext, whitebg);*/
    XmChangeColor(filetext, whitebg);

    /* setup callbacks */
    XtAddCallback(fileopen_dialog, XmNcancelCallback, on_open_cancel, NULL);
    XtAddCallback(fileopen_dialog, XmNokCallback, on_open_ok, NULL);
}

 /* FILE SAVE WINDOW: widget [TODO: stub] */
void on_save(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    Widget window = (Widget)client_data;
    Widget warning_dialog;
    Arg args[4];

    XtSetArg(args[0], XmNmessageString, XmStringCreateLocalized("Not implemented yet..."));
    XtSetArg(args[1], XmNdialogTitle, XmStringCreateLocalized("Not implemented"));
    XtSetArg(args[2], XmNnoResize, TRUE);
    XtSetArg(args[3], XmNdialogStyle, XmDIALOG_APPLICATION_MODAL);
    warning_dialog = XmCreateWarningDialog(window, "Not implemented", args, 4);
    XtManageChild(warning_dialog);

    /* we hide the useless "Cancel" & "Help" buttons */
    Widget bt_cancel, bt_help;
    bt_cancel = XmMessageBoxGetChild(warning_dialog, XmDIALOG_CANCEL_BUTTON);
    bt_help   = XmMessageBoxGetChild(warning_dialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(bt_cancel);
    XtUnmanageChild(bt_help);
}

 /* MAIN QUIT: callback */
void on_quit(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    printf("Quit button pressed !\n");
    exit(0);
}

 /* MENU2: callback */
void on_showmenu2(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    Widget menu2 = (Widget)client_data;

    int state = XmToggleButtonGetState(widget); /* checked or not */
    if (state) {
        XtManageChild(menu2);
    } else {
	XtUnmanageChild (menu2);
    }
}

 /* HELP DIALOG: widget */
void on_help(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    Widget window = (Widget)client_data;
    Widget help_dialog, box;
    Arg args[2];

    XtSetArg(args[0], XmNtitle, "Help");
    XtSetArg(args[1], XmNallowShellResize, TRUE);
    help_dialog = XmCreateDialogShell(window, "Help", args, 2);

    XtSetArg(args[0], XmNdialogType, XmDIALOG_MESSAGE);
    box = XmCreateMessageBox(help_dialog, "", args, 1);

    /* we hide the useless "Help" button */
    Widget bt_help;
    bt_help = XmMessageBoxGetChild(box, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(bt_help);

# ifdef XMHTML
    Widget html;
    html = XtVaCreateManagedWidget("html",
                                   xmHTMLWidgetClass, box,
                                   XmNwidth, 200, XmNheight, 100, NULL);
    XmHTMLTextSetString(html, "<html><body>Test</body></html>");
    XtManageChild(html);
# else
    Widget label;
    label = XmCreateLabel(box, "Cannot open help : not compiled with xmHTML support !", NULL, 0);
    XtManageChild (label);
#endif

    XtManageChild(box);
    XtManageChild(help_dialog);
}

 /* ABOUT DIALOG: widget */
void on_about(Widget widget, XtPointer client_data, XtPointer callback_data)
{
    Widget window = (Widget)client_data;
    Widget about_dialog;
    Arg args[4];

    XtSetArg(args[0], XmNmessageString, XmStringCreateLocalized("@2001 Tarnyko"));
    XtSetArg(args[1], XmNdialogTitle, XmStringCreateLocalized("About..."));
    XtSetArg(args[2], XmNnoResize, TRUE);
    XtSetArg(args[3], XmNdialogStyle, XmDIALOG_APPLICATION_MODAL);
    about_dialog = XmCreateInformationDialog(window, "About...", args, 4);
    XtManageChild(about_dialog);

    /* we hide the useless "Cancel" & "Help" */
    Widget bt_cancel, bt_help;
    bt_cancel = XmMessageBoxGetChild(about_dialog, XmDIALOG_CANCEL_BUTTON);
    bt_help   = XmMessageBoxGetChild(about_dialog, XmDIALOG_HELP_BUTTON);
    XtUnmanageChild(bt_cancel);
    XtUnmanageChild(bt_help);
}


int main (int argc, char *argv[])
{
    Widget window, bg, menu, menu2, frame, form, label,/* text,*/ button/*, messagebar*/;
    Widget menu_file, menu_edit, menu_view, menu_help;
    Widget pmenu_file, pmenu_edit, pmenu_view, pmenu_help;
    Widget option_open, option_save, option_sep, option_quit, option_cut, option_copy, option_paste, option_sep2, option_prefs, option_showmenu2, option_help, option_sep3, option_about;
    Widget button_open, button_save, button_cut, button_copy, button_paste;
    XtAppContext app;
    Arg args[11];

    /* window title */
    window = XtVaAppInitialize(&app, argv[0], NULL, 0, &argc, argv, NULL, NULL);
    XtVaSetValues(window, XmNtitle, argv[0], NULL);

    /* window icon */
    Pixmap icon_window;
    icon_window = XCreatePixmapFromBitmapData(XtDisplay(window), RootWindowOfScreen (XtScreen(window)), plaid_bits, plaid_width, plaid_height, 1, 0, 1);
    XtVaSetValues(window, XmNiconPixmap, icon_window, NULL);

    /* menu icons */
    Pixel fgc, bgc;
    Pixmap icon_open, icon_save, icon_cut, icon_copy, icon_paste;
    XtVaGetValues(window, XmNforeground, &fgc, XmNbackground, &bgc, NULL);
    icon_open  = XmGetPixmap(XtScreen(window), "icons/icon_open.xbm", fgc, bgc);
    icon_save  = XmGetPixmap(XtScreen(window), "icons/icon_save.xbm", fgc, bgc);
    icon_cut   = XmGetPixmap(XtScreen(window), "icons/icon_cut.xbm", fgc, bgc);
    icon_copy  = XmGetPixmap(XtScreen(window), "icons/icon_copy.xbm", fgc, bgc);
    icon_paste = XmGetPixmap(XtScreen(window), "icons/icon_paste.xbm", fgc, bgc);

    /* window position (randomized) */
    Screen *screen;
    Dimension width, height;
    Position x, y;
     /* */
    screen = XtScreen(window);
    width  = WidthOfScreen(screen);
    height = HeightOfScreen(screen);
    printf("SCREEN DIMENSIONS: %dx%d\n", height, width);
     /* */
    srand(time(NULL));
    x = rand() % width ;
    y = rand() % height ;
    printf ("CHOSEN POSITION: %dx%d\n", x, y);
     /* */
    XtVaSetValues(window, XmNx, x, XmNy, y, NULL);

    /* reusable colors init (white, gray, blue) */
    Display *display;
    Colormap cmap;
    XColor ignore;
     /* */
    display = XtDisplay (window);
    cmap = DefaultColormap (display, 0);
     /* */
    whitebg = WhitePixelOfScreen(screen);
    char *colorname = "light gray";
    XAllocNamedColor(display, cmap, colorname, &graybg, &ignore);
    char *colorname2 = "blue";
    XAllocNamedColor(display, cmap, colorname2, &bluebg, &ignore);

    /* window dimensions */
    bg = XtVaCreateManagedWidget("bg", xmMainWindowWidgetClass, window,
                                  XmNwidth, 512, XmNheight, 384, NULL);

    /* WINDOW LAYOUT */

    /* --- MENU LEVEL 0--- (use "Alt+M" instead of default "F10") */
    XtSetArg(args[0], XmNmenuAccelerator, "Alt<Key>M"); 
    menu = XmCreateMenuBar(bg, "main menu", args, 1);
    XtManageChild(menu);
     /* -- FILE -- */
    pmenu_file = XmCreateSimplePulldownMenu(menu, "file menu", NULL, 0);
    XtManageChild(pmenu_file);
     /**/
    XtSetArg(args[0], XmNsubMenuId, pmenu_file);
    XtSetArg(args[1], XmNmnemonic, 'F');
    menu_file = XmCreateCascadeButton(menu, "File", args, 2);
    XtManageChild(menu_file);
     /* -- EDIT -- */
    pmenu_edit = XmCreateSimplePulldownMenu(menu, "edit menu", NULL, 0);
    XtManageChild(pmenu_edit);
     /**/
    XtSetArg(args[0], XmNsubMenuId, pmenu_edit);
    XtSetArg(args[1], XmNmnemonic, 'E');
    menu_edit = XmCreateCascadeButton(menu, "Edit", args, 2);
    XtManageChild(menu_edit);
     /* -- VIEW -- */
    pmenu_view = XmCreateSimplePulldownMenu(menu, "view menu", NULL, 0);
    XtManageChild(pmenu_view);
     /* */
    XtSetArg(args[0], XmNsubMenuId, pmenu_view);
    XtSetArg(args[1], XmNmnemonic, 'V');
    menu_view = XmCreateCascadeButton(menu, "View", args, 2);
    XtManageChild(menu_view);
     /* -- ? -- */
    pmenu_help = XmCreateSimplePulldownMenu(menu, "help menu", NULL, 0);
    XtManageChild(pmenu_help);
     /**/
    XtSetArg(args[0], XmNsubMenuId, pmenu_help);
    XtSetArg(args[1], XmNmnemonic, '?');
    menu_help = XmCreateCascadeButton(menu, "?", args, 2);
    XtManageChild(menu_help);

    /* --- MENU LEVEL 1--- */
     /* -- FILE -> OPEN -- */
    XtSetArg(args[0], XmNaccelerator, "Ctrl<Key>O"); /* no spaces */
    XtSetArg(args[1], XmNacceleratorText, XmStringCreateLocalized("Ctrl+O"));
    option_open = XmCreatePushButtonGadget(pmenu_file, "Open...", args, 2);
    XtManageChild(option_open);
     /* -- FILE -> SAVE -- */
    XtSetArg(args[0], XmNaccelerator, "Ctrl<Key>S");
    XtSetArg (args[1], XmNacceleratorText, XmStringCreateLocalized("Ctrl+S"));
    option_save = XmCreatePushButtonGadget(pmenu_file, "Save...", args, 2);
    XtManageChild(option_save);
     /* -- FILE -> <---> -- */
    option_sep = XmCreateSeparatorGadget(pmenu_file, "---", NULL, 0);
    XtManageChild(option_sep);
     /* -- FILE -> QUIT -- */
    option_quit = XmCreatePushButtonGadget(pmenu_file, "Quit", NULL, 0);
    XtManageChild(option_quit);
     /* -- EDIT -> CUT -- */
    XtSetArg(args[0], XmNaccelerator, "Ctrl<Key>X");
    XtSetArg(args[1], XmNacceleratorText, XmStringCreateLocalized("Ctrl+X"));
    option_cut = XmCreatePushButtonGadget(pmenu_edit, "Cut", args, 2);
    XtManageChild (option_cut);
     /* -- EDIT -> COPY -- */
    XtSetArg(args[0], XmNaccelerator, "Ctrl<Key>C");
    XtSetArg(args[1], XmNacceleratorText, XmStringCreateLocalized("Ctrl+C"));
    option_copy = XmCreatePushButtonGadget(pmenu_edit, "Copy", args, 2);
    XtManageChild(option_copy);
     /* -- EDIT -> PASTE -- */
    XtSetArg (args[0], XmNaccelerator, "Ctrl<Key>V");
    XtSetArg (args[1], XmNacceleratorText, XmStringCreateLocalized("Ctrl+V"));
    option_paste = XmCreatePushButtonGadget(pmenu_edit, "Paste", args, 2);
    XtManageChild(option_paste);
     /* -- EDIT -> <---> -- */
    option_sep2 = XmCreateSeparatorGadget(pmenu_edit, "---", NULL, 0);
    XtManageChild(option_sep2);
     /* -- EDIT -> PREFERENCES -- */
    option_prefs = XmCreatePushButtonGadget(pmenu_edit, "Preferences...", NULL, 0);
    XtManageChild(option_prefs);
     /* -- VIEW -> QUICK TOOLBAR -- */
    XtSetArg(args[0], XmNindicatorOn, XmINDICATOR_CHECK);
    option_showmenu2 = XmCreateToggleButtonGadget(pmenu_view, "Quick toolbar", args, 2);
    XtManageChild (option_showmenu2);
     /* -- ? -> HELP */
    XtSetArg(args[0], XmNaccelerator, "<Key>F1");
    XtSetArg(args[1], XmNacceleratorText, XmStringCreateLocalized("F1"));
    option_help = XmCreatePushButtonGadget(pmenu_help, "Help...", args, 2);
    XtManageChild(option_help);
     /* -- ? -> <---> */
    option_sep3 = XmCreateSeparatorGadget(pmenu_help, "---", NULL, 0);
    XtManageChild(option_sep3);
     /* -- ? -> ABOUT */
    option_about = XmCreatePushButtonGadget(pmenu_help, "About...", NULL, 0);
    XtManageChild(option_about);

    /* Hack! Puts the "Help" menu on the right side of the menu */
    XtVaSetValues (menu, XmNmenuHelpWidget, menu_help, NULL);


    /* --- FRAME --- */
    frame = XtVaCreateManagedWidget("frame", xmFrameWidgetClass, bg,
	                            XmNshadowType, XmSHADOW_IN,
                                    NULL);
    form = XmCreateForm (frame, "form", NULL, 0);
    XtManageChild (form);

    /* --- MENU 2 --- */
     /* we cannot set 2 menus as child of bg, so we add this one to form */
    XtSetArg(args[0], XmNtopAttachment, XmATTACH_FORM);	/* form edge */
    XtSetArg(args[1], XmNbottomAttachment, XmATTACH_POSITION);
    XtSetArg(args[2], XmNbottomPosition, 15);
    XtSetArg(args[3], XmNleftAttachment, XmATTACH_FORM);
    XtSetArg(args[4], XmNrightAttachment, XmATTACH_FORM);
    menu2 = XmCreateMenuBar(form, "quick toolbar menu", args, 5);
    XtManageChild(menu2);
     /* -- QUICK BUTTONS -- */
    int n; /* if pixmap exists, use it instead of text for button  */
     /* attach to menubar border */
    XtSetArg(args[0], XmNleftAttachment, XmATTACH_FORM);
    if (icon_open != XmUNSPECIFIED_PIXMAP) {
        XtSetArg(args[1], XmNlabelType, XmPIXMAP);
        XtSetArg(args[2], XmNlabelPixmap, icon_open);
        n = 3;
    } else {
        n = 1;
    }
    button_open = XmCreatePushButton(menu2, "Open", args, n);
    XtManageChild(button_open);
     /* attach to formet widget */
    XtSetArg(args[0], XmNleftAttachment, XmATTACH_WIDGET);
    if (icon_save != XmUNSPECIFIED_PIXMAP) {
        XtSetArg(args[1], XmNlabelType, XmPIXMAP);
        XtSetArg(args[2], XmNlabelPixmap, icon_save);
        n = 3;
    } else {
        n = 1;
    }
    button_save = XmCreatePushButton (menu2, "Save", args, n);
    XtManageChild (button_save);
     /* */
    XtSetArg(args[0], XmNleftAttachment, XmATTACH_WIDGET);
    if (icon_cut != XmUNSPECIFIED_PIXMAP) {
        XtSetArg(args[1], XmNlabelType, XmPIXMAP);
        XtSetArg(args[2], XmNlabelPixmap, icon_cut);
        n = 3;
    } else {
        n = 1;
    }
    button_cut = XmCreatePushButton(menu2, "Cut", args, n);
    XtManageChild(button_cut);
     /* */
    XtSetArg (args[0], XmNleftAttachment, XmATTACH_WIDGET);
    if (icon_copy != XmUNSPECIFIED_PIXMAP) {
        XtSetArg (args[1], XmNlabelType, XmPIXMAP);
        XtSetArg (args[2], XmNlabelPixmap, icon_copy);
        n = 3;
    } else {
        n = 1;
    }
    button_copy = XmCreatePushButton(menu2, "Copy", args, n);
    XtManageChild(button_copy);
     /* */
    XtSetArg(args[0], XmNleftAttachment, XmATTACH_WIDGET);
    if (icon_paste != XmUNSPECIFIED_PIXMAP) {
        XtSetArg(args[1], XmNlabelType, XmPIXMAP);
        XtSetArg(args[2], XmNlabelPixmap, icon_paste);
        n = 3;
    } else {
        n = 1;
    }
    button_paste = XmCreatePushButton(menu2, "Paste", args, n);
    XtManageChild(button_paste);

    /* --- OTHER: all coordinates are between 0 and 100 */

    /* --- INFO LABEL  ---*/
    XtSetArg(args[0], XmNtopAttachment, XmATTACH_POSITION);
    XtSetArg(args[1], XmNtopPosition, 15);
    XtSetArg(args[2], XmNbottomAttachment, XmATTACH_POSITION);
    XtSetArg(args[3], XmNbottomPosition, 25);
    XtSetArg(args[4], XmNleftAttachment, XmATTACH_POSITION);
    XtSetArg(args[5], XmNleftPosition, 10);
    XtSetArg(args[6], XmNrightAttachment, XmATTACH_POSITION);
    XtSetArg(args[7], XmNrightPosition, 90);
    label = XmCreateLabel(form, "This window positions itself randomly", args, 8);
    XtManageChild(label);

    /* --- TEXT EDITOR FIELD  ---*/
    XtSetArg(args[0], XmNtopAttachment, XmATTACH_POSITION);
    XtSetArg(args[1], XmNtopPosition, 25);
    XtSetArg(args[2], XmNbottomAttachment, XmATTACH_POSITION);
    XtSetArg(args[3], XmNbottomPosition, 87);
    XtSetArg(args[4], XmNleftAttachment, XmATTACH_POSITION);
    XtSetArg(args[5], XmNleftPosition, 5);
    XtSetArg(args[6], XmNrightAttachment, XmATTACH_POSITION);
    XtSetArg(args[7], XmNrightPosition, 95);
     /* by default, XmScrolledText is single-line with no vertical bar */
    XtSetArg(args[8], XmNeditMode, XmMULTI_LINE_EDIT);
     /* display the cursor as soon as we click in */
    XtSetArg(args[9], XmNhighlightOnEnter, TRUE);
    text = XmCreateScrolledText(form, "", args, 10);
    XmChangeColor(text, whitebg);
    XtManageChild(text);

    /* --- QUIT BUTTON  ---*/
    XtSetArg(args[0], XmNtopAttachment, XmATTACH_POSITION);
    XtSetArg(args[1], XmNtopPosition, 88);
    XtSetArg(args[2], XmNbottomAttachment, XmATTACH_POSITION);
    XtSetArg(args[3], XmNbottomPosition, 98);
    XtSetArg(args[4], XmNleftAttachment, XmATTACH_POSITION);
    XtSetArg(args[5], XmNleftPosition, 35);
    XtSetArg(args[6], XmNrightAttachment, XmATTACH_POSITION);
    XtSetArg(args[7], XmNrightPosition, 65);
    button = XmCreatePushButton(form, "Quit", args, 8);
    XtManageChild(button);

    /* --- STATUSBAR --- */
    XtSetArg(args[0], XmNalignment, XmALIGNMENT_BEGINNING); /* left-aligned */
    messagebar = XmCreateLabel(bg, "Program started", args, 1);
    XtManageChild(messagebar);
     /* tells the MainWindow to treat it as a Message area */
    XtVaSetValues(bg, XmNmessageWindow, messagebar, NULL);

    /* setup MAIN CALLBACKS */
    XtAddCallback(option_open,      XmNactivateCallback, on_open, window);
    XtAddCallback(option_save,      XmNactivateCallback, on_save, window);
    XtAddCallback(option_quit,      XmNactivateCallback, on_quit, NULL);
    XtAddCallback(option_cut,       XmNactivateCallback, on_cut, NULL);
    XtAddCallback(option_copy,      XmNactivateCallback, on_copy, NULL);
    XtAddCallback(option_paste,     XmNactivateCallback, on_paste, NULL);
    XtAddCallback(option_prefs,     XmNactivateCallback, on_prefs, window);
    XtAddCallback(option_showmenu2, XmNvalueChangedCallback, on_showmenu2, menu2);
    XtAddCallback(button_open,  XmNactivateCallback, on_open, window);
    XtAddCallback(button_save,  XmNactivateCallback, on_save, window);
    XtAddCallback(button_cut,   XmNactivateCallback, on_cut, NULL);
    XtAddCallback(button_copy,  XmNactivateCallback, on_copy, NULL);
    XtAddCallback(button_paste, XmNactivateCallback, on_paste, NULL);
    XtAddCallback(option_help,  XmNactivateCallback, on_help, window);
    XtAddCallback(option_about, XmNactivateCallback, on_about, window);
    XtAddCallback(text, XmNvalueChangedCallback, on_text_changed, NULL);
     /* right-click on XmText isn't mapped -> use a generic XtEventHandler */
    XtAddEventHandler(text, ButtonPressMask, FALSE, (XtEventHandler)on_popup, 0);
    XtAddCallback(button, XmNactivateCallback, on_quit, NULL);

    /* MAIN LOOP */
    XtRealizeWidget(window);
    XtAppMainLoop(app);

    return EXIT_SUCCESS;
}
