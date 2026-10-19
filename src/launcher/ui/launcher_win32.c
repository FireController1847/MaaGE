#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include "launcher.h"
#include "maage/launcher_version.h"
#include <string.h>
#define MAAGE_UI_ACCOUNT 110
#define MAAGE_UI_INSTALLATION 111
#define MAAGE_UI_LIST 112
#define MAAGE_UI_NAME 113
#define MAAGE_UI_MANAGE 120
#define MAAGE_UI_VERIFY 121
#define MAAGE_UI_REPAIR 122
#define MAAGE_UI_PROJECT 123
#define MAAGE_UI_STATUS 124
#define MAAGE_UI_NEW 130
#define MAAGE_UI_EDIT 131
#define MAAGE_UI_DUPLICATE 132
#define MAAGE_UI_REMOVE 133
#define MAAGE_UI_DEFAULT 134
typedef struct maage_launcher_control {
    HWND handle;
    int page, x, y, width, height;
} maage_launcher_control;
typedef struct maage_launcher_win32 {
    maage_launcher_ui *state;
    HWND window, navigation[4], account, installation, list, name, status, resource, account_status, verify, repair, error;
    HFONT font, heading, brand;
    HBRUSH background, sidebar, magenta;
    maage_launcher_control controls[72];
    unsigned count, revision;
    int result, updating;
} maage_launcher_win32;
static void maage_ui_text(HWND control, const char *text)
{
    wchar_t wide[MAAGE_PATH_CAPACITY];
    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, wide, MAAGE_PATH_CAPACITY)) SetWindowTextW(control, wide);
}
static HWND maage_ui_control(maage_launcher_win32 *ui, int page, const wchar_t *klass, const wchar_t *title, DWORD style, int x, int y, int width, int height, int id)
{
    maage_launcher_control *control = &ui->controls[ui->count++];
    control->page = page; control->x = x; control->y = y; control->width = width; control->height = height;
    control->handle = CreateWindowExW(!wcscmp(klass, L"EDIT") ? WS_EX_CLIENTEDGE : 0, klass, title, WS_CHILD | WS_VISIBLE | style,
        x,y,width,height,ui->window,(HMENU)(INT_PTR)id,GetModuleHandleW(NULL),NULL);
    SendMessageW(control->handle, WM_SETFONT, (WPARAM)ui->font, TRUE); return control->handle;
}
static HWND maage_ui_label(maage_launcher_win32 *ui, int page, const wchar_t *title, int x, int y, int width, int height)
{ return maage_ui_control(ui, page, L"STATIC", title, 0, x,y,width,height,0); }
static HWND maage_ui_button(maage_launcher_win32 *ui, int page, const wchar_t *title, int x, int y, int width, int id)
{ return maage_ui_control(ui, page, L"BUTTON", title, WS_TABSTOP | BS_PUSHBUTTON, x,y,width,32,id); }
static void maage_ui_layout(maage_launcher_win32 *ui)
{
    RECT rect; unsigned i; int width, height, x, y, w, h, center;
    GetClientRect(ui->window, &rect); width = rect.right; height = rect.bottom; center = 170 + (width - 170) / 2;
    for (i = 0; i < ui->count; ++i) {
        maage_launcher_control *c = &ui->controls[i]; x=c->x; y=c->y; w=c->width; h=c->height;
        if (y < 0) y += height;
        if (x < 0) x += width;
        if (w < 0) w += width;
        if (h < 0) h += height;
        if (c->page == MAAGE_PAGE_PLAY && c->y >= 150) { x = center - w/2; y = height/2 + c->y - 285; }
        MoveWindow(c->handle,x,y,w,h,TRUE);
        ShowWindow(c->handle, c->page < 0 || c->page == (int)ui->state->page ? SW_SHOW : SW_HIDE);
    }
    for (i = 0; i < 4; ++i) SendMessageW(ui->navigation[i], BM_SETCHECK, i == (unsigned)ui->state->page ? BST_CHECKED : BST_UNCHECKED, 0);
    InvalidateRect(ui->window, NULL, TRUE);
}
static int maage_ui_selected(maage_launcher_win32 *ui)
{ return ListView_GetNextItem(ui->list, -1, LVNI_SELECTED); }
static void maage_ui_installations_reload(maage_launcher_win32 *ui)
{
    unsigned i; wchar_t wide[160]; LVITEMW item;
    ui->updating = 1; ListView_DeleteAllItems(ui->list); SendMessageW(ui->installation, CB_RESETCONTENT, 0, 0);
    for (i = 0; i < ui->state->count; ++i) {
        maage_installation *entry = &ui->state->installations[i];
        MultiByteToWideChar(CP_UTF8,0,entry->name,-1,wide,160);
        memset(&item,0,sizeof(item)); item.mask=LVIF_TEXT; item.iItem=(int)i; item.pszText=wide;
        SendMessageW(ui->list,LVM_INSERTITEMW,0,(LPARAM)&item);
        item.iSubItem=1; item.pszText=L"Minecraft 1.7.10 resources"; SendMessageW(ui->list,LVM_SETITEMTEXTW,i,(LPARAM)&item);
        item.iSubItem=2; item.pszText = entry->id == ui->state->default_id ? L"Default" : entry->id == ui->state->selected ? L"Selected" : L"";
        SendMessageW(ui->list,LVM_SETITEMTEXTW,i,(LPARAM)&item);
        SendMessageW(ui->installation,CB_ADDSTRING,0,(LPARAM)wide);
        if (entry->id == ui->state->selected) SendMessageW(ui->installation,CB_SETCURSEL,i,0);
    }
    ui->updating = 0;
}
static void maage_ui_refresh(maage_launcher_win32 *ui)
{
    maage_launcher_ui *state = ui->state; unsigned i; int selected = -1, added = 0;
    maage_launcher_ui_refresh(state);
    if (ui->revision != state->accounts.revision || SendMessageW(ui->account, CB_GETCOUNT,0,0) == 0) {
        wchar_t wide[256]; char title[256];
        ui->updating=1; SendMessageW(ui->account,CB_RESETCONTENT,0,0);
        for (i=0; i<state->accounts.count; ++i) {
            maage_access_profile *p=&state->accounts.profiles[i]; if (!p->connected) continue;
            wsprintfA(title,"%s - %s",p->name,maage_access_state_name(p->state)); MultiByteToWideChar(CP_UTF8,0,title,-1,wide,256);
            SendMessageW(ui->account,CB_ADDSTRING,0,(LPARAM)wide); SendMessageW(ui->account,CB_SETITEMDATA,added,i);
            if (!strcmp(p->uuid,state->account)) selected=added;
            ++added;
        }
        if (!added) SendMessageW(ui->account,CB_ADDSTRING,0,(LPARAM)L"No connected Minecraft account");
        {
            LRESULT row=SendMessageW(ui->account,CB_ADDSTRING,0,(LPARAM)L"Manage Microsoft Accounts...");
            SendMessageW(ui->account,CB_SETITEMDATA,row,(LPARAM)-2);
            row=SendMessageW(ui->account,CB_ADDSTRING,0,(LPARAM)L"Add Microsoft Account...");
            SendMessageW(ui->account,CB_SETITEMDATA,row,(LPARAM)-4);
            if (state->account[0]) {
                row=SendMessageW(ui->account,CB_ADDSTRING,0,(LPARAM)L"Refresh / Reverify Account");
                SendMessageW(ui->account,CB_SETITEMDATA,row,(LPARAM)-3);
                row=SendMessageW(ui->account,CB_ADDSTRING,0,(LPARAM)L"Sign Out"); SendMessageW(ui->account,CB_SETITEMDATA,row,(LPARAM)-5);
                row=SendMessageW(ui->account,CB_ADDSTRING,0,(LPARAM)L"Remove Account"); SendMessageW(ui->account,CB_SETITEMDATA,row,(LPARAM)-6);
            }
        }
        SendMessageW(ui->account,CB_SETCURSEL,selected<0 ? 0 : selected,0); ui->revision=state->accounts.revision; ui->updating=0;
    }
    EnableWindow(ui->account,!state->accounts.busy); EnableWindow(ui->verify,!state->accounts.busy && state->account[0]); EnableWindow(ui->repair,!state->accounts.busy);
    maage_ui_text(ui->status,state->readiness); maage_ui_text(ui->resource,state->progress.stage==MAAGE_SETUP_FAILED ? state->progress.error : state->readiness);
    maage_ui_text(ui->account_status,state->account_status); maage_ui_text(ui->error,state->error);
    SetWindowTextW(ui->repair,state->accounts.allowed ? L"Repair Assets..." : L"Verify Minecraft Access...");
    for (i=1; i<5; ++i) EnableWindow(GetDlgItem(ui->window,MAAGE_UI_NEW+(int)i),maage_ui_selected(ui)>=0);
}
static void maage_ui_installation_action(maage_launcher_win32 *ui, int id)
{
    int row=maage_ui_selected(ui); wchar_t wide[128]; char name[512];
    if (id != MAAGE_UI_NEW && row < 0) return;
    if (id == MAAGE_UI_REMOVE) {
        if (MessageBoxW(ui->window,L"Remove this MaaGE configuration? Minecraft accounts and resources are preserved.",L"Remove Installation",MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
            maage_launcher_installation_remove(ui->state,(unsigned)row);
    } else if (id == MAAGE_UI_DEFAULT) maage_launcher_installation_default(ui->state,(unsigned)row);
    else {
        GetWindowTextW(ui->name,wide,128); WideCharToMultiByte(CP_UTF8,0,wide,-1,name,sizeof(name),NULL,NULL);
        if (id == MAAGE_UI_DUPLICATE) { strcpy(name,ui->state->installations[row].name); strcat(name," Copy"); }
        maage_launcher_installation_write(ui->state,id==MAAGE_UI_EDIT ? row : -1,name);
    }
    maage_ui_installations_reload(ui); maage_ui_refresh(ui);
}
static LRESULT CALLBACK maage_ui_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    maage_launcher_win32 *ui=(maage_launcher_win32 *)GetWindowLongPtrW(window,GWLP_USERDATA);
    if (message==WM_NCCREATE) { ui=(maage_launcher_win32 *)((CREATESTRUCTW *)lparam)->lpCreateParams; ui->window=window; SetWindowLongPtrW(window,GWLP_USERDATA,(LONG_PTR)ui); }
    if (!ui) return DefWindowProcW(window,message,wparam,lparam);
    switch(message) {
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lparam)->ptMinTrackSize.x=1020; ((MINMAXINFO *)lparam)->ptMinTrackSize.y=740; return 0;
    case WM_SIZE: maage_ui_layout(ui); return 0;
    case WM_TIMER: maage_ui_refresh(ui); return 0;
    case WM_COMMAND:
        {
            int id=LOWORD(wparam), event=HIWORD(wparam);
            if (id>=100 && id<104) { ui->state->page=(maage_launcher_page)(id-100); maage_ui_layout(ui); return 0; }
            if (id==MAAGE_UI_STATUS) { ui->state->page=MAAGE_PAGE_SETTINGS; maage_ui_layout(ui); return 0; }
            if (id==MAAGE_UI_ACCOUNT && event==CBN_SELCHANGE && !ui->updating) {
                LRESULT row=SendMessageW(ui->account,CB_GETCURSEL,0,0), index;
                index=SendMessageW(ui->account,CB_GETITEMDATA,row,0);
                if (index>=0 && (unsigned)index<ui->state->accounts.count) maage_launcher_account_select(ui->state,ui->state->accounts.profiles[index].uuid);
                else if (index==-2 && !ui->state->accounts.busy) { ui->result=1; DestroyWindow(window); return 0; }
                else if (index==-3 && ui->state->account[0]) maage_access_verify_local(ui->state->access,ui->state->account);
                else if (index==-4 && maage_access_sign_in(ui->state->access,0)) { ui->result=1; DestroyWindow(window); return 0; }
                else if (index==-5 && ui->state->account[0]) maage_access_sign_out(ui->state->access,ui->state->account);
                else if (index==-6 && ui->state->account[0]) maage_access_disconnect(ui->state->access,ui->state->account);
                ui->revision=0;
            }
            if (id==MAAGE_UI_INSTALLATION && event==CBN_SELCHANGE && !ui->updating) { maage_launcher_installation_select(ui->state,(unsigned)SendMessageW(ui->installation,CB_GETCURSEL,0,0)); maage_ui_installations_reload(ui); }
            if (id>=MAAGE_UI_NEW && id<=MAAGE_UI_DEFAULT) maage_ui_installation_action(ui,id);
            if (id==MAAGE_UI_VERIFY && ui->state->account[0]) maage_access_verify_local(ui->state->access,ui->state->account);
            if (id==MAAGE_UI_PROJECT && (INT_PTR)ShellExecuteW(window,L"open",L"https://github.com/FireController1847/MaaGE",NULL,NULL,SW_SHOWNORMAL)<=32)
                strcpy(ui->state->error,"Windows could not open the project link in your browser.");
            if ((id==MAAGE_UI_MANAGE || id==MAAGE_UI_REPAIR) && !ui->state->accounts.busy) {
                ui->result=id==MAAGE_UI_REPAIR && maage_access_allowed(ui->state->access) ? 2 : 1; DestroyWindow(window); return 0;
            }
            maage_ui_refresh(ui); return 0;
        }
    case WM_NOTIFY:
        if (((NMHDR *)lparam)->hwndFrom==ui->list && ((NMHDR *)lparam)->code==LVN_ITEMCHANGED && !ui->updating) {
            int row=maage_ui_selected(ui); if (row>=0) maage_ui_text(ui->name,ui->state->installations[row].name); maage_ui_refresh(ui);
        }
        return 0;
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wparam,GetSysColor(COLOR_WINDOWTEXT)); SetBkMode((HDC)wparam,TRANSPARENT); return (LRESULT)ui->background;
    case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT *draw=(DRAWITEMSTRUCT *)lparam;
            if (draw->CtlID==150) {
                FillRect(draw->hDC,&draw->rcItem,ui->magenta); SetTextColor(draw->hDC,RGB(235,220,230)); SetBkMode(draw->hDC,TRANSPARENT);
                DrawTextW(draw->hDC,L"Play",-1,&draw->rcItem,DT_CENTER | DT_VCENTER | DT_SINGLELINE); return TRUE;
            }
        }
        break;
    case WM_ERASEBKGND:
        {
            RECT rect; GetClientRect(window,&rect); FillRect((HDC)wparam,&rect,ui->background); rect.right=170; FillRect((HDC)wparam,&rect,ui->sidebar); return 1;
        }
    case WM_CLOSE: DestroyWindow(window); return 0;
    case WM_DESTROY: KillTimer(window,1); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(window,message,wparam,lparam);
}
int maage_launcher_native_run(maage_launcher_ui *state)
{
    maage_launcher_win32 ui; WNDCLASSW klass; MSG message; int received, i; HWND control; LVCOLUMNW column;
    const wchar_t *names[]={L"\x25b6  Play",L"\x2261  News",L"\x25a4  Installations",L"\x2699\xfe0e  Settings"}; INITCOMMONCONTROLSEX common;
    memset(&ui,0,sizeof(ui)); ui.state=state;
    common.dwSize=sizeof(common); common.dwICC=ICC_LISTVIEW_CLASSES; InitCommonControlsEx(&common);
    ui.font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    ui.heading=CreateFontW(-27,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    ui.brand=CreateFontW(-34,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    ui.background=CreateSolidBrush(GetSysColor(COLOR_WINDOW)); ui.sidebar=CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); ui.magenta=CreateSolidBrush(RGB(151,57,116));
    memset(&klass,0,sizeof(klass)); klass.lpfnWndProc=maage_ui_proc; klass.hInstance=GetModuleHandleW(NULL); klass.hCursor=LoadCursor(NULL,IDC_ARROW); klass.lpszClassName=L"MaaGELauncher";
    RegisterClassW(&klass); ui.window=CreateWindowExW(WS_EX_CONTROLPARENT,klass.lpszClassName,L"MaaGE Launcher",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,1080,780,NULL,NULL,klass.hInstance,&ui);
    if (!ui.window) { ui.result=-1; goto cleanup; }
    maage_ui_label(&ui,-1,L"MAAGE",20,24,130,24);
    for(i=0;i<4;++i) ui.navigation[i]=maage_ui_control(&ui,-1,L"BUTTON",names[i],WS_TABSTOP | BS_AUTORADIOBUTTON | BS_PUSHLIKE,14,i==3 ? -58 : 72+i*44,142,34,100+i);
    { const wchar_t *headings[]={L"Play MaaGE",L"News",L"Installations",L"Settings"};
      for(i=0;i<4;++i) { control=maage_ui_label(&ui,i,headings[i],198,25,330,38); SendMessageW(control,WM_SETFONT,(WPARAM)ui.heading,TRUE); } }
    ui.account=maage_ui_control(&ui,0,L"COMBOBOX",L"Active Minecraft account",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,-420,25,278,240,MAAGE_UI_ACCOUNT);
    maage_ui_button(&ui,0,L"Manage...",-132,25,105,MAAGE_UI_MANAGE);
    control=maage_ui_label(&ui,0,L"MaaGE",0,176,300,42); SendMessageW(control,WM_SETFONT,(WPARAM)ui.brand,TRUE); SetWindowLongPtrW(control,GWL_STYLE,GetWindowLongPtrW(control,GWL_STYLE)|SS_CENTER);
    control=maage_ui_label(&ui,0,L"Minecraft as a Game Engine",0,229,320,26); SetWindowLongPtrW(control,GWL_STYLE,GetWindowLongPtrW(control,GWL_STYLE)|SS_CENTER);
    ui.installation=maage_ui_control(&ui,0,L"COMBOBOX",L"MaaGE installation",WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,0,279,280,230,MAAGE_UI_INSTALLATION);
    control=maage_ui_control(&ui,0,L"BUTTON",L"Play",BS_OWNERDRAW,0,327,280,48,150); EnableWindow(control,FALSE);
    control=maage_ui_label(&ui,0,L"Client launching is not available in this build.",0,390,420,42); SetWindowLongPtrW(control,GWL_STYLE,GetWindowLongPtrW(control,GWL_STYLE)|SS_CENTER);
    maage_ui_label(&ui,1,L"No news source is configured yet.\nProject updates are available in the MaaGE repository.",198,100,-230,72);
    maage_ui_button(&ui,1,L"Open MaaGE Project",198,188,200,MAAGE_UI_PROJECT);
    maage_ui_label(&ui,2,L"MaaGE configurations - Minecraft 1.7.10 resource baseline",198,78,-230,28);
    ui.list=maage_ui_control(&ui,2,WC_LISTVIEWW,L"MaaGE installations",WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,198,120,-230,-350,MAAGE_UI_LIST);
    ListView_SetExtendedListViewStyle(ui.list,LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    memset(&column,0,sizeof(column)); column.mask=LVCF_TEXT | LVCF_WIDTH;
    column.pszText=L"Name"; column.cx=245; SendMessageW(ui.list,LVM_INSERTCOLUMNW,0,(LPARAM)&column);
    column.pszText=L"Configuration"; column.cx=260; SendMessageW(ui.list,LVM_INSERTCOLUMNW,1,(LPARAM)&column);
    column.pszText=L"Status"; column.cx=100; SendMessageW(ui.list,LVM_INSERTCOLUMNW,2,(LPARAM)&column);
    maage_ui_label(&ui,2,L"Configuration name",198,-205,200,26);
    ui.name=maage_ui_control(&ui,2,L"EDIT",L"New Installation",WS_TABSTOP | ES_AUTOHSCROLL,198,-175,-230,30,MAAGE_UI_NAME); SendMessageW(ui.name,EM_SETLIMITTEXT,95,0);
    { const wchar_t *titles[]={L"New",L"Save Name",L"Duplicate",L"Remove",L"Set as Default"}; for(i=0;i<5;++i) maage_ui_button(&ui,2,titles[i],198+i*128,-132,118,MAAGE_UI_NEW+i); }
    maage_ui_label(&ui,3,L"Launcher",198,80,250,26);
    maage_ui_label(&ui,3,L"Startup opens Play after account and resource preparation.",198,110,-230,26);
    maage_ui_button(&ui,3,L"Manage Minecraft Accounts...",198,146,245,MAAGE_UI_MANAGE);
    ui.verify=maage_ui_button(&ui,3,L"Refresh / Reverify Account",455,146,200,MAAGE_UI_VERIFY);
    ui.account_status=maage_ui_label(&ui,3,L"",198,188,-230,55);
    maage_ui_label(&ui,3,L"Resources",198,256,250,26);
    control=maage_ui_control(&ui,3,L"EDIT",L"Minecraft resource location",ES_READONLY | ES_AUTOHSCROLL | WS_TABSTOP,198,288,-230,30,0); maage_ui_text(control,state->resources);
    ui.resource=maage_ui_label(&ui,3,L"",198,330,-230,55); ui.repair=maage_ui_button(&ui,3,L"Repair Assets...",198,390,245,MAAGE_UI_REPAIR);
    maage_ui_label(&ui,3,L"About MaaGE",198,442,250,26);
    maage_ui_label(&ui,3,L"Minecraft as a Game Engine\nLauncher " MAAGE_LAUNCHER_VERSION L"\nOriginal code: Apache License 2.0. Minecraft resources are separate.\nNot affiliated with Mojang or Microsoft.",198,478,-230,94);
    maage_ui_button(&ui,3,L"MaaGE Project",198,584,200,MAAGE_UI_PROJECT);
    maage_ui_label(&ui,-1,L"Development build - " MAAGE_LAUNCHER_VERSION,198,-42,410,26);
    ui.status=maage_ui_button(&ui,-1,L"",-360,-48,330,MAAGE_UI_STATUS); ui.error=maage_ui_label(&ui,-1,L"",198,-87,-230,36);
    maage_ui_installations_reload(&ui); maage_ui_refresh(&ui); maage_ui_layout(&ui); SetFocus(ui.navigation[0]);
    if (!SetTimer(ui.window,1,250,NULL)) { ui.result=-1; DestroyWindow(ui.window); }
    else ShowWindow(ui.window,SW_SHOW);
    while ((received=GetMessageW(&message,NULL,0,0))>0) if (!IsDialogMessageW(ui.window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    if (received<0) { ui.result=-1; if (IsWindow(ui.window)) DestroyWindow(ui.window); }
cleanup:
    DeleteObject(ui.font); DeleteObject(ui.heading); DeleteObject(ui.brand); DeleteObject(ui.background); DeleteObject(ui.sidebar); DeleteObject(ui.magenta); UnregisterClassW(klass.lpszClassName,klass.hInstance); return ui.result;
}
